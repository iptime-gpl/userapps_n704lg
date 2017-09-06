

#include <linux/config.h>
#include <linux/module.h>

#include <linux/poll.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/apm_bios.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/desc.h>

#include <linux/sysrq.h>

extern unsigned long get_cmos_time(void);
extern void machine_real_restart(unsigned char *, int);

#if defined(CONFIG_APM_DISPLAY_BLANK) && defined(CONFIG_VT)
extern int (*console_blank_hook)(int);
#endif

/*
 * The apm_bios device is one of the misc char devices.
 * This is its minor number.
 */
#define	APM_MINOR_DEV	134

/*
 * See Documentation/Config.help for the configuration options.
 *
 * Various options can be changed at boot time as follows:
 * (We allow underscores for compatibility with the modules code)
 *	apm=on/off			enable/disable APM
 *	    [no-]allow[-_]ints		allow interrupts during BIOS calls
 *	    [no-]broken[-_]psr		BIOS has a broken GetPowerStatus call
 *	    [no-]realmode[-_]power[-_]off	switch to real mode before
 *	    					powering off
 *	    [no-]debug			log some debugging messages
 *	    [no-]power[-_]off		power off on shutdown
 *	    bounce[-_]interval=<n>	number of ticks to ignore suspend
 *	    				bounces
 *          idle[-_]threshold=<n>       System idle percentage above which to
 *                                      make APM BIOS idle calls. Set it to
 *                                      100 to disable.
 *          idle[-_]period=<n>          Period (in 1/100s of a second) over
 *                                      which the idle percentage is
 *                                      calculated.
 */


/*
 * Define as 1 to make the driver always call the APM BIOS busy
 * routine even if the clock was not reported as slowed by the
 * idle routine.  Otherwise, define as 0.
 */
#define ALWAYS_CALL_BUSY   1

/*
 * Define to make the APM BIOS calls zero all data segment registers (so
 * that an incorrect BIOS implementation will cause a kernel panic if it
 * tries to write to arbitrary memory).
 */
#define APM_ZERO_SEGS

/*
 * Define to make all _set_limit calls use 64k limits.  The APM 1.1 BIOS is
 * supposed to provide limit information that it recognizes.  Many machines
 * do this correctly, but many others do not restrict themselves to their
 * claimed limit.  When this happens, they will cause a segmentation
 * violation in the kernel at boot time.  Most BIOS's, however, will
 * respect a 64k limit, so we use that.  If you want to be pedantic and
 * hold your BIOS to its claims, then undefine this.
 */
#define APM_RELAX_SEGMENTS

/*
 * Define to re-initialize the interrupt 0 timer to 100 Hz after a suspend.
 * This patched by Chad Miller <cmiller@surfsouth.com>, original code by
 * David Chen <chen@ctpa04.mit.edu>
 */
#undef INIT_TIMER_AFTER_SUSPEND

#ifdef INIT_TIMER_AFTER_SUSPEND
#include <linux/timex.h>
#include <asm/io.h>
#include <linux/delay.h>
#endif

/*
 * Need to poll the APM BIOS every second
 */
#define APM_CHECK_TIMEOUT	(HZ)

/*
 * Ignore suspend events for this amount of time after a resume
 */
#define DEFAULT_BOUNCE_INTERVAL		(3 * HZ)

/*
 * Save a segment register away
 */
