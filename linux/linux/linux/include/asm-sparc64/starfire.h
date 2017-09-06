/* $Id: starfire.h,v 1.1.1.1 2012/08/29 05:42:24 bcm5357 Exp $
 * starfire.h: Group all starfire specific code together.
 *
 * Copyright (C) 2000 Anton Blanchard (anton@samba.org)
 */

#ifndef _SPARC64_STARFIRE_H
#define _SPARC64_STARFIRE_H

#ifndef __ASSEMBLY__

extern int this_is_starfire;

extern void check_if_starfire(void);
extern void starfire_cpu_setup(void);
extern int starfire_hard_smp_processor_id(void);
extern void *starfire_hookup(int);
extern unsigned int starfire_translate(unsigned long imap, unsigned int upaid);

#endif
#endif
