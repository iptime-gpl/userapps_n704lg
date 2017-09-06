/* $Id: xtalkaddrs.h,v 1.1.1.1 2012/08/29 05:42:25 bcm5357 Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2001 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_XTALK_XTALKADDRS_H
#define _ASM_SN_XTALK_XTALKADDRS_H


/* Hub-specific xtalk definitions */

#define HX_MEM_BIT		0L	/* Hub's idea of xtalk memory access */
#define HX_IO_BIT		1L	/* Hub's idea of xtalk register access */
#define HX_ACCTYPE_SHIFT	47

#define HX_NODE_SHIFT		39

#define HX_BIGWIN_SHIFT		28
#define HX_SWIN_SHIFT		23

#define HX_LOCACC		0L	/* local access */
#define HX_REMACC		1L	/* remote access */
#define HX_ACCESS_SHIFT		23

/*
 * Pre-calculate the fixed portion of a crosstalk address that maps
 * to local register space on a hub.
 */
#define HX_REG_BASE		((HX_IO_BIT<<HX_ACCTYPE_SHIFT) + \
				(0L<<HX_BIGWIN_SHIFT) + \
				(1L<<HX_SWIN_SHIFT) + IALIAS_SIZE + \
				(HX_REMACC<<HX_ACCESS_SHIFT))

/* 
 * Return a crosstalk address which a widget can use to access a
 * designated register on a designated node.
 */
#define HUBREG_AS_XTALKADDR(nasid, regaddr) \
	((iopaddr_t)(HX_REG_BASE + (((long)nasid)<<HX_NODE_SHIFT) + ((long)regaddr)))

#if TBD
#assert sizeof(iopaddr_t) == 8
#endif /* TBD */

#define XWIDGET_ID_READ(nasid, widget) \
        (widgetreg_t)(*(volatile uint32_t *)(NODE_SWIN_BASE(nasid, widget) + WIDGET_ID))


#endif /* _ASM_SN_XTALK_XTALKADDRS_H */