#define savesegment(seg, where) \
		__asm__ __volatile__("movl %%" #seg ",%0" : "=m" (where))

/*
 * Maximum number of events stored
 */
#define APM_MAX_EVENTS		20

/*
 * The per-file APM data
 */
struct apm_user {
	int		magic;
	struct apm_user *	next;
	int		suser: 1;
	int		writer: 1;
	int		reader: 1;
	int		suspend_wait: 1;
	int		suspend_result;
	int		suspends_pending;
	int		standbys_pending;
	int		suspends_read;
	int		standbys_read;
	int		event_head;
	int		event_tail;
	apm_event_t	events[APM_MAX_EVENTS];
};

/*
 * The magic number in apm_user
 */
#define APM_BIOS_MAGIC		0x4101

/*
 * idle percentage above which bios idle calls are done
 */
#ifdef CONFIG_APM_CPU_IDLE
#define DEFAULT_IDLE_THRESHOLD	95
#else
#define DEFAULT_IDLE_THRESHOLD	100
#endif
#define DEFAULT_IDLE_PERIOD	(100 / 3)

/*
 * Local variables
 */
static struct {
	unsigned long	offset;
	unsigned short	segment;
}				apm_bios_entry;
static int			clock_slowed;
static int			idle_threshold = DEFAULT_IDLE_THRESHOLD;
static int			idle_period = DEFAULT_IDLE_PERIOD;
static int			set_pm_idle;
static int			suspends_pending;
static int			standbys_pending;
static int			ignore_sys_suspend;
static int			ignore_normal_resume;
static int			bounce_interval = DEFAULT_BOUNCE_INTERVAL;

#ifdef CONFIG_APM_RTC_IS_GMT
#	define	clock_cmos_diff	0
#	define	got_clock_diff	1
#else
static long			clock_cmos_diff;
static int			got_clock_diff;
#endif
static int			debug;
static int			apm_disabled = -1;
#ifdef CONFIG_SMP
static int			power_off;
#else
static int			power_off = 1;
#endif
#ifdef CONFIG_APM_REAL_MODE_POWER_OFF
static int			realmode_power_off = 1;
#else
static int			realmode_power_off;
#endif
static int			exit_kapmd;
static int			kapmd_running;
#ifdef CONFIG_APM_ALLOW_INTS
static int			allow_ints = 1;
#else
static int			allow_ints;
#endif
static int			broken_psr;

static DECLARE_WAIT_QUEUE_HEAD(apm_waitqueue);
static DECLARE_WAIT_QUEUE_HEAD(apm_suspend_waitqueue);
static struct apm_user *	user_list;

static char			driver_version[] = "1.16";	/* no spaces */

/*
 *	APM event names taken from the APM 1.2 specification. These are
 *	the message codes that the BIOS uses to tell us about events
 */
static char *	apm_event_name[] = {
	"system standby",
	"system suspend",
	"normal resume",
	"critical resume",
	"low battery",
	"power status change",
	"update time",
	"critical suspend",
	"user standby",
	"user suspend",
	"system standby resume",
	"capabilities change"
};
#define NR_APM_EVENT_NAME	\
		(sizeof(apm_event_name) / sizeof(apm_event_name[0]))

typedef struct lookup_t {
	int	key;
	char *	msg;
} lookup_t;

/*
 *	The BIOS returns a set of standard error codes in AX when the
 *	carry flag is set.
 */
 
static const lookup_t error_table[] = {
/* N/A	{ APM_SUCCESS,		"Operation succeeded" }, */
	{ APM_DISABLED,		"Power management disabled" },
	{ APM_CONNECTED,	"Real mode interface already connected" },
	{ APM_NOT_CONNECTED,	"Interface not connected" },
	{ APM_16_CONNECTED,	"16 bit interface already connected" },
/* N/A	{ APM_16_UNSUPPORTED,	"16 bit interface not supported" }, */
	{ APM_32_CONNECTED,	"32 bit interface already connected" },
	{ APM_32_UNSUPPORTED,	"32 bit interface not supported" },
	{ APM_BAD_DEVICE,	"Unrecognized device ID" },
	{ APM_BAD_PARAM,	"Parameter out of range" },
	{ APM_NOT_ENGAGED,	"Interface not engaged" },
	{ APM_BAD_FUNCTION,     "Function not supported" },
	{ APM_RESUME_DISABLED,	"Resume timer disabled" },
	{ APM_BAD_STATE,	"Unable to enter requested state" },
/* N/A	{ APM_NO_EVENTS,	"No events pending" }, */
	{ APM_NO_ERROR,		"BIOS did not set a return code" },
	{ APM_NOT_PRESENT,	"No APM present" }
};
#define ERROR_COUNT	(sizeof(error_table)/sizeof(lookup_t))

/**
 *	apm_error	-	display an APM error
 *	@str: information string
 *	@err: APM BIOS return code
 *
 *	Write a meaningful log entry to the kernel log in the event of
 *	an APM error.
 */
 
static void apm_error(char *str, int err)
{
	int	i;

	for (i = 0; i < ERROR_COUNT; i++)
		if (error_table[i].key == err) break;
	if (i < ERROR_COUNT)
		printk(KERN_NOTICE "apm: %s: %s\n", str, error_table[i].msg);
	else
		printk(KERN_NOTICE "apm: %s: unknown error code %#2.2x\n",
			str, err);
}

/*
 * These are the actual BIOS calls.  Depending on APM_ZERO_SEGS and
 * apm_info.allow_ints, we are being really paranoid here!  Not only
 * are interrupts disabled, but all the segment registers (except SS)
 * are saved and zeroed this means that if the BIOS tries to reference
 * any data without explicitly loading the segment registers, the kernel
 * will fault immediately rather than have some unforeseen circumstances
 * for the rest of the kernel.  And it will be very obvious!  :-) Doing
 * this depends on CS referring to the same physical memory as DS so that
 * DS can be zeroed before the call. Unfortunately, we can't do anything
 * about the stack segment/pointer.  Also, we tell the compiler that
 * everything could change.
 *
 * Also, we KNOW that for the non error case of apm_bios_call, there
 * is no useful data returned in the low order 8 bits of eax.
 */
#define APM_DO_CLI	\
	if (apm_info.allow_ints) \
		__sti(); \
	else \
		__cli();

#ifdef APM_ZERO_SEGS
#	define APM_DECL_SEGS \
		unsigned int saved_fs; unsigned int saved_gs;
#	define APM_DO_SAVE_SEGS \
		savesegment(fs, saved_fs); savesegment(gs, saved_gs)
#	define APM_DO_ZERO_SEGS \
		"pushl %%ds\n\t" \
		"pushl %%es\n\t" \
		"xorl %%edx, %%edx\n\t" \
		"mov %%dx, %%ds\n\t" \
		"mov %%dx, %%es\n\t" \
		"mov %%dx, %%fs\n\t" \
		"mov %%dx, %%gs\n\t"
#	define APM_DO_POP_SEGS \
		"popl %%es\n\t" \
		"popl %%ds\n\t"
#	define APM_DO_RESTORE_SEGS \
		loadsegment(fs, saved_fs); loadsegment(gs, saved_gs)
#else
#	define APM_DECL_SEGS
#	define APM_DO_SAVE_SEGS
#	define APM_DO_ZERO_SEGS
#	define APM_DO_POP_SEGS
#	define APM_DO_RESTORE_SEGS
#endif

/**
 *	apm_bios_call	-	Make an APM BIOS 32bit call
 *	@func: APM function to execute
 *	@ebx_in: EBX register for call entry
 *	@ecx_in: ECX register for call entry
 *	@eax: EAX register return
 *	@ebx: EBX register return
 *	@ecx: ECX register return
 *	@edx: EDX register return
 *	@esi: ESI register return
 *
 *	Make an APM call using the 32bit protected mode interface. The
 *	caller is responsible for knowing if APM BIOS is configured and
 *	enabled. This call can disable interrupts for a long period of
 *	time on some laptops.  The return value is in AH and the carry
 *	flag is loaded into AL.  If there is an error, then the error
 *	code is returned in AH (bits 8-15 of eax) and this function
 *	returns non-zero.
 */
 
static u8 apm_bios_call(u32 func, u32 ebx_in, u32 ecx_in,
	u32 *eax, u32 *ebx, u32 *ecx, u32 *edx, u32 *esi)
{
	APM_DECL_SEGS
	unsigned long	flags;

	__save_flags(flags);
	APM_DO_CLI;
	APM_DO_SAVE_SEGS;
	/*
	 * N.B. We do NOT need a cld after the BIOS call
	 * because we always save and restore the flags.
	 */
	__asm__ __volatile__(APM_DO_ZERO_SEGS
		"pushl %%edi\n\t"
		"pushl %%ebp\n\t"
		"lcall %%cs:" SYMBOL_NAME_STR(apm_bios_entry) "\n\t"
		"setc %%al\n\t"
		"popl %%ebp\n\t"
		"popl %%edi\n\t"
		APM_DO_POP_SEGS
		: "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx),
		  "=S" (*esi)
		: "a" (func), "b" (ebx_in), "c" (ecx_in)
		: "memory", "cc");
	APM_DO_RESTORE_SEGS;
	__restore_flags(flags);
	return *eax & 0xff;
}

/**
 *	apm_bios_call_simple	-	make a simple APM BIOS 32bit call
 *	@func: APM function to invoke
 *	@ebx_in: EBX register value for BIOS call
 *	@ecx_in: ECX register value for BIOS call
 *	@eax: EAX register on return from the BIOS call
 *
 *	Make a BIOS call that does only returns one value, or just status.
 *	If there is an error, then the error code is returned in AH
 *	(bits 8-15 of eax) and this function returns non-zero. This is
 *	used for simpler BIOS operations. This call may hold interrupts
 *	off for a long time on some laptops.
 */

static u8 apm_bios_call_simple(u32 func, u32 ebx_in, u32 ecx_in, u32 *eax)
{
	u8		error;
	APM_DECL_SEGS
	unsigned long	flags;

	__save_flags(flags);
	APM_DO_CLI;
	APM_DO_SAVE_SEGS;
	{
		int	cx, dx, si;

		/*
		 * N.B. We do NOT need a cld after the BIOS call
		 * because we always save and restore the flags.
		 */
		__asm__ __volatile__(APM_DO_ZERO_SEGS
			"pushl %%edi\n\t"
			"pushl %%ebp\n\t"
			"lcall %%cs:" SYMBOL_NAME_STR(apm_bios_entry) "\n\t"
			"setc %%bl\n\t"
			"popl %%ebp\n\t"
			"popl %%edi\n\t"
			APM_DO_POP_SEGS
			: "=a" (*eax), "=b" (error), "=c" (cx), "=d" (dx),
			  "=S" (si)
			: "a" (func), "b" (ebx_in), "c" (ecx_in)
			: "memory", "cc");
	}
	APM_DO_RESTORE_SEGS;
	__restore_flags(flags);
	return error;
}

/**
 *	apm_driver_version	-	APM driver version
 *	@val:	loaded with the APM version on return
 *
 *	Retrieve the APM version supported by the BIOS. This is only
 *	supported for APM 1.1 or higher. An error indicates APM 1.0 is
 *	probably present.
 *
 *	On entry val should point to a value indicating the APM driver
 *	version with the high byte being the major and the low byte the
 *	minor number both in BCD
 *
 *	On return it will hold the BIOS revision supported in the
 *	same format.
 */

static int __init apm_driver_version(u_short *val)
{
	u32	eax;

	if (apm_bios_call_simple(APM_FUNC_VERSION, 0, *val, &eax))
		return (eax >> 8) & 0xff;
	*val = eax;
	return APM_SUCCESS;
}

/**
 *	apm_get_event	-	get an APM event from the BIOS
 *	@event: pointer to the event
 *	@info: point to the event information
 *
 *	The APM BIOS provides a polled information for event
 *	reporting. The BIOS expects to be polled at least every second
 *	when events are pending. When a message is found the caller should
 *	poll until no more messages are present.  However, this causes
 *	problems on some laptops where a suspend event notification is
 *	not cleared until it is acknowledged.
 *
 *	Additional information is returned in the info pointer, providing
 *	that APM 1.2 is in use. If no messges are pending the value 0x80
 *	is returned (No power management events pending).
 */
 
static int apm_get_event(apm_event_t *event, apm_eventinfo_t *info)
{
	u32	eax;
	u32	ebx;
	u32	ecx;
	u32	dummy;

	if (apm_bios_call(APM_FUNC_GET_EVENT, 0, 0, &eax, &ebx, &ecx,
			&dummy, &dummy))
		return (eax >> 8) & 0xff;
	*event = ebx;
	if (apm_info.connection_version < 0x0102)
		*info = ~0; /* indicate info not valid */
	else
		*info = ecx;
	return APM_SUCCESS;
}

/**
 *	set_power_state	-	set the power management state
 *	@what: which items to transition
 *	@state: state to transition to
 *
 *	Request an APM change of state for one or more system devices. The
 *	processor state must be transitioned last of all. what holds the
 *	class of device in the upper byte and the device number (0xFF for
 *	all) for the object to be transitioned.
 *
 *	The state holds the state to transition to, which may in fact
 *	be an acceptance of a BIOS requested state change.
 */
 
static int set_power_state(u_short what, u_short state)
{
	u32	eax;

	if (apm_bios_call_simple(APM_FUNC_SET_STATE, what, state, &eax))
		return (eax >> 8) & 0xff;
	return APM_SUCCESS;
}

/**
 *	set_system_power_state - set system wide power state
 *	@state: which state to enter
 *
 *	Transition the entire system into a new APM power state.
 */
 
static int set_system_power_state(u_short state)
{
	return set_power_state(APM_DEVICE_ALL, state);
}

/**
 *	apm_do_idle	-	perform power saving
 *
 *	This function notifies the BIOS that the processor is (in the view
 *	of the OS) idle. It returns -1 in the event that the BIOS refuses
 *	to handle the idle request. On a success the function returns 1
 *	if the BIOS did clock slowing or 0 otherwise.
 */
 
static int apm_do_idle(void)
{
	u32	eax;

	if (apm_bios_call_simple(APM_FUNC_IDLE, 0, 0, &eax)) {
		static unsigned long t;

		if (time_after(jiffies, t + 10 * HZ)) {
			printk(KERN_DEBUG "apm_do_idle failed (%d)\n",
					(eax >> 8) & 0xff);
			t = jiffies;
		}
		return -1;
	}
	clock_slowed = (apm_info.bios.flags & APM_IDLE_SLOWS_CLOCK) != 0;
	return clock_slowed;
}

/**
 *	apm_do_busy	-	inform the BIOS the CPU is busy
 *
 *	Request that the BIOS brings the CPU back to full performance. 
 */
 
static void apm_do_busy(void)
{
	u32	dummy;

	if (clock_slowed || ALWAYS_CALL_BUSY) {
		(void) apm_bios_call_simple(APM_FUNC_BUSY, 0, 0, &dummy);
		clock_slowed = 0;
	}
}

/*
 * If no process has really been interested in
 * the CPU for some time, we want to call BIOS
 * power management - we probably want
 * to conserve power.
 */
#define IDLE_CALC_LIMIT   (HZ * 100)
#define IDLE_LEAKY_MAX    16

static void (*original_pm_idle)(void);

extern void default_idle(void);

/**
 * apm_cpu_idle		-	cpu idling for APM capable Linux
 *
 * This is the idling function the kernel executes when APM is available. It 
 * tries to do BIOS powermanagement based on the average system idle time.
 * Furthermore it calls the system default idle routine.
 */

static void apm_cpu_idle(void)
{
	static int use_apm_idle; /* = 0 */
	static unsigned int last_jiffies; /* = 0 */
	static unsigned int last_stime; /* = 0 */

	int apm_idle_done = 0;
	unsigned int jiffies_since_last_check = jiffies - last_jiffies;
	unsigned int bucket;

recalc:
	if (jiffies_since_last_check > IDLE_CALC_LIMIT) {
		use_apm_idle = 0;
		last_jiffies = jiffies;
		last_stime = current->times.tms_stime;
	} else if (jiffies_since_last_check > idle_period) {
		unsigned int idle_percentage;

		idle_percentage = current->times.tms_stime - last_stime;
		idle_percentage *= 100;
		idle_percentage /= jiffies_since_last_check;
		use_apm_idle = (idle_percentage > idle_threshold);
		last_jiffies = jiffies;
		last_stime = current->times.tms_stime;
	}

	bucket = IDLE_LEAKY_MAX;

	while (!current->need_resched) {
		if (use_apm_idle) {
			unsigned int t;

			t = jiffies;
			switch (apm_do_idle()) {
			case 0: apm_idle_done = 1;
				if (t != jiffies) {
					if (bucket) {
						bucket = IDLE_LEAKY_MAX;
						continue;
					}
				} else if (bucket) {
					bucket--;
					continue;
				}
				break;
			case 1: apm_idle_done = 1;
				break;
			default: /* BIOS refused */
				break;
			}
		}
		if (original_pm_idle)
			original_pm_idle();
		else
			default_idle();
		jiffies_since_last_check = jiffies - last_jiffies;
		if (jiffies_since_last_check > idle_period)
			goto recalc;
	}

	if (apm_idle_done)
		apm_do_busy();
}

/**
 *	apm_power_off	-	ask the BIOS to power off
 *
 *	Handle the power off sequence. This is the one piece of code we
 *	will execute even on SMP machines. In order to deal with BIOS
 *	bugs we support real mode APM BIOS power off calls. We also make
 *	the SMP call on CPU0 as some systems will only honour this call
 *	on their first cpu.
 */
 
static void apm_power_off(void)
{
	unsigned char	po_bios_call[] = {
		0xb8, 0x00, 0x10,	/* movw  $0x1000,ax  */
		0x8e, 0xd0,		/* movw  ax,ss       */
		0xbc, 0x00, 0xf0,	/* movw  $0xf000,sp  */
		0xb8, 0x07, 0x53,	/* movw  $0x5307,ax  */
		0xbb, 0x01, 0x00,	/* movw  $0x0001,bx  */
		0xb9, 0x03, 0x00,	/* movw  $0x0003,cx  */
		0xcd, 0x15		/* int   $0x15       */
	};

	/*
	 * This may be called on an SMP machine.
	 */
#ifdef CONFIG_SMP
	/* Some bioses don't like being called from CPU != 0 */
	if (cpu_number_map(smp_processor_id()) != 0) {
		current->cpus_allowed = 1;
		schedule();
		if (unlikely(cpu_number_map(smp_processor_id()) != 0))
			BUG();
	}
#endif
	if (apm_info.realmode_power_off)
		machine_real_restart(po_bios_call, sizeof(po_bios_call));
	else
		(void) set_system_power_state(APM_STATE_OFF);
}

/**
 * handle_poweroff	-	sysrq callback for power down
 * @key: key pressed (unused)
 * @pt_regs: register state (unused)
 * @kbd: keyboard state (unused)
 * @tty: tty involved (unused)
 *
 * When the user hits Sys-Rq o to power down the machine this is the
 * callback we use.
 */

void handle_poweroff (int key, struct pt_regs *pt_regs,
		struct kbd_struct *kbd, struct tty_struct *tty) {
        apm_power_off();
}

struct sysrq_key_op	sysrq_poweroff_op = {
	handler:        handle_poweroff,
	help_msg:       "Off",
	action_msg:     "Power Off\n"
};


#ifdef CONFIG_APM_DO_ENABLE

/**
 *	apm_enable_power_management - enable BIOS APM power management
 *	@enable: enable yes/no
 *
 *	Enable or disable the APM BIOS power services. 
 */
 
static int apm_enable_power_management(int enable)
{
	u32	eax;

	if ((enable == 0) && (apm_info.bios.flags & APM_BIOS_DISENGAGED))
		return APM_NOT_ENGAGED;
	if (apm_bios_call_simple(APM_FUNC_ENABLE_PM, APM_DEVICE_BALL,
			enable, &eax))
		return (eax >> 8) & 0xff;
	if (enable)
		apm_info.bios.flags &= ~APM_BIOS_DISABLED;
	else
		apm_info.bios.flags |= APM_BIOS_DISABLED;
	return APM_SUCCESS;
}
#endif

/**
 *	apm_get_power_status	-	get current power state
 *	@status: returned status
 *	@bat: battery info
 *	@life: estimated life
 *
 *	Obtain the current power status from the APM BIOS. We return a
 *	status which gives the rough battery status, and current power
 *	source. The bat value returned give an estimate as a percentage
 *	of life and a status value for the battery. The estimated life
 *	if reported is a lifetime in secodnds/minutes at current powwer
 *	consumption.
 */
 
static int apm_get_power_status(u_short *status, u_short *bat, u_short *life)
{
	u32	eax;
	u32	ebx;
	u32	ecx;
	u32	edx;
	u32	dummy;

	if (apm_info.get_power_status_broken)
		return APM_32_UNSUPPORTED;
	if (apm_bios_call(APM_FUNC_GET_STATUS, APM_DEVICE_ALL, 0,
			&eax, &ebx, &ecx, &edx, &dummy))
		return (eax >> 8) & 0xff;
	*status = ebx;
	*bat = ecx;
	if (apm_info.get_power_status_swabinminutes) {
		*life = swab16((u16)edx);
		*life |= 0x8000;
	} else
		*life = edx;
	return APM_SUCCESS;
}


/**
 *	apm_engage_power_management	-	enable PM on a device
 *	@device: identity of device
 *	@enable: on/off
 *
 *	Activate or deactive power management on either a specific device
 *	or the entire system (%APM_DEVICE_ALL).
 */
 
static int apm_engage_power_management(u_short device, int enable)
{
	u32	eax;

	if ((enable == 0) && (device == APM_DEVICE_ALL)
	    && (apm_info.bios.flags & APM_BIOS_DISABLED))
		return APM_DISABLED;
	if (apm_bios_call_simple(APM_FUNC_ENGAGE_PM, device, enable, &eax))
		return (eax >> 8) & 0xff;
	if (device == APM_DEVICE_ALL) {
		if (enable)
			apm_info.bios.flags &= ~APM_BIOS_DISENGAGED;
		else
			apm_info.bios.flags |= APM_BIOS_DISENGAGED;
	}
	return APM_SUCCESS;
}

#if defined(CONFIG_APM_DISPLAY_BLANK) && defined(CONFIG_VT)

/**
 *	apm_console_blank	-	blank the display
 *	@blank: on/off
 *
 *	Attempt to blank the console, firstly by blanking just video device
 *	zero, and if that fails (some BIOSes dont support it) then it blanks
 *	all video devices. Typically the BIOS will do laptop backlight and
 *	monitor powerdown for us.
 */
 
static int apm_console_blank(int blank)
{
	int	error;
	u_short	state;

	state = blank ? APM_STATE_STANDBY : APM_STATE_READY;
	/* Blank the first display device */
	error = set_power_state(0x100, state);
	if ((error != APM_SUCCESS) && (error != APM_NO_ERROR)) {
		/* try to blank them all instead */
		error = set_power_state(0x1ff, state);
		if ((error != APM_SUCCESS) && (error != APM_NO_ERROR))
			/* try to blank device one instead */
			error = set_power_state(0x101, state);
	}
	if ((error == APM_SUCCESS) || (error == APM_NO_ERROR))
		return 1;
	apm_error("set display", error);
	return 0;
}
#endif

static int queue_empty(struct apm_user *as)
{
	return as->event_head == as->event_tail;
}

static apm_event_t get_queued_event(struct apm_user *as)
{
	as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
	return as->events[as->event_tail];
}

static void queue_event(apm_event_t event, struct apm_user *sender)
{
	struct apm_user *	as;

	if (user_list == NULL)
		return;
	for (as = user_list; as != NULL; as = as->next) {
		if ((as == sender) || (!as->reader))
			continue;
		as->event_head = (as->event_head + 1) % APM_MAX_EVENTS;
		if (as->event_head == as->event_tail) {
			static int notified;

			if (notified++ == 0)
			    printk(KERN_ERR "apm: an event queue overflowed\n");
			as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
		}
		as->events[as->event_head] = event;
		if ((!as->suser) || (!as->writer))
			continue;
		switch (event) {
		case APM_SYS_SUSPEND:
		case APM_USER_SUSPEND:
			as->suspends_pending++;
			suspends_pending++;
			break;

		case APM_SYS_STANDBY:
		case APM_USER_STANDBY:
			as->standbys_pending++;
			standbys_pending++;
			break;
		}
	}
	wake_up_interruptible(&apm_waitqueue);
}

static void set_time(void)
{
	unsigned long	flags;

	if (got_clock_diff) {	/* Must know time zone in order to set clock */
		save_flags(flags);
		cli();
		CURRENT_TIME = get_cmos_time() + clock_cmos_diff;
		restore_flags(flags);
	}
}

static void get_time_diff(void)
{
#ifndef CONFIG_APM_RTC_IS_GMT
	unsigned long	flags;

	/*
	 * Estimate time zone so that set_time can update the clock
	 */
	save_flags(flags);
	clock_cmos_diff = -get_cmos_time();
	cli();
	clock_cmos_diff += CURRENT_TIME;
	got_clock_diff = 1;
	restore_flags(flags);
#endif
}

static void reinit_timer(void)
{
#ifdef INIT_TIMER_AFTER_SUSPEND
	unsigned long	flags;

	save_flags(flags);
	cli();
	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	udelay(10);
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	udelay(10);
	outb(LATCH >> 8 , 0x40);	/* MSB */
	udelay(10);
	restore_flags(flags);
#endif
}

static int suspend(int vetoable)
{
	int		err;
	struct apm_user	*as;

	if (pm_send_all(PM_SUSPEND, (void *)3)) {
		/* Vetoed */
		if (vetoable) {
			if (apm_info.connection_version > 0x100)
				set_system_power_state(APM_STATE_REJECT);
			err = -EBUSY;
			ignore_sys_suspend = 0;
			printk(KERN_WARNING "apm: suspend was vetoed.\n");
			goto out;
		}
		printk(KERN_CRIT "apm: suspend was vetoed, but suspending anyway.\n");
	}
	get_time_diff();
	__cli();
	err = set_system_power_state(APM_STATE_SUSPEND);
	reinit_timer();
	set_time();
	ignore_normal_resume = 1;
	__sti();
	if (err == APM_NO_ERROR)
		err = APM_SUCCESS;
	if (err != APM_SUCCESS)
		apm_error("suspend", err);
	err = (err == APM_SUCCESS) ? 0 : -EIO;
	pm_send_all(PM_RESUME, (void *)0);
	queue_event(APM_NORMAL_RESUME, NULL);
 out:
	for (as = user_list; as != NULL; as = as->next) {
		as->suspend_wait = 0;
		as->suspend_result = err;
	}
	wake_up_interruptible(&apm_suspend_waitqueue);
	return err;
}

static void standby(void)
{
	int	err;

	/* If needed, notify drivers here */
	get_time_diff();
	err = set_system_power_state(APM_STATE_STANDBY);
	if ((err != APM_SUCCESS) && (err != APM_NO_ERROR))
		apm_error("standby", err);
}

static apm_event_t get_event(void)
{
	int		error;
	apm_event_t	event;
	apm_eventinfo_t	info;

	static int notified;

	/* we don't use the eventinfo */
	error = apm_get_event(&event, &info);
	if (error == APM_SUCCESS)
		return event;

	if ((error != APM_NO_EVENTS) && (notified++ == 0))
		apm_error("get_event", error);

	return 0;
}

static void check_events(void)
{
	apm_event_t		event;
	static unsigned long	last_resume;
	static int		ignore_bounce;

	while ((event = get_event()) != 0) {
		if (debug) {
			if (event <= NR_APM_EVENT_NAME)
				printk(KERN_DEBUG "apm: received %s notify\n",
				       apm_event_name[event - 1]);
			else
				printk(KERN_DEBUG "apm: received unknown "
				       "event 0x%02x\n", event);
		}
		if (ignore_bounce
		    && ((jiffies - last_resume) > bounce_interval))
			ignore_bounce = 0;

		switch (event) {
		case APM_SYS_STANDBY:
		case APM_USER_STANDBY:
			queue_event(event, NULL);
			if (standbys_pending <= 0)
				standby();
			break;

		case APM_USER_SUSPEND:
#ifdef CONFIG_APM_IGNORE_USER_SUSPEND
			if (apm_info.connection_version > 0x100)
				set_system_power_state(APM_STATE_REJECT);
			break;
#endif
		case APM_SYS_SUSPEND:
			if (ignore_bounce) {
				if (apm_info.connection_version > 0x100)
					set_system_power_state(APM_STATE_REJECT);
				break;
			}
			/*
			 * If we are already processing a SUSPEND,
			 * then further SUSPEND events from the BIOS
			 * will be ignored.  We also return here to
			 * cope with the fact that the Thinkpads keep
			 * sending a SUSPEND event until something else
			 * happens!
			 */
			if (ignore_sys_suspend)
				return;
			ignore_sys_suspend = 1;
			queue_event(event, NULL);
			if (suspends_pending <= 0)
				(void) suspend(1);
			break;

		case APM_NORMAL_RESUME:
		case APM_CRITICAL_RESUME:
		case APM_STANDBY_RESUME:
			ignore_sys_suspend = 0;
			last_resume = jiffies;
			ignore_bounce = 1;
			if ((event != APM_NORMAL_RESUME)
			    || (ignore_normal_resume == 0)) {
				set_time();
				pm_send_all(PM_RESUME, (void *)0);
				queue_event(event, NULL);
			}
			ignore_normal_resume = 0;
			break;

		case APM_CAPABILITY_CHANGE:
		case APM_LOW_BATTERY:
		case APM_POWER_STATUS_CHANGE:
			queue_event(event, NULL);
			/* If needed, notify drivers here */
			break;

		case APM_UPDATE_TIME:
			set_time();
			break;

		case APM_CRITICAL_SUSPEND:
			/*
			 * We are not allowed to reject a critical suspend.
			 */
			(void) suspend(0);
			break;
		}
	}
}

static void apm_event_handler(void)
{
	static int	pending_count = 4;
	int		err;

	if ((standbys_pending > 0) || (suspends_pending > 0)) {
		if ((apm_info.connection_version > 0x100) &&
				(pending_count-- <= 0)) {
			pending_count = 4;
			if (debug)
				printk(KERN_DEBUG "apm: setting state busy\n");
			err = set_system_power_state(APM_STATE_BUSY);
			if (err)
				apm_error("busy", err);
		}
	} else
		pending_count = 4;
	check_events();
}

/*
 * This is the APM thread main loop.
 */

static void apm_mainloop(void)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&apm_waitqueue, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	for (;;) {
		schedule_timeout(APM_CHECK_TIMEOUT);
		if (exit_kapmd)
			break;
		/*
		 * Ok, check all events, check for idle (and mark us sleeping
		 * so as not to count towards the load average)..
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		apm_event_handler();
	}
	remove_wait_queue(&apm_waitqueue, &wait);
}

static int check_apm_user(struct apm_user *as, const char *func)
{
	if ((as == NULL) || (as->magic != APM_BIOS_MAGIC)) {
		printk(KERN_ERR "apm: %s passed bad filp\n", func);
		return 1;
	}
	return 0;
}

static ssize_t do_read(struct file *fp, char *buf, size_t count, loff_t *ppos)
{
	struct apm_user *	as;
	int			i;
	apm_event_t		event;

	as = fp->private_data;
	if (check_apm_user(as, "read"))
		return -EIO;
	if (count < sizeof(apm_event_t))
		return -EINVAL;
	if ((queue_empty(as)) && (fp->f_flags & O_NONBLOCK))
		return -EAGAIN;
	wait_event_interruptible(apm_waitqueue, !queue_empty(as));
	i = count;
	while ((i >= sizeof(event)) && !queue_empty(as)) {
		event = get_queued_event(as);
		if (copy_to_user(buf, &event, sizeof(event))) {
			if (i < count)
				break;
			return -EFAULT;
		}
		switch (event) {
		case APM_SYS_SUSPEND:
		case APM_USER_SUSPEND:
			as->suspends_read++;
			break;

		case APM_SYS_STANDBY:
		case APM_USER_STANDBY:
			as->standbys_read++;
			break;
		}
		buf += sizeof(event);
		i -= sizeof(event);
	}
	if (i < count)
		return count - i;
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

static unsigned int do_poll(struct file *fp, poll_table * wait)
{
	struct apm_user * as;

	as = fp->private_data;
	if (check_apm_user(as, "poll"))
		return 0;
	poll_wait(fp, &apm_waitqueue, wait);
	if (!queue_empty(as))
		return POLLIN | POLLRDNORM;
	return 0;
}

static int do_ioctl(struct inode * inode, struct file *filp,
		    u_int cmd, u_long arg)
{
	struct apm_user *	as;

	as = filp->private_data;
	if (check_apm_user(as, "ioctl"))
		return -EIO;
	if ((!as->suser) || (!as->writer))
		return -EPERM;
	switch (cmd) {
	case APM_IOC_STANDBY:
		if (as->standbys_read > 0) {
			as->standbys_read--;
			as->standbys_pending--;
			standbys_pending--;
		} else
			queue_event(APM_USER_STANDBY, as);
		if (standbys_pending <= 0)
			standby();
		break;
	case APM_IOC_SUSPEND:
		if (as->suspends_read > 0) {
			as->suspends_read--;
			as->suspends_pending--;
			suspends_pending--;
		} else
			queue_event(APM_USER_SUSPEND, as);
		if (suspends_pending <= 0) {
			return suspend(1);
		} else {
			as->suspend_wait = 1;
			wait_event_interruptible(apm_suspend_waitqueue,
					as->suspend_wait == 0);
			return as->suspend_result;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int do_release(struct inode * inode, struct file * filp)
{
	struct apm_user *	as;

	as = filp->private_data;
	if (check_apm_user(as, "release"))
		return 0;
	filp->private_data = NULL;
	lock_kernel();
	if (as->standbys_pending > 0) {
		standbys_pending -= as->standbys_pending;
		if (standbys_pending <= 0)
			standby();
	}
	if (as->suspends_pending > 0) {
		suspends_pending -= as->suspends_pending;
		if (suspends_pending <= 0)
			(void) suspend(1);
	}
	if (user_list == as)
		user_list = as->next;
	else {
		struct apm_user *	as1;

		for (as1 = user_list;
		     (as1 != NULL) && (as1->next != as);
		     as1 = as1->next)
			;
		if (as1 == NULL)
			printk(KERN_ERR "apm: filp not in user list\n");
		else
			as1->next = as->next;
	}
	unlock_kernel();
	kfree(as);
	return 0;
}

static int do_open(struct inode * inode, struct file * filp)
{
	struct apm_user *	as;

	as = (struct apm_user *)kmalloc(sizeof(*as), GFP_KERNEL);
	if (as == NULL) {
		printk(KERN_ERR "apm: cannot allocate struct of size %d bytes\n",
		       sizeof(*as));
		return -ENOMEM;
	}
	as->magic = APM_BIOS_MAGIC;
	as->event_tail = as->event_head = 0;
	as->suspends_pending = as->standbys_pending = 0;
	as->suspends_read = as->standbys_read = 0;
	as->suser = capable(CAP_SYS_ADMIN);
	as->writer = (filp->f_mode & FMODE_WRITE) == FMODE_WRITE;
	as->reader = (filp->f_mode & FMODE_READ) == FMODE_READ;
	as->next = user_list;
	user_list = as;
	filp->private_data = as;
	return 0;
}

static int apm_get_info(char *buf, char **start, off_t fpos, int length)
{
	char *		p;
	unsigned short	bx;
	unsigned short	cx;
	unsigned short	dx;
	int		error;
	unsigned short  ac_line_status = 0xff;
	unsigned short  battery_status = 0xff;
	unsigned short  battery_flag   = 0xff;
	int		percentage     = -1;
	int             time_units     = -1;
	char            *units         = "?";

	p = buf;

	if ((smp_num_cpus == 1) &&
	    !(error = apm_get_power_status(&bx, &cx, &dx))) {
		ac_line_status = (bx >> 8) & 0xff;
		battery_status = bx & 0xff;
		if ((cx & 0xff) != 0xff)
			percentage = cx & 0xff;

		if (apm_info.connection_version > 0x100) {
			battery_flag = (cx >> 8) & 0xff;
			if (dx != 0xffff) {
				units = (dx & 0x8000) ? "min" : "sec";
				time_units = dx & 0x7fff;
			}
		}
	}
	/* Arguments, with symbols from linux/apm_bios.h.  Information is
	   from the Get Power Status (0x0a) call unless otherwise noted.

	   0) Linux driver version (this will change if format changes)
	   1) APM BIOS Version.  Usually 1.0, 1.1 or 1.2.
	   2) APM flags from APM Installation Check (0x00):
	      bit 0: APM_16_BIT_SUPPORT
	      bit 1: APM_32_BIT_SUPPORT
	      bit 2: APM_IDLE_SLOWS_CLOCK
	      bit 3: APM_BIOS_DISABLED
	      bit 4: APM_BIOS_DISENGAGED
	   3) AC line status
	      0x00: Off-line
	      0x01: On-line
	      0x02: On backup power (BIOS >= 1.1 only)
	      0xff: Unknown
	   4) Battery status
	      0x00: High
	      0x01: Low
	      0x02: Critical
	      0x03: Charging
	      0x04: Selected battery not present (BIOS >= 1.2 only)
	      0xff: Unknown
	   5) Battery flag
	      bit 0: High
	      bit 1: Low
	      bit 2: Critical
	      bit 3: Charging
	      bit 7: No system battery
	      0xff: Unknown
	   6) Remaining battery life (percentage of charge):
	      0-100: valid
	      -1: Unknown
	   7) Remaining battery life (time units):
	      Number of remaining minutes or seconds
	      -1: Unknown
	   8) min = minutes; sec = seconds */

	p += sprintf(p, "%s %d.%d 0x%02x 0x%02x 0x%02x 0x%02x %d%% %d %s\n",
		     driver_version,
		     (apm_info.bios.version >> 8) & 0xff,
		     apm_info.bios.version & 0xff,
		     apm_info.bios.flags,
		     ac_line_status,
		     battery_status,
		     battery_flag,
		     percentage,
		     time_units,
		     units);

	return p - buf;
}

static int apm(void *unused)
{
	unsigned short	bx;
	unsigned short	cx;
	unsigned short	dx;
	int		error;
	char *		power_stat;
	char *		bat_stat;

	kapmd_running = 1;

	daemonize();

	strcpy(current->comm, "kapmd");
	sigfillset(&current->blocked);

#ifdef CONFIG_SMP
	/* 2002/08/01 - WT
	 * This is to avoid random crashes at boot time during initialization
	 * on SMP systems in case of "apm=power-off" mode. Seen on ASUS A7M266D.
	 * Some bioses don't like being called from CPU != 0.
	 * Method suggested by Ingo Molnar.
	 */
	if (cpu_number_map(smp_processor_id()) != 0) {
		current->cpus_allowed = 1;
		schedule();
		if (unlikely(cpu_number_map(smp_processor_id()) != 0))
			BUG();
	}
#endif
	
	if (apm_info.connection_version == 0) {
		apm_info.connection_version = apm_info.bios.version;
		if (apm_info.connection_version > 0x100) {
			/*
			 * We only support BIOSs up to version 1.2
			 */
			if (apm_info.connection_version > 0x0102)
				apm_info.connection_version = 0x0102;
			error = apm_driver_version(&apm_info.connection_version);
			if (error != APM_SUCCESS) {
				apm_error("driver version", error);
				/* Fall back to an APM 1.0 connection. */
				apm_info.connection_version = 0x100;
			}
		}
	}

	if (debug)
		printk(KERN_INFO "apm: Connection version %d.%d\n",
			(apm_info.connection_version >> 8) & 0xff,
			apm_info.connection_version & 0xff);

#ifdef CONFIG_APM_DO_ENABLE
	if (apm_info.bios.flags & APM_BIOS_DISABLED) {
		/*
		 * This call causes my NEC UltraLite Versa 33/C to hang if it
		 * is booted with PM disabled but not in the docking station.
		 * Unfortunate ...
		 */
		error = apm_enable_power_management(1);
		if (error) {
			apm_error("enable power management", error);
			return -1;
		}
	}
#endif

	if ((apm_info.bios.flags & APM_BIOS_DISENGAGED)
	    && (apm_info.connection_version > 0x0100)) {
		error = apm_engage_power_management(APM_DEVICE_ALL, 1);
		if (error) {
			apm_error("engage power management", error);
			return -1;
		}
	}

	if (debug) {
		error = apm_get_power_status(&bx, &cx, &dx);
		if (error)
			printk(KERN_INFO "apm: power status not available\n");
		else {
			switch ((bx >> 8) & 0xff) {
			case 0: power_stat = "off line"; break;
			case 1: power_stat = "on line"; break;
			case 2: power_stat = "on backup power"; break;
			default: power_stat = "unknown"; break;
			}
			switch (bx & 0xff) {
			case 0: bat_stat = "high"; break;
			case 1: bat_stat = "low"; break;
			case 2: bat_stat = "critical"; break;
			case 3: bat_stat = "charging"; break;
			default: bat_stat = "unknown"; break;
			}
			printk(KERN_INFO
			       "apm: AC %s, battery status %s, battery life ",
			       power_stat, bat_stat);
			if ((cx & 0xff) == 0xff)
				printk("unknown\n");
			else
				printk("%d%%\n", cx & 0xff);
			if (apm_info.connection_version > 0x100) {
				printk(KERN_INFO
				       "apm: battery flag 0x%02x, battery life ",
				       (cx >> 8) & 0xff);
				if (dx == 0xffff)
					printk("unknown\n");
				else
					printk("%d %s\n", dx & 0x7fff,
						(dx & 0x8000) ?
						"minutes" : "seconds");
			}
		}
	}

	/* Install our power off handler.. */
	if (power_off)
		pm_power_off = apm_power_off;
	register_sysrq_key('o', &sysrq_poweroff_op);

	if (smp_num_cpus == 1) {
#if defined(CONFIG_APM_DISPLAY_BLANK) && defined(CONFIG_VT)
		console_blank_hook = apm_console_blank;
#endif
		apm_mainloop();
#if defined(CONFIG_APM_DISPLAY_BLANK) && defined(CONFIG_VT)
		console_blank_hook = NULL;
#endif
	}
	kapmd_running = 0;

	return 0;
}

#ifndef MODULE
static int __init apm_setup(char *str)
{
	int	invert;

	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "off", 3) == 0)
			apm_disabled = 1;
		if (strncmp(str, "on", 2) == 0)
			apm_disabled = 0;
		if ((strncmp(str, "bounce-interval=", 16) == 0) ||
		    (strncmp(str, "bounce_interval=", 16) == 0))
			bounce_interval = simple_strtol(str + 16, NULL, 0);
		if ((strncmp(str, "idle-threshold=", 15) == 0) ||
		    (strncmp(str, "idle_threshold=", 15) == 0))
			idle_threshold = simple_strtol(str + 15, NULL, 0);
		if ((strncmp(str, "idle-period=", 12) == 0) ||
		    (strncmp(str, "idle_period=", 12) == 0))
			idle_period = simple_strtol(str + 12, NULL, 0);
		invert = (strncmp(str, "no-", 3) == 0) ||
			(strncmp(str, "no_", 3) == 0);
		if (invert)
			str += 3;
		if (strncmp(str, "debug", 5) == 0)
			debug = !invert;
		if ((strncmp(str, "power-off", 9) == 0) ||
		    (strncmp(str, "power_off", 9) == 0))
			power_off = !invert;
		if ((strncmp(str, "allow-ints", 10) == 0) ||
		    (strncmp(str, "allow_ints", 10) == 0))
 			apm_info.allow_ints = !invert;
		if ((strncmp(str, "broken-psr", 10) == 0) ||
		    (strncmp(str, "broken_psr", 10) == 0))
			apm_info.get_power_status_broken = !invert;
		if ((strncmp(str, "realmode-power-off", 18) == 0) ||
		    (strncmp(str, "realmode_power_off", 18) == 0))
			apm_info.realmode_power_off = !invert;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}

