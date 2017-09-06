/* Shared library add-on to iptables to add STAT target support. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#include <iptables.h>
#include <linux/netfilter_ipv4/ip_tables.h>

/* Function which prints out usage message. */
static void
help(void)
{
	printf("MSMVOICE \n");
}

static struct option opts[] = {
	{ 0 }
};

/* Initialize the target. */
static void
init(struct ipt_entry_target *t, unsigned int *nfcache)
{
}

/* Function which parses command options; returns true if it
   ate an option */
static int
parse(int c, char **argv, int invert, unsigned int *flags,
      const struct ipt_entry *entry,
      struct ipt_entry_target **target)
{
	return 1;
}

static void
final_check(unsigned int flags)
{
}

/* Prints out the targinfo. */
static void
print(const struct ipt_ip *ip,
      const struct ipt_entry_target *target,
      int numeric)
{
	printf("MSMVOICE");
}

/* Saves the union ipt_targinfo in parsable form to stdout. */
static void
save(const struct ipt_ip *ip, const struct ipt_entry_target *target)
{
}

struct iptables_target msmvoice
= { NULL,
    "MSMVOICE",
    IPTABLES_VERSION,
    0, 
    0,
    &help,
    &init,
    &parse,
    &final_check,
    &print,
    &save,
    opts
};

#ifdef SHARED_LIBRARY_NOT_SUPPORT
void msmvoice_init(void)
{
#ifdef GLOBAL_INIT_PROBLEM
	extern int _code_base;

	msmvoice.version += _code_base;
	msmvoice.help = &help;
	msmvoice.init = &init;
	msmvoice.parse = &parse;
	msmvoice.final_check = &final_check;
	msmvoice.print = &print;
	msmvoice.save = &save;
	msmvoice.extra_opts = NULL;
#endif

	register_target(&msmvoice);
}
#else
void _init(void)
{
	register_target(&msmvoice);
}
#endif
