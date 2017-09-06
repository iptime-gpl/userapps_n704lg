/*
 * Kernel module to match cone target.
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
 * $Id: ipt_cone.c,v 1.1.1.1 2012/08/29 05:42:22 bcm5357 Exp $
 */
#include <linux/module.h>

#include <linux/config.h>
#include <linux/cache.h>
#include <linux/skbuff.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <net/ip.h>

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_nat_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_nat_lock)

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/listhelp.h>

#define DEBUGP(format, args...)

DECLARE_RWLOCK_EXTERN(ip_nat_lock);


/* Calculated at init based on memory size */
static unsigned int ipt_cone_htable_size;
static struct list_head *bycone;

static inline size_t
hash_by_cone(u_int32_t ip, u_int16_t port, u_int16_t protonum)
{
	/* Original src, to ensure we map it consistently if poss. */
	return (ip + port + protonum) % ipt_cone_htable_size;
}

static inline int
cone_cmp(const struct ip_nat_hash *i,
	const struct ip_conntrack_tuple *tuple)
{
	return (i->conntrack->tuplehash[IP_CT_DIR_REPLY].tuple.dst.protonum
		== tuple->dst.protonum &&
		i->conntrack->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip
		== tuple->dst.ip &&
		i->conntrack->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u.udp.port
		== tuple->dst.u.udp.port);
}

/* Only called for SRC manip */
static struct ip_conntrack *
find_appropriate_cone(const struct ip_conntrack_tuple *tuple)
{
	struct ip_nat_hash *i;
	unsigned int h = hash_by_cone(tuple->dst.ip,
		tuple->dst.u.udp.port, tuple->dst.protonum);

	MUST_BE_READ_LOCKED(&ip_nat_lock);
	i = LIST_FIND(&bycone[h], cone_cmp, struct ip_nat_hash *, tuple);
	if (i)
		return i->conntrack;
	else
		return NULL;
}

void
ipt_cone_replace_in_hashes(struct ip_conntrack *conntrack)
{
	struct ip_nat_info *info = &conntrack->nat.info;

	if ((info->nat_type & NFC_IP_CONE_NAT)) {
		struct ip_conntrack_tuple *tuple =
			&conntrack->tuplehash[IP_CT_DIR_REPLY].tuple;

		if (!find_appropriate_cone(tuple)) {
			unsigned int conehash = hash_by_cone(
				tuple->dst.ip, tuple->dst.u.udp.port, tuple->dst.protonum);

			info->bycone.conntrack = conntrack;
			list_prepend(&bycone[conehash], &info->bycone);
		}
	}
}

void
ipt_cone_cleanup_conntrack(struct ip_conntrack *conntrack)
{
	struct ip_nat_info *info = &conntrack->nat.info;

	if (info->bycone.list.next)
		list_del(&info->bycone.list);
}

unsigned int
ipt_cone_target(struct sk_buff **pskb,
	unsigned int hooknum,
	const struct net_device *in,
	const struct net_device *out,
	const void *targinfo,
	void *userinfo)
{
	const struct ip_nat_multi_range *mr = targinfo;
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	struct ip_conntrack_tuple *tuple;
	struct ip_conntrack *cone;
	u_int32_t newdst;
	u_int16_t newport;
	struct ip_nat_multi_range newrange;

	/* Care about only new created one */
	ct = ip_conntrack_get(*pskb, &ctinfo);
	if (ct == 0 || (ctinfo != IP_CT_NEW && ctinfo != IP_CT_RELATED))
		return IPT_CONTINUE;

	/* As a matter of fact, CONE NAT should only apply on IPPROTO_UDP */
	tuple = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	if (tuple->dst.protonum != IPPROTO_UDP)
		return IPT_CONTINUE;

	/* Handle forwarding from WAN to LAN */
	if (hooknum == NF_IP_FORWARD) {
		if (ct->nat.info.nat_type & NFC_IP_CONE_NAT_ALTERED)
			return NF_ACCEPT;
		else
			return IPT_CONTINUE;
	}

	/* Make sure it is pre routing */
	if (hooknum != NF_IP_PRE_ROUTING)
		return IPT_CONTINUE;

	/* Get cone dst */
	cone = find_appropriate_cone(tuple);
	if (cone == NULL)
		return IPT_CONTINUE;

	/* Mark it's a CONE_NAT_TYPE, so NF_IP_FORWARD can accept it */
	ct->nat.info.nat_type |= NFC_IP_CONE_NAT_ALTERED;

	/* Setup new dst ip and port */
	newdst = cone->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
	newport = cone->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port;

	/* Transfer from original range. */
	newrange = ((struct ip_nat_multi_range)
		{ 1, { { mr->range[0].flags | IP_NAT_RANGE_MAP_IPS,
		newdst, newdst,
		{newport}, {newport} } } });

	/* Hand modified range to generic setup. */
	return ip_nat_setup_info(ct, &newrange, hooknum);
}

/* Init/cleanup */
static int __init init(void)
{
	int i;

	ipt_cone_htable_size = ip_conntrack_htable_size;

	bycone = vmalloc(sizeof(struct list_head) * ipt_cone_htable_size);
	if (!bycone) {
		return -ENOMEM;
	}

	for (i = 0; i < ipt_cone_htable_size; i++)
		INIT_LIST_HEAD(&bycone[i]);

	return 0;
}

static void __exit fini(void)
{
	vfree(bycone);
}

module_init(init);
module_exit(fini);