__setup("apm=", apm_setup);
#endif

static struct file_operations apm_bios_fops = {
	owner:		THIS_MODULE,
	read:		do_read,
	poll:		do_poll,
	ioctl:		do_ioctl,
	open:		do_open,
	release:	do_release,
};

static struct miscdevice apm_device = {
	APM_MINOR_DEV,
	"apm_bios",
	&apm_bios_fops
};

/*
 * Just start the APM thread. We do NOT want to do APM BIOS
 * calls from anything but the APM thread, if for no other reason
 * than the fact that we don't trust the APM BIOS. This way,
 * most common APM BIOS problems that lead to protection errors
 * etc will have at least some level of being contained...
 *
 * In short, if something bad happens, at least we have a choice
 * of just killing the apm thread..
 */
static int __init apm_init(void)
{
	struct proc_dir_entry *apm_proc;

	if (apm_info.bios.version == 0) {
		printk(KERN_INFO "apm: BIOS not found.\n");
		return -ENODEV;
	}
	printk(KERN_INFO
		"apm: BIOS version %d.%d Flags 0x%02x (Driver version %s)\n",
		((apm_info.bios.version >> 8) & 0xff),
		(apm_info.bios.version & 0xff),
		apm_info.bios.flags,
		driver_version);
	if ((apm_info.bios.flags & APM_32_BIT_SUPPORT) == 0) {
		printk(KERN_INFO "apm: no 32 bit BIOS support\n");
		return -ENODEV;
	}

	if (allow_ints)
		apm_info.allow_ints = 1;
	if (broken_psr)
		apm_info.get_power_status_broken = 1;
	if (realmode_power_off)
		apm_info.realmode_power_off = 1;
	/* User can override, but default is to trust DMI */
	if (apm_disabled != -1)
		apm_info.disabled = apm_disabled;

	/*
	 * Fix for the Compaq Contura 3/25c which reports BIOS version 0.1
	 * but is reportedly a 1.0 BIOS.
	 */
	if (apm_info.bios.version == 0x001)
		apm_info.bios.version = 0x100;

	/* BIOS < 1.2 doesn't set cseg_16_len */
	if (apm_info.bios.version < 0x102)
		apm_info.bios.cseg_16_len = 0; /* 64k */

	if (debug) {
		printk(KERN_INFO "apm: entry %x:%lx cseg16 %x dseg %x",
			apm_info.bios.cseg, apm_info.bios.offset,
			apm_info.bios.cseg_16, apm_info.bios.dseg);
		if (apm_info.bios.version > 0x100)
			printk(" cseg len %x, dseg len %x",
				apm_info.bios.cseg_len,
				apm_info.bios.dseg_len);
		if (apm_info.bios.version > 0x101)
			printk(" cseg16 len %x", apm_info.bios.cseg_16_len);
		printk("\n");
	}

	if (apm_info.disabled) {
		printk(KERN_NOTICE "apm: disabled on user request.\n");
		return -ENODEV;
	}
	if ((smp_num_cpus > 1) && !power_off) {
		printk(KERN_NOTICE "apm: disabled - APM is not SMP safe.\n");
		return -ENODEV;
	}
	if (PM_IS_ACTIVE()) {
		printk(KERN_NOTICE "apm: overridden by ACPI.\n");
		return -ENODEV;
	}
	pm_active = 1;

	/*
	 * Set up a segment that references the real mode segment 0x40
	 * that extends up to the end of page zero (that we have reserved).
	 * This is for buggy BIOS's that refer to (real mode) segment 0x40
	 * even though they are called in protected mode.
	 */
	set_base(gdt[APM_40 >> 3],
		 __va((unsigned long)0x40 << 4));
	_set_limit((char *)&gdt[APM_40 >> 3], 4095 - (0x40 << 4));

	apm_bios_entry.offset = apm_info.bios.offset;
	apm_bios_entry.segment = APM_CS;
	set_base(gdt[APM_CS >> 3],
		 __va((unsigned long)apm_info.bios.cseg << 4));
	set_base(gdt[APM_CS_16 >> 3],
		 __va((unsigned long)apm_info.bios.cseg_16 << 4));
	set_base(gdt[APM_DS >> 3],
		 __va((unsigned long)apm_info.bios.dseg << 4));
#ifndef APM_RELAX_SEGMENTS
	if (apm_info.bios.version == 0x100) {
#endif
		/* For ASUS motherboard, Award BIOS rev 110 (and others?) */
		_set_limit((char *)&gdt[APM_CS >> 3], 64 * 1024 - 1);
		/* For some unknown machine. */
		_set_limit((char *)&gdt[APM_CS_16 >> 3], 64 * 1024 - 1);
		/* For the DEC Hinote Ultra CT475 (and others?) */
		_set_limit((char *)&gdt[APM_DS >> 3], 64 * 1024 - 1);
#ifndef APM_RELAX_SEGMENTS
	} else {
		_set_limit((char *)&gdt[APM_CS >> 3],
			(apm_info.bios.cseg_len - 1) & 0xffff);
		_set_limit((char *)&gdt[APM_CS_16 >> 3],
			(apm_info.bios.cseg_16_len - 1) & 0xffff);
		_set_limit((char *)&gdt[APM_DS >> 3],
			(apm_info.bios.dseg_len - 1) & 0xffff);
	}
#endif

	apm_proc = create_proc_info_entry("apm", 0, NULL, apm_get_info);
	if (apm_proc)
		SET_MODULE_OWNER(apm_proc);

	kernel_thread(apm, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND | SIGCHLD);

	if (smp_num_cpus > 1) {
		printk(KERN_NOTICE
		   "apm: disabled - APM is not SMP safe (power off active).\n");
		return 0;
	}

	misc_register(&apm_device);

	if (HZ != 100)
		idle_period = (idle_period * HZ) / 100;
	if (idle_threshold < 100) {
		original_pm_idle = pm_idle;
		pm_idle  = apm_cpu_idle;
		set_pm_idle = 1;
	}

	return 0;
}

