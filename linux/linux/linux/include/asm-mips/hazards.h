/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle <ralf@linux-mips.org>
 * Copyright (C) MIPS Technologies, Inc.
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef _ASM_HAZARDS_H
#define _ASM_HAZARDS_H


#ifdef __ASSEMBLY__
#define ASMMACRO(name, code...) .macro name; code; .endm
#else

#define ASMMACRO(name, code...)						\
__asm__(".macro " #name "; " #code "; .endm");				\
									\
static inline void name(void)						\
{									\
	__asm__ __volatile__ (#name);					\
}

#endif

ASMMACRO(_ssnop,
	 sll	$0, $0, 1
	)

#define SW_HAZARD_BARRIERS
#ifdef SW_HAZARD_BARRIERS

ASMMACRO(_ehb,
	 sll	$0, $0, 3
	)

#else /* SW_HAZARD_BARRIERS */

ASMMACRO(_ehb,
	 nop
	)

#endif /* SW_HAZARD_BARRIERS */

/*
 * TLB hazards
 */

/*
 * MIPSR2 defines ehb for hazard avoidance
 */

ASMMACRO(mtc0_tlbw_hazard,
	 _ehb
	)
ASMMACRO(tlbw_use_hazard,
	 _ehb
	)
ASMMACRO(tlb_probe_hazard,
	 _ehb
	)
ASMMACRO(irq_enable_hazard,
	 _ehb
	)
ASMMACRO(irq_disable_hazard,
	_ehb
	)
ASMMACRO(back_to_back_c0_hazard,
	 _ehb
	)

#endif /* _ASM_HAZARDS_H */
