/*
 * Inlcude file for match cone target.
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
 * $Id: ipt_cone.h,v 1.1.1.1 2012/08/29 05:42:24 bcm5357 Exp $
 */

#ifndef	IPT_CONE_H
#define IPT_CONE_H

struct ip_conntrack *find_appropriate_cone(const struct ip_conntrack_tuple *tuple);
void ipt_cone_replace_in_hashes(struct ip_conntrack *conntrack);
void ipt_cone_cleanup_conntrack(struct ip_conntrack *conntrack);
unsigned int ipt_cone_target(struct sk_buff **pskb, unsigned int hooknum,
	const struct net_device *in, const struct net_device *out,
	const void *targinfo, void *userinfo);

#endif	/* IPT_CONE_H */