static void __exit apm_exit(void)
{
	int	error;

	if (set_pm_idle)
		pm_idle = original_pm_idle;
	if (((apm_info.bios.flags & APM_BIOS_DISENGAGED) == 0)
	    && (apm_info.connection_version > 0x0100)) {
		error = apm_engage_power_management(APM_DEVICE_ALL, 0);
		if (error)
			apm_error("disengage power management", error);
	}
	misc_deregister(&apm_device);
	remove_proc_entry("apm", NULL);
	unregister_sysrq_key('o',&sysrq_poweroff_op);
	if (power_off)
		pm_power_off = NULL;
	exit_kapmd = 1;
	while (kapmd_running)
		schedule();
	pm_active = 0;
}

module_init(apm_init);
module_exit(apm_exit);

MODULE_AUTHOR("Stephen Rothwell");
MODULE_DESCRIPTION("Advanced Power Management");
MODULE_LICENSE("GPL");
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Enable debug mode");
MODULE_PARM(power_off, "i");
MODULE_PARM_DESC(power_off, "Enable power off");
MODULE_PARM(bounce_interval, "i");
MODULE_PARM_DESC(bounce_interval,
		"Set the number of ticks to ignore suspend bounces");
MODULE_PARM(allow_ints, "i");
MODULE_PARM_DESC(allow_ints, "Allow interrupts during BIOS calls");
MODULE_PARM(broken_psr, "i");
MODULE_PARM_DESC(broken_psr, "BIOS has a broken GetPowerStatus call");
MODULE_PARM(realmode_power_off, "i");
MODULE_PARM_DESC(realmode_power_off,
		"Switch to real mode before powering off");
MODULE_PARM(idle_threshold, "i");
MODULE_PARM_DESC(idle_threshold,
	"System idle percentage above which to make APM BIOS idle calls");
MODULE_PARM(idle_period, "i");
MODULE_PARM_DESC(idle_period,
	"Period (in sec/100) over which to caculate the idle percentage");

EXPORT_NO_SYMBOLS;
