/*
 * squashfs with lzma compression
 *
 * Copyright (C) 2010, Broadcom Corporation. All Rights Reserved.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id: sqlzma.c,v 1.1.1.1 2012/08/29 05:42:25 bcm5357 Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>

#include "LzmaDec.h"
#include "LzmaDec.c"

static void *SzAlloc(void *p, size_t size) { p = p; return vmalloc(size); }
static void SzFree(void *p, void *address) { p = p; vfree(address); }
static ISzAlloc g_Alloc = { SzAlloc, SzFree };

int LzmaUncompress(void *dst, int dstlen, void *src, int srclen)
{
	int res;
	SizeT inSizePure;
	ELzmaStatus status;
	SizeT outSize;

	if (srclen < LZMA_PROPS_SIZE)
	{
		memcpy(dst, src, srclen);
		return srclen;
	}
	inSizePure = srclen - LZMA_PROPS_SIZE;
	outSize = dstlen;
	res = LzmaDecode(dst, &outSize, src + LZMA_PROPS_SIZE, &inSizePure,
	                 src, LZMA_PROPS_SIZE, LZMA_FINISH_ANY, &status, &g_Alloc);
	srclen = inSizePure ;

	if ((res == SZ_OK) ||
		((res == SZ_ERROR_INPUT_EOF) && (srclen == inSizePure)))
		res = 0;
	return outSize;
}
