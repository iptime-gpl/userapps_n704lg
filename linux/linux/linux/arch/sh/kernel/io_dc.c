/*
 *	$Id: io_dc.c,v 1.1.1.1 2012/08/29 05:42:23 bcm5357 Exp $
 *	I/O routines for SEGA Dreamcast
 */

#include <asm/io.h>
#include <asm/machvec.h>

unsigned long dreamcast_isa_port2addr(unsigned long offset)
{
	return offset + 0xa0000000;
}
