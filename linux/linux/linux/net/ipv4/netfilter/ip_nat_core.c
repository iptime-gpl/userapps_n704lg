/* NAT for netfilter; shared with compatibility layer. */

/* (c) 1999 Paul `Rusty' Russell.  Licenced under the GNU General
   Public Licence. */
#ifdef MODULE
#define __NO_VERSION__
#endif
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <linux/brlock.h>
#include <linux/vmalloc.h>
#include <net/checksum.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/tcp.h>  /* For tcp_prot in getorigdst */

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_nat_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_nat_lock)

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_protocol.h>
#include <linux/netfilter_ipv4/ip_nat_core.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/listhelp.h>
#include <linux/netfilter_ipv4/ipt_cone.h>

#ifdef HNDCTF
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <typedefs.h>
#include <osl.h>
#include <ctf/hndctf.h>

#define NFC_CTF_ENABLED	(1 << 31)
#endif /* HNDCTF */

#define DEBUGP(format, args...)

DECLARE_RWLOCK(ip_nat_lock);
DECLARE_RWLOCK_EXTERN(ip_conntrack_lock);

/* Calculated at init based on memory size */
static unsigned int ip_nat_htable_size;

static struct list_head *bysource;
static struct list_head *byipsproto;
LIST_HEAD(protos);
LIST_HEAD(helpers);

extern struct ip_nat_protocol unknown_nat_protocol;

#ifdef HNDCTF
bool
ip_conntrack_is_ipc_allowed(struct sk_buff *skb, u_int32_t hooknum)
{
	struct net_device *dev;

	if (!CTF_ENAB(kcih))
		return FALSE;

	if (hooknum == NF_IP_PRE_ROUTING) {
		dev = skb->dev;
		if (dev->priv_flags & IFF_802_1Q_VLAN)
			dev = VLAN_DEV_INFO(dev)->real_dev;

		/* Add ipc entry if packet is received on ctf enabled interface
		 * and the packet is not a defrag'd one.
		 */
		if (ctf_isenabled(kcih, dev) && (skb->len <= dev->mtu))
			skb->nfcache |= NFC_CTF_ENABLED;
	}

	/* Add the cache entries only if the device has registered and
	 * enabled ctf.
	 */
	if (skb->nfcache & NFC_CTF_ENABLED)
		return TRUE;

	return FALSE;
}

void
ip_conntrack_ipct_add(struct sk_buff *skb, u_int32_t hooknum,
                      struct ip_conntrack *ct, enum ip_conntrack_info ci,
                      struct ip_nat_info_manip *manip)
{
	ctf_ipc_t ipc_entry;
	struct hh_cache *hh;
	struct ethhdr *eth;
	struct iphdr *iph;
	struct tcphdr *tcph;
	struct ip_nat_info *info;
	u_int32_t daddr;
	struct rtable *rt;

	if ((skb == NULL) || (ct == NULL))
		return;

	info = &ct->nat.info;

	/* We only add cache entires for non-helper connections and at
	 * pre or post routing hooks.
	 */
	if (info->helper || (ct->ctf_flags & CTF_FLAGS_EXCLUDED) ||
	    ((hooknum != NF_IP_PRE_ROUTING) && (hooknum != NF_IP_POST_ROUTING)))
		return;

	if (ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.tcp.port != 
	    ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u.tcp.port)
		return;

	/* Add ipc entries for connections in established state only */
	if ((ci != IP_CT_ESTABLISHED) && (ci != (IP_CT_ESTABLISHED+IP_CT_IS_REPLY)))
		return;

	iph = skb->nh.iph;
	if (((iph->protocol != IPPROTO_TCP) ||
	    ((ct->proto.tcp.state >= TCP_CONNTRACK_FIN_WAIT) &&
	    (ct->proto.tcp.state <= TCP_CONNTRACK_LAST_ACK))) &&
	    (iph->protocol != IPPROTO_UDP))
		return;

	/* Do route lookup for alias address if we are doing DNAT in this
	 * direction.
	 */
	daddr = iph->daddr;
	if ((manip != NULL) && (manip->maniptype == IP_NAT_MANIP_DST))
		daddr = manip->manip.ip; 

	memset(&ipc_entry, 0, sizeof(ipc_entry));

	/* Init the neighboring sender address */
	memcpy(ipc_entry.sa.octet, skb->mac.ethernet->h_source, ETH_ALEN);

	/* If the packet is received on a bridge device then save
	 * the bridge cache entry pointer in the ip cache entry.
	 * This will be referenced in the data path to update the
	 * live counter of brc entry whenever a received packet
	 * matches corresponding ipc entry.
	 */
	if ((skb->dev != NULL) && ctf_isbridge(kcih, skb->dev))
		ipc_entry.brcp = ctf_brc_lkup(kcih, skb->mac.ethernet->h_source);

	/* Find the destination interface */
	if ((skb->dst == NULL) &&
	    (ip_route_input(skb, daddr, iph->saddr, iph->tos, skb->dev) == 0))
		skb->dev = skb->dst->dev;

	/* Ensure the packet belongs to a forwarding connection and it is
	 * destined to an unicast address.
	 */
	rt = (struct rtable *)skb->dst;
	if ((rt == NULL) || (rt->u.dst.input != ip_forward) ||
	    (rt->rt_type != RTN_UNICAST) || (rt->u.dst.neighbour == NULL) ||
	    ((rt->u.dst.neighbour->nud_state & (NUD_PERMANENT|NUD_REACHABLE)) == 0))
		return;

	hh = skb->dst->hh;
	if (hh != NULL) {
		eth = (struct ethhdr *)(((unsigned char *)hh->hh_data) + 2);
		memcpy(ipc_entry.dhost.octet, eth->h_dest, ETH_ALEN);
		memcpy(ipc_entry.shost.octet, eth->h_source, ETH_ALEN);
	} else {
		memcpy(ipc_entry.dhost.octet, rt->u.dst.neighbour->ha, ETH_ALEN);
		memcpy(ipc_entry.shost.octet, skb->dst->dev->dev_addr, ETH_ALEN);
	}

	tcph = ((struct tcphdr *)(((__u8 *)iph) + (iph->ihl << 2)));

	/* Add ctf ipc entry for this direction */
	ipc_entry.tuple.sip = iph->saddr;
	ipc_entry.tuple.dip = iph->daddr;
	ipc_entry.tuple.proto = iph->protocol;
	ipc_entry.tuple.sp = tcph->source;
	ipc_entry.tuple.dp = tcph->dest;

	/* For vlan interfaces fill the vlan id and the tag/untag actions */
	if (skb->dst->dev->priv_flags & IFF_802_1Q_VLAN) {
		ipc_entry.txif = (void *)(VLAN_DEV_INFO(skb->dst->dev)->real_dev);
		ipc_entry.vid = VLAN_DEV_INFO(skb->dst->dev)->vlan_id;
		ipc_entry.action = ((VLAN_DEV_INFO(skb->dst->dev)->flags & 1) ?
		                    CTF_ACTION_TAG : CTF_ACTION_UNTAG);
	} else {
		ipc_entry.txif = skb->dst->dev;
		ipc_entry.action = CTF_ACTION_UNTAG;
	}

	/* Update the manip ip and port */
	if (manip != NULL) {
		if (manip->maniptype == IP_NAT_MANIP_SRC) {
			ipc_entry.nat.ip = manip->manip.ip;
			ipc_entry.nat.port = manip->manip.u.tcp.port;
			ipc_entry.action |= CTF_ACTION_SNAT;
		} else {
			ipc_entry.nat.ip = manip->manip.ip;
			ipc_entry.nat.port = manip->manip.u.tcp.port;
			ipc_entry.action |= CTF_ACTION_DNAT;
		}
	}

	/* Do bridge cache lookup to determine outgoing interface
	 * and any vlan tagging actions if needed.
	 */
	if (ctf_isbridge(kcih, ipc_entry.txif)) {
		ctf_brc_t *brcp;

		brcp = ctf_brc_lkup(kcih, ipc_entry.dhost.octet);

		if (brcp == NULL)
			return;
		else {
			ipc_entry.action |= brcp->action;
			ipc_entry.txif = brcp->txifp;
			ipc_entry.vid = brcp->vid;
		}
	}

#ifdef DEBUG
	printk("%s: Adding ipc entry for [%d]%u.%u.%u.%u:%u - %u.%u.%u.%u:%u\n", __FUNCTION__,
			ipc_entry.tuple.proto, 
			NIPQUAD(ipc_entry.tuple.sip), ntohs(ipc_entry.tuple.sp), 
			NIPQUAD(ipc_entry.tuple.dip), ntohs(ipc_entry.tuple.dp)); 
	printk("sa %02x:%02x:%02x:%02x:%02x:%02x\n",
			ipc_entry.shost.octet[0], ipc_entry.shost.octet[1],
			ipc_entry.shost.octet[2], ipc_entry.shost.octet[3],
			ipc_entry.shost.octet[4], ipc_entry.shost.octet[5]);
	printk("da %02x:%02x:%02x:%02x:%02x:%02x\n",
			ipc_entry.dhost.octet[0], ipc_entry.dhost.octet[1],
			ipc_entry.dhost.octet[2], ipc_entry.dhost.octet[3],
			ipc_entry.dhost.octet[4], ipc_entry.dhost.octet[5]);
	printk("[%d] vid: %d action %x\n", hooknum, ipc_entry.vid, ipc_entry.action);
	if (manip != NULL)
		printk("manip_ip: %u.%u.%u.%u manip_port %u\n",
			NIPQUAD(ipc_entry.nat.ip), ntohs(ipc_entry.nat.port));
	printk("txif: %s\n", ((struct net_device *)ipc_entry.txif)->name);
#endif

	ctf_ipc_add(kcih, &ipc_entry);

        /* Update the features flag to indicate a CTF conn */
        ct->ctf_flags |= CTF_FLAGS_CACHED;
}

int
ip_conntrack_ipct_delete(struct ip_conntrack *ct, int ct_timeout)
{
	ctf_ipc_t *ipct;
	struct ip_conntrack_tuple *orig, *repl;
	struct ip_nat_info *info;

	if (!CTF_ENAB(kcih))
		return (0);

	info = &ct->nat.info;

	orig = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;

	if ((orig->dst.protonum != IPPROTO_TCP) && (orig->dst.protonum != IPPROTO_UDP))
		return (0);

	repl = &ct->tuplehash[IP_CT_DIR_REPLY].tuple;

	/* If the refresh counter of ipc entry is non zero, it indicates
	 * that the packet transfer is active and we should not delete
	 * the conntrack entry.
	 */
	if (ct_timeout) {
		ipct = ctf_ipc_lkup(kcih, orig->src.ip, orig->dst.ip,
		                    orig->dst.protonum, orig->src.u.tcp.port,
		                    orig->dst.u.tcp.port);

		/* Postpone the deletion of ct entry if there are frames
		 * flowing in this direction.
		 */
		if ((ipct != NULL) && (ipct->live > 0)) {
			ipct->live = 0;
			ct->timeout.expires = jiffies + ct->expire_jiffies;
			add_timer(&ct->timeout);
			return (-1);
		}

		ipct = ctf_ipc_lkup(kcih, repl->src.ip, repl->dst.ip,
		                    repl->dst.protonum, repl->src.u.tcp.port,
		                    repl->dst.u.tcp.port);

		IP_NF_ASSERT(ipct != NULL);

		if ((ipct != NULL) && (ipct->live > 0)) {
			ipct->live = 0;
			ct->timeout.expires = jiffies + ct->expire_jiffies;
			add_timer(&ct->timeout);
			return (-1);
		}
	}

	/* If there are no packets over this connection for timeout period
	 * delete the entries.
	 */
	ctf_ipc_delete(kcih, orig->src.ip, orig->dst.ip, orig->dst.protonum,
	               orig->src.u.tcp.port, orig->dst.u.tcp.port);

	ctf_ipc_delete(kcih, repl->src.ip, repl->dst.ip, repl->dst.protonum,
	               repl->src.u.tcp.port, repl->dst.u.tcp.port);

#ifdef DEBUG
	printk("%s: Deleting the tuple %x %x %d %d %d\n",
	       __FUNCTION__, orig->src.ip, orig->dst.ip, orig->dst.protonum,
	       orig->src.u.tcp.port, orig->dst.u.tcp.port);
	printk("%s: Deleting the tuple %x %x %d %d %d\n",
	       __FUNCTION__, repl->dst.ip, repl->src.ip, repl->dst.protonum,
	       repl->dst.u.tcp.port, repl->src.u.tcp.port);
#endif

	return (0);
}
#endif /* HNDCTF */

/* We keep extra hashes for each conntrack, for fast searching. */
static inline size_t
hash_by_ipsproto(u_int32_t src, u_int32_t dst, u_int16_t proto)
{
	/* Modified src and dst, to ensure we don't create two
           identical streams. */
	return (src + dst + proto) % ip_nat_htable_size;
}

static inline size_t
hash_by_src(const struct ip_conntrack_manip *manip, u_int16_t proto)
{
	/* Original src, to ensure we map it consistently if poss. */
	return (manip->ip + manip->u.all + proto) % ip_nat_htable_size;
}

/* Noone using conntrack by the time this called. */
static void ip_nat_cleanup_conntrack(struct ip_conntrack *conn)
{
	struct ip_nat_info *info = &conn->nat.info;

	if (!info->initialized)
		return;

	IP_NF_ASSERT(info->bysource.conntrack);
	IP_NF_ASSERT(info->byipsproto.conntrack);

	WRITE_LOCK(&ip_nat_lock);
	LIST_DELETE(&bysource[hash_by_src(&conn->tuplehash[IP_CT_DIR_ORIGINAL]
					  .tuple.src,
					  conn->tuplehash[IP_CT_DIR_ORIGINAL]
					  .tuple.dst.protonum)],
		    &info->bysource);

	LIST_DELETE(&byipsproto
		    [hash_by_ipsproto(conn->tuplehash[IP_CT_DIR_REPLY]
				      .tuple.src.ip,
				      conn->tuplehash[IP_CT_DIR_REPLY]
				      .tuple.dst.ip,
				      conn->tuplehash[IP_CT_DIR_REPLY]
				      .tuple.dst.protonum)],
		    &info->byipsproto);

	/* Detach from the cone conntrack list */
	ipt_cone_cleanup_conntrack(conn);

	WRITE_UNLOCK(&ip_nat_lock);
}

/* We do checksum mangling, so if they were wrong before they're still
 * wrong.  Also works for incomplete packets (eg. ICMP dest
 * unreachables.) */
u_int16_t
ip_nat_cheat_check(u_int32_t oldvalinv, u_int32_t newval, u_int16_t oldcheck)
{
	u_int32_t diffs[] = { oldvalinv, newval };
	return csum_fold(csum_partial((char *)diffs, sizeof(diffs),
				      oldcheck^0xFFFF));
}

static inline int cmp_proto(const struct ip_nat_protocol *i, int proto)
{
	return i->protonum == proto;
}

struct ip_nat_protocol *
find_nat_proto(u_int16_t protonum)
{
	struct ip_nat_protocol *i;

	MUST_BE_READ_LOCKED(&ip_nat_lock);
	i = LIST_FIND(&protos, cmp_proto, struct ip_nat_protocol *, protonum);
	if (!i)
		i = &unknown_nat_protocol;
	return i;
}

/* Is this tuple already taken? (not by us) */
int
ip_nat_used_tuple(const struct ip_conntrack_tuple *tuple,
		  const struct ip_conntrack *ignored_conntrack)
{
	/* Conntrack tracking doesn't keep track of outgoing tuples; only
	   incoming ones.  NAT means they don't have a fixed mapping,
	   so we invert the tuple and look for the incoming reply.

	   We could keep a separate hash if this proves too slow. */
	struct ip_conntrack_tuple reply;

	invert_tuplepr(&reply, tuple);
	return ip_conntrack_tuple_taken(&reply, ignored_conntrack);
}

/* Does tuple + the source manip come within the range mr */
static int
in_range(const struct ip_conntrack_tuple *tuple,
	 const struct ip_conntrack_manip *manip,
	 const struct ip_nat_multi_range *mr)
{
	struct ip_nat_protocol *proto = find_nat_proto(tuple->dst.protonum);
	unsigned int i;
	struct ip_conntrack_tuple newtuple = { *manip, tuple->dst };

	for (i = 0; i < mr->rangesize; i++) {
		/* If we are allowed to map IPs, then we must be in the
		   range specified, otherwise we must be unchanged. */
		if (mr->range[i].flags & IP_NAT_RANGE_MAP_IPS) {
			if (ntohl(newtuple.src.ip) < ntohl(mr->range[i].min_ip)
			    || (ntohl(newtuple.src.ip)
				> ntohl(mr->range[i].max_ip)))
				continue;
		} else {
			if (newtuple.src.ip != tuple->src.ip)
				continue;
		}

		if ((mr->range[i].flags & IP_NAT_RANGE_PROTO_SPECIFIED)
		    && proto->in_range(&newtuple, IP_NAT_MANIP_SRC,
				       &mr->range[i].min, &mr->range[i].max))
			return 1;
	}
	return 0;
}

static inline int
src_cmp(const struct ip_nat_hash *i,
	const struct ip_conntrack_tuple *tuple,
	const struct ip_nat_multi_range *mr)
{
	return (i->conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum
		== tuple->dst.protonum
		&& i->conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip
		== tuple->src.ip
		&& i->conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.all
		== tuple->src.u.all
		&& in_range(tuple,
			    &i->conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			    .tuple.src,
			    mr));
}

/* Only called for SRC manip */
static struct ip_conntrack_manip *
find_appropriate_src(const struct ip_conntrack_tuple *tuple,
		     const struct ip_nat_multi_range *mr)
{
	unsigned int h = hash_by_src(&tuple->src, tuple->dst.protonum);
	struct ip_nat_hash *i;

	MUST_BE_READ_LOCKED(&ip_nat_lock);
	i = LIST_FIND(&bysource[h], src_cmp, struct ip_nat_hash *, tuple, mr);
	if (i)
		return &i->conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src;
	else
		return NULL;
}

#ifdef CONFIG_IP_NF_NAT_LOCAL
/* If it's really a local destination manip, it may need to do a
   source manip too. */
static int
do_extra_mangle(u_int32_t var_ip, u_int32_t *other_ipp)
{
	struct rtable *rt;

	if (ip_route_output(&rt, var_ip, 0, 0, 0) != 0) {
		DEBUGP("do_extra_mangle: Can't get route to %u.%u.%u.%u\n",
		       NIPQUAD(var_ip));
		return 0;
	}

	*other_ipp = rt->rt_src;
	ip_rt_put(rt);
	return 1;
}
#endif

/* Simple way to iterate through all. */
static inline int fake_cmp(const struct ip_nat_hash *i,
			   u_int32_t src, u_int32_t dst, u_int16_t protonum,
			   unsigned int *score,
			   const struct ip_conntrack *conntrack)
{
	/* Compare backwards: we're dealing with OUTGOING tuples, and
           inside the conntrack is the REPLY tuple.  Don't count this
           conntrack. */
	if (i->conntrack != conntrack
	    && i->conntrack->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip == dst
	    && i->conntrack->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip == src
	    && (i->conntrack->tuplehash[IP_CT_DIR_REPLY].tuple.dst.protonum
		== protonum))
		(*score)++;
	return 0;
}

static inline unsigned int
count_maps(u_int32_t src, u_int32_t dst, u_int16_t protonum,
	   const struct ip_conntrack *conntrack)
{
	unsigned int score = 0;

	MUST_BE_READ_LOCKED(&ip_nat_lock);
	LIST_FIND(&byipsproto[hash_by_ipsproto(src, dst, protonum)],
		  fake_cmp, struct ip_nat_hash *, src, dst, protonum, &score,
		  conntrack);

	return score;
}

/* For [FUTURE] fragmentation handling, we want the least-used
   src-ip/dst-ip/proto triple.  Fairness doesn't come into it.  Thus
   if the range specifies 1.2.3.4 ports 10000-10005 and 1.2.3.5 ports
   1-65535, we don't do pro-rata allocation based on ports; we choose
   the ip with the lowest src-ip/dst-ip/proto usage.

   If an allocation then fails (eg. all 6 ports used in the 1.2.3.4
   range), we eliminate that and try again.  This is not the most
   efficient approach, but if you're worried about that, don't hand us
   ranges you don't really have.  */
static struct ip_nat_range *
find_best_ips_proto(struct ip_conntrack_tuple *tuple,
		    const struct ip_nat_multi_range *mr,
		    const struct ip_conntrack *conntrack,
		    unsigned int hooknum)
{
	unsigned int i;
	struct {
		const struct ip_nat_range *range;
		unsigned int score;
		struct ip_conntrack_tuple tuple;
	} best = { NULL,  0xFFFFFFFF };
	u_int32_t *var_ipp, *other_ipp, saved_ip, orig_dstip;
	static unsigned int randomness = 0;

	if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC) {
		var_ipp = &tuple->src.ip;
		saved_ip = tuple->dst.ip;
		other_ipp = &tuple->dst.ip;
	} else {
		var_ipp = &tuple->dst.ip;
		saved_ip = tuple->src.ip;
		other_ipp = &tuple->src.ip;
	}
	/* Don't do do_extra_mangle unless neccessary (overrides
           explicit socket bindings, for example) */
	orig_dstip = tuple->dst.ip;

	IP_NF_ASSERT(mr->rangesize >= 1);
	for (i = 0; i < mr->rangesize; i++) {
		/* Host order */
		u_int32_t minip, maxip, j;

		/* Don't do ranges which are already eliminated. */
		if (mr->range[i].flags & IP_NAT_RANGE_FULL) {
			continue;
		}

		if (mr->range[i].flags & IP_NAT_RANGE_MAP_IPS) {
			minip = ntohl(mr->range[i].min_ip);
			maxip = ntohl(mr->range[i].max_ip);
		} else
			minip = maxip = ntohl(*var_ipp);

		randomness++;
		for (j = 0; j < maxip - minip + 1; j++) {
			unsigned int score;

			*var_ipp = htonl(minip + (randomness + j) 
					 % (maxip - minip + 1));

			/* Reset the other ip in case it was mangled by
			 * do_extra_mangle last time. */
			*other_ipp = saved_ip;

#ifdef CONFIG_IP_NF_NAT_LOCAL
			if (hooknum == NF_IP_LOCAL_OUT
			    && *var_ipp != orig_dstip
			    && !do_extra_mangle(*var_ipp, other_ipp)) {
				DEBUGP("Range %u %u.%u.%u.%u rt failed!\n",
				       i, NIPQUAD(*var_ipp));
				/* Can't route?  This whole range part is
				 * probably screwed, but keep trying
				 * anyway. */
				continue;
			}
#endif

			/* Count how many others map onto this. */
			score = count_maps(tuple->src.ip, tuple->dst.ip,
					   tuple->dst.protonum, conntrack);
			if (score < best.score) {
				/* Optimization: doesn't get any better than
				   this. */
				if (score == 0)
					return (struct ip_nat_range *)
						&mr->range[i];

				best.score = score;
				best.tuple = *tuple;
				best.range = &mr->range[i];
			}
		}
	}
	*tuple = best.tuple;

	/* Discard const. */
	return (struct ip_nat_range *)best.range;
}

/* Fast version doesn't iterate through hash chains, but only handles
   common case of single IP address (null NAT, masquerade) */
static struct ip_nat_range *
find_best_ips_proto_fast(struct ip_conntrack_tuple *tuple,
			 const struct ip_nat_multi_range *mr,
			 const struct ip_conntrack *conntrack,
			 unsigned int hooknum)
{
	if (mr->rangesize != 1
	    || (mr->range[0].flags & IP_NAT_RANGE_FULL)
	    || ((mr->range[0].flags & IP_NAT_RANGE_MAP_IPS)
		&& mr->range[0].min_ip != mr->range[0].max_ip))
		return find_best_ips_proto(tuple, mr, conntrack, hooknum);

	if (mr->range[0].flags & IP_NAT_RANGE_MAP_IPS) {
		if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC)
			tuple->src.ip = mr->range[0].min_ip;
		else {
			/* Only do extra mangle when required (breaks
                           socket binding) */
#ifdef CONFIG_IP_NF_NAT_LOCAL
			if (tuple->dst.ip != mr->range[0].min_ip
			    && hooknum == NF_IP_LOCAL_OUT
			    && !do_extra_mangle(mr->range[0].min_ip,
						&tuple->src.ip))
				return NULL;
#endif
			tuple->dst.ip = mr->range[0].min_ip;
		}
	}

	/* Discard const. */
	return (struct ip_nat_range *)&mr->range[0];
}

static int
get_unique_tuple(struct ip_conntrack_tuple *tuple,
		 const struct ip_conntrack_tuple *orig_tuple,
		 const struct ip_nat_multi_range *mrr,
		 struct ip_conntrack *conntrack,
		 unsigned int hooknum)
{
	struct ip_nat_protocol *proto
		= find_nat_proto(orig_tuple->dst.protonum);
	struct ip_nat_range *rptr;
	unsigned int i;
	int ret;

	/* We temporarily use flags for marking full parts, but we
	   always clean up afterwards */
	struct ip_nat_multi_range *mr = (void *)mrr;

	/* 1) If this srcip/proto/src-proto-part is currently mapped,
	   and that same mapping gives a unique tuple within the given
	   range, use that.

	   This is only required for source (ie. NAT/masq) mappings.
	   So far, we don't do local source mappings, so multiple
	   manips not an issue.  */
	if (hooknum == NF_IP_POST_ROUTING) {
		struct ip_conntrack_manip *manip;

		manip = find_appropriate_src(orig_tuple, mr);
		if (manip) {
			/* Apply same source manipulation. */
			*tuple = ((struct ip_conntrack_tuple)
				  { *manip, orig_tuple->dst });
			DEBUGP("get_unique_tuple: Found current src map\n");
			return 1;
		}
	}

	/* 2) Select the least-used IP/proto combination in the given
	   range.
	*/
	*tuple = *orig_tuple;
	while ((rptr = find_best_ips_proto_fast(tuple, mr, conntrack, hooknum))
	       != NULL) {
		DEBUGP("Found best for "); DUMP_TUPLE_RAW(tuple);
		/* 3) The per-protocol part of the manip is made to
		   map into the range to make a unique tuple. */

#ifdef CONFIG_IP_NF_NAT_PORT_RESTRICTED_CONE
                if ((orig_tuple->dst.protonum == IPPROTO_UDP || orig_tuple->dst.protonum == IPPROTO_TCP) &&
                    (orig_tuple->src.u.all == htons(6112) || orig_tuple->dst.u.all == htons(6112)))
                        conntrack->port_restricted_cone = PORT_RESTRICTED_CONE_SKIP;

                if ((HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC) &&
                    (conntrack->port_restricted_cone != PORT_RESTRICTED_CONE_SKIP) &&
                    (orig_tuple->dst.protonum == IPPROTO_UDP || orig_tuple->dst.protonum == IPPROTO_TCP) &&
                    ip_ct_port_restricted_cone_nat_tuple(tuple, conntrack))
                {
                        ret = 1;
                        goto clear_fulls;
                } else
#endif

		/* Only bother mapping if it's not already in range
		   and unique */
		if ((!(rptr->flags & IP_NAT_RANGE_PROTO_SPECIFIED)
		     || proto->in_range(tuple, HOOK2MANIP(hooknum),
					&rptr->min, &rptr->max))
		    && !ip_nat_used_tuple(tuple, conntrack)) {
			ret = 1;
			goto clear_fulls;
		} else {
			if (proto->unique_tuple(tuple, rptr,
						HOOK2MANIP(hooknum),
						conntrack)) {
				/* Must be unique. */
				IP_NF_ASSERT(!ip_nat_used_tuple(tuple,
								conntrack));
				ret = 1;
				goto clear_fulls;
			} else if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_DST) {
				/* Try implicit source NAT; protocol
                                   may be able to play with ports to
                                   make it unique. */
				struct ip_nat_range r
					= { IP_NAT_RANGE_MAP_IPS, 
					    tuple->src.ip, tuple->src.ip,
					    { 0 }, { 0 } };
				DEBUGP("Trying implicit mapping\n");
				if (proto->unique_tuple(tuple, &r,
							IP_NAT_MANIP_SRC,
							conntrack)) {
					/* Must be unique. */
					IP_NF_ASSERT(!ip_nat_used_tuple
						     (tuple, conntrack));
					ret = 1;
					goto clear_fulls;
				}
			}
			DEBUGP("Protocol can't get unique tuple %u.\n",
			       hooknum);
		}

		/* Eliminate that from range, and try again. */
		rptr->flags |= IP_NAT_RANGE_FULL;
		*tuple = *orig_tuple;
	}

	ret = 0;

 clear_fulls:
	/* Clear full flags. */
	IP_NF_ASSERT(mr->rangesize >= 1);
	for (i = 0; i < mr->rangesize; i++)
		mr->range[i].flags &= ~IP_NAT_RANGE_FULL;

	return ret;
}

static inline int
helper_cmp(const struct ip_nat_helper *helper,
	   const struct ip_conntrack_tuple *tuple)
{
	return ip_ct_tuple_mask_cmp(tuple, &helper->tuple, &helper->mask);
}

/* Where to manip the reply packets (will be reverse manip). */
static unsigned int opposite_hook[NF_IP_NUMHOOKS]
= { [NF_IP_PRE_ROUTING] = NF_IP_POST_ROUTING,
    [NF_IP_POST_ROUTING] = NF_IP_PRE_ROUTING,
#ifdef CONFIG_IP_NF_NAT_LOCAL
    [NF_IP_LOCAL_OUT] = NF_IP_LOCAL_IN,
    [NF_IP_LOCAL_IN] = NF_IP_LOCAL_OUT,
#endif
};

unsigned int
ip_nat_setup_info(struct ip_conntrack *conntrack,
		  const struct ip_nat_multi_range *mr,
		  unsigned int hooknum)
{
	struct ip_conntrack_tuple new_tuple, inv_tuple, reply;
	struct ip_conntrack_tuple orig_tp;
	struct ip_nat_info *info = &conntrack->nat.info;

	MUST_BE_WRITE_LOCKED(&ip_nat_lock);
	IP_NF_ASSERT(hooknum == NF_IP_PRE_ROUTING
		     || hooknum == NF_IP_POST_ROUTING
		     || hooknum == NF_IP_LOCAL_OUT);
	IP_NF_ASSERT(info->num_manips < IP_NAT_MAX_MANIPS);

	/* What we've got will look like inverse of reply. Normally
	   this is what is in the conntrack, except for prior
	   manipulations (future optimization: if num_manips == 0,
	   orig_tp =
	   conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple) */
	invert_tuplepr(&orig_tp,
		       &conntrack->tuplehash[IP_CT_DIR_REPLY].tuple);


	do {
		if (!get_unique_tuple(&new_tuple, &orig_tp, mr, conntrack,
				      hooknum)) {
			DEBUGP("ip_nat_setup_info: Can't get unique for %p.\n",
			       conntrack);
			return NF_DROP;
		}


		/* We now have two tuples (SRCIP/SRCPT/DSTIP/DSTPT):
		   the original (A/B/C/D') and the mangled one (E/F/G/H').

		   We're only allowed to work with the SRC per-proto
		   part, so we create inverses of both to start, then
		   derive the other fields we need.  */

		/* Reply connection: simply invert the new tuple
                   (G/H/E/F') */
		invert_tuplepr(&reply, &new_tuple);

		/* Alter conntrack table so it recognizes replies.
                   If fail this race (reply tuple now used), repeat. */
	} while (!ip_conntrack_alter_reply(conntrack, &reply));

	/* Create inverse of original: C/D/A/B' */
	invert_tuplepr(&inv_tuple, &orig_tp);

	/* Has source changed?. */
	if (!ip_ct_tuple_src_equal(&new_tuple, &orig_tp)) {
		/* In this direction, a source manip. */
		info->manips[info->num_manips++] =
			((struct ip_nat_info_manip)
			 { IP_CT_DIR_ORIGINAL, hooknum,
			   IP_NAT_MANIP_SRC, new_tuple.src });

		IP_NF_ASSERT(info->num_manips < IP_NAT_MAX_MANIPS);

		/* In the reverse direction, a destination manip. */
		info->manips[info->num_manips++] =
			((struct ip_nat_info_manip)
			 { IP_CT_DIR_REPLY, opposite_hook[hooknum],
			   IP_NAT_MANIP_DST, orig_tp.src });
		IP_NF_ASSERT(info->num_manips <= IP_NAT_MAX_MANIPS);
	}

	/* Has destination changed? */
	if (!ip_ct_tuple_dst_equal(&new_tuple, &orig_tp)) {
		/* In this direction, a destination manip */
		info->manips[info->num_manips++] =
			((struct ip_nat_info_manip)
			 { IP_CT_DIR_ORIGINAL, hooknum,
			   IP_NAT_MANIP_DST, reply.src });

		IP_NF_ASSERT(info->num_manips < IP_NAT_MAX_MANIPS);

		/* In the reverse direction, a source manip. */
		info->manips[info->num_manips++] =
			((struct ip_nat_info_manip)
			 { IP_CT_DIR_REPLY, opposite_hook[hooknum],
			   IP_NAT_MANIP_SRC, inv_tuple.src });
		IP_NF_ASSERT(info->num_manips <= IP_NAT_MAX_MANIPS);
	}

	/* If there's a helper, assign it; based on new tuple. */
	if (!conntrack->master)
		info->helper = LIST_FIND(&helpers, helper_cmp, struct ip_nat_helper *,
					 &reply);

	/* It's done. */
	info->initialized |= (1 << HOOK2MANIP(hooknum));
	return NF_ACCEPT;
}

void replace_in_hashes(struct ip_conntrack *conntrack,
		       struct ip_nat_info *info)
{
	/* Source has changed, so replace in hashes. */
	unsigned int srchash
		= hash_by_src(&conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.src,
			      conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.dst.protonum);
	/* We place packet as seen OUTGOUNG in byips_proto hash
           (ie. reverse dst and src of reply packet. */
	unsigned int ipsprotohash
		= hash_by_ipsproto(conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.dst.ip,
				   conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.src.ip,
				   conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.dst.protonum);

	IP_NF_ASSERT(info->bysource.conntrack == conntrack);
	MUST_BE_WRITE_LOCKED(&ip_nat_lock);

	list_del(&info->bysource.list);
	list_del(&info->byipsproto.list);

	list_prepend(&bysource[srchash], &info->bysource);
	list_prepend(&byipsproto[ipsprotohash], &info->byipsproto);

	/* Attach to the cone conntrack list */
	ipt_cone_replace_in_hashes(conntrack);
}

void place_in_hashes(struct ip_conntrack *conntrack,
		     struct ip_nat_info *info)
{
	unsigned int srchash
		= hash_by_src(&conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.src,
			      conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.dst.protonum);
	/* We place packet as seen OUTGOUNG in byips_proto hash
           (ie. reverse dst and src of reply packet. */
	unsigned int ipsprotohash
		= hash_by_ipsproto(conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.dst.ip,
				   conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.src.ip,
				   conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.dst.protonum);

	IP_NF_ASSERT(!info->bysource.conntrack);

	MUST_BE_WRITE_LOCKED(&ip_nat_lock);
	info->byipsproto.conntrack = conntrack;
	info->bysource.conntrack = conntrack;

	list_prepend(&bysource[srchash], &info->bysource);
	list_prepend(&byipsproto[ipsprotohash], &info->byipsproto);
}

static void
manip_pkt(u_int16_t proto, struct iphdr *iph, size_t len,
	  const struct ip_conntrack_manip *manip,
	  enum ip_nat_manip_type maniptype,
	  __u32 *nfcache)
{
	*nfcache |= NFC_ALTERED;
	find_nat_proto(proto)->manip_pkt(iph, len, manip, maniptype);

	if (maniptype == IP_NAT_MANIP_SRC) {
		iph->check = ip_nat_cheat_check(~iph->saddr, manip->ip,
						iph->check);
		iph->saddr = manip->ip;
	} else {
		iph->check = ip_nat_cheat_check(~iph->daddr, manip->ip,
						iph->check);
		iph->daddr = manip->ip;
	}
}

static inline int exp_for_packet(struct ip_conntrack_expect *exp,
			         struct sk_buff **pskb)
{
	struct ip_conntrack_protocol *proto;
	int ret = 1;

	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	proto = __ip_ct_find_proto((*pskb)->nh.iph->protocol);
	if (proto->exp_matches_pkt)
		ret = proto->exp_matches_pkt(exp, pskb);

	return ret;
}

/* Do packet manipulations according to binding. */
unsigned int
do_bindings(struct ip_conntrack *ct,
	    enum ip_conntrack_info ctinfo,
	    struct ip_nat_info *info,
	    unsigned int hooknum,
	    struct sk_buff **pskb)
{
	unsigned int i;
	struct ip_nat_helper *helper;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int is_tcp = (*pskb)->nh.iph->protocol == IPPROTO_TCP;
#ifdef HNDCTF
	bool enabled = ip_conntrack_is_ipc_allowed(*pskb, hooknum);

#ifdef CONFIG_EFM_PATCH
	if (enabled) 
	{
	    // EFM, ysyoo, 2012-08-30, for Haripin connection
	    if ((check_internal_subnet(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip)) &&
	        (check_internal_subnet(ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip)))
		enabled =  FALSE;
#if defined(CONFIG_DRIVERLEVEL_REAL_IPCLONE) || defined(CONFIG_DRIVERLEVEL_REAL_IPCLONE_MODULE)
            else if ((ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip & 0x00ffffff) == htonl(0xc0a8ff00) ||
                     (ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip & 0x00ffffff) == htonl(0xc0a8ff00)) 
		enabled =  FALSE;
#endif
	}
#endif
#endif
	/* Need nat lock to protect against modification, but neither
	   conntrack (referenced) and helper (deleted with
	   synchronize_bh()) can vanish. */
	READ_LOCK(&ip_nat_lock);
	for (i = 0; i < info->num_manips; i++) {
		/* raw socket (tcpdump) may have clone of incoming
                   skb: don't disturb it --RR */
		if (skb_cloned(*pskb) && !(*pskb)->sk) {
			struct sk_buff *nskb = skb_copy(*pskb, GFP_ATOMIC);
			if (!nskb) {
				READ_UNLOCK(&ip_nat_lock);
				return NF_DROP;
			}
			kfree_skb(*pskb);
			*pskb = nskb;
		}

		if (info->manips[i].direction == dir
		    && info->manips[i].hooknum == hooknum) {
			DEBUGP("Mangling %p: %s to %u.%u.%u.%u %u\n",
			       *pskb,
			       info->manips[i].maniptype == IP_NAT_MANIP_SRC
			       ? "SRC" : "DST",
			       NIPQUAD(info->manips[i].manip.ip),
			       htons(info->manips[i].manip.u.all));
#ifdef HNDCTF
			/* Add ipc entry for this direction */
			if (enabled)
				ip_conntrack_ipct_add(*pskb, hooknum, ct,
				                      ctinfo, &info->manips[i]);
#endif /* HNDCTF */
			manip_pkt((*pskb)->nh.iph->protocol,
				  (*pskb)->nh.iph,
				  (*pskb)->len,
				  &info->manips[i].manip,
				  info->manips[i].maniptype,
				  &(*pskb)->nfcache);
		}
	}
	helper = info->helper;
	READ_UNLOCK(&ip_nat_lock);

#if defined(CONFIG_DRIVERLEVEL_REAL_IPCLONE) || defined(CONFIG_DRIVERLEVEL_REAL_IPCLONE_MODULE)
        if (((ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip & 0x00ffffff) != htonl(0xc0a8ff00)) &&
            ((ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip & 0x00ffffff) != htonl(0xc0a8ff00)) &&
            helper)
#else
	if (helper) 
#endif
	{
		struct ip_conntrack_expect *exp = NULL;
		struct list_head *cur_item;
		int ret = NF_ACCEPT;

		DEBUGP("do_bindings: helper existing for (%p)\n", ct);

		/* Always defragged for helpers */
		IP_NF_ASSERT(!((*pskb)->nh.iph->frag_off
			       & htons(IP_MF|IP_OFFSET)));

		/* Have to grab read lock before sibling_list traversal */
		READ_LOCK(&ip_conntrack_lock);
		list_for_each(cur_item, &ct->sibling_list) { 
			exp = list_entry(cur_item, struct ip_conntrack_expect, 
					 expected_list);
					 
			/* if this expectation is already established, skip */
			if (exp->sibling)
				continue;

			if (exp_for_packet(exp, pskb)) {
				DEBUGP("calling nat helper (exp=%p) for packet\n",
					exp);
				ret = helper->help(ct, exp, info, ctinfo, 
						   hooknum, pskb);
				if (ret != NF_ACCEPT) {
					READ_UNLOCK(&ip_conntrack_lock);
					return ret;
				}
			}
		}
		/* Helper might want to manip the packet even when there is no expectation */
		if (!exp && helper->flags & IP_NAT_HELPER_F_ALWAYS) {
			DEBUGP("calling nat helper for packet without expectation\n");
			ret = helper->help(ct, NULL, info, ctinfo, 
					   hooknum, pskb);
			if (ret != NF_ACCEPT) {
				READ_UNLOCK(&ip_conntrack_lock);
				return ret;
			}
		}
		READ_UNLOCK(&ip_conntrack_lock);
		
		/* Adjust sequence number only once per packet 
		 * (helper is called at all hooks) */
		if (is_tcp && (hooknum == NF_IP_POST_ROUTING
			       || hooknum == NF_IP_LOCAL_IN)) {
			DEBUGP("ip_nat_core: adjusting sequence number\n");
			/* future: put this in a l4-proto specific function,
			 * and call this function here. */
			ip_nat_seq_adjust(*pskb, ct, ctinfo);
		}

		return ret;

	} else {
#ifdef HNDCTF
#endif /* HNDCTF */
		return NF_ACCEPT;
	}

	/* not reached */
}

unsigned int
icmp_reply_translation(struct sk_buff *skb,
		       struct ip_conntrack *conntrack,
		       unsigned int hooknum,
		       int dir)
{
	struct iphdr *iph = skb->nh.iph;
	struct icmphdr *hdr = (struct icmphdr *)((u_int32_t *)iph + iph->ihl);
	struct iphdr *inner = (struct iphdr *)(hdr + 1);
	size_t datalen = skb->len - ((void *)inner - (void *)iph);
	unsigned int i;
	struct ip_nat_info *info = &conntrack->nat.info;

	IP_NF_ASSERT(skb->len >= iph->ihl*4 + sizeof(struct icmphdr));
	/* Must be RELATED */
	IP_NF_ASSERT(skb->nfct - (struct ip_conntrack *)skb->nfct->master
		     == IP_CT_RELATED
		     || skb->nfct - (struct ip_conntrack *)skb->nfct->master
		     == IP_CT_RELATED+IP_CT_IS_REPLY);

	/* Redirects on non-null nats must be dropped, else they'll
           start talking to each other without our translation, and be
           confused... --RR */
	if (hdr->type == ICMP_REDIRECT) {
		/* Don't care about races here. */
		if (info->initialized
		    != ((1 << IP_NAT_MANIP_SRC) | (1 << IP_NAT_MANIP_DST))
		    || info->num_manips != 0)
			return NF_DROP;
	}

	DEBUGP("icmp_reply_translation: translating error %p hook %u dir %s\n",
	       skb, hooknum, dir == IP_CT_DIR_ORIGINAL ? "ORIG" : "REPLY");
	/* Note: May not be from a NAT'd host, but probably safest to
	   do translation always as if it came from the host itself
	   (even though a "host unreachable" coming from the host
	   itself is a bit weird).

	   More explanation: some people use NAT for anonymizing.
	   Also, CERT recommends dropping all packets from private IP
	   addresses (although ICMP errors from internal links with
	   such addresses are not too uncommon, as Alan Cox points
	   out) */

	READ_LOCK(&ip_nat_lock);
	for (i = 0; i < info->num_manips; i++) {
		DEBUGP("icmp_reply: manip %u dir %s hook %u\n",
		       i, info->manips[i].direction == IP_CT_DIR_ORIGINAL ?
		       "ORIG" : "REPLY", info->manips[i].hooknum);

		if (info->manips[i].direction != dir)
			continue;

		/* Mapping the inner packet is just like a normal
		   packet, except it was never src/dst reversed, so
		   where we would normally apply a dst manip, we apply
		   a src, and vice versa. */
		if (info->manips[i].hooknum == opposite_hook[hooknum]) {
			DEBUGP("icmp_reply: inner %s -> %u.%u.%u.%u %u\n",
			       info->manips[i].maniptype == IP_NAT_MANIP_SRC
			       ? "DST" : "SRC",
			       NIPQUAD(info->manips[i].manip.ip),
			       ntohs(info->manips[i].manip.u.udp.port));
			manip_pkt(inner->protocol, inner,
				  skb->len - ((void *)inner - (void *)iph),
				  &info->manips[i].manip,
				  !info->manips[i].maniptype,
				  &skb->nfcache);
		/* Outer packet needs to have IP header NATed like
                   it's a reply. */
		} else if (info->manips[i].hooknum == hooknum) {
			/* Use mapping to map outer packet: 0 give no
                           per-proto mapping */
			DEBUGP("icmp_reply: outer %s -> %u.%u.%u.%u\n",
			       info->manips[i].maniptype == IP_NAT_MANIP_SRC
			       ? "SRC" : "DST",
			       NIPQUAD(info->manips[i].manip.ip));
			manip_pkt(0, iph, skb->len,
				  &info->manips[i].manip,
				  info->manips[i].maniptype,
				  &skb->nfcache);
		}
	}
	READ_UNLOCK(&ip_nat_lock);

	/* Since we mangled inside ICMP packet, recalculate its
	   checksum from scratch.  (Hence the handling of incorrect
	   checksums in conntrack, so we don't accidentally fix one.)  */
	hdr->checksum = 0;
	hdr->checksum = ip_compute_csum((unsigned char *)hdr,
					sizeof(*hdr) + datalen);

	return NF_ACCEPT;
}

int __init ip_nat_init(void)
{
	size_t i;

	/* Leave them the same for the moment. */
	ip_nat_htable_size = ip_conntrack_htable_size;

	/* One vmalloc for both hash tables */
	bysource = vmalloc(sizeof(struct list_head) * ip_nat_htable_size*2);
	if (!bysource) {
		return -ENOMEM;
	}
	byipsproto = bysource + ip_nat_htable_size;

	/* Sew in builtin protocols. */
	WRITE_LOCK(&ip_nat_lock);
	list_append(&protos, &ip_nat_protocol_tcp);
	list_append(&protos, &ip_nat_protocol_udp);
	list_append(&protos, &ip_nat_protocol_icmp);
	WRITE_UNLOCK(&ip_nat_lock);

	for (i = 0; i < ip_nat_htable_size; i++) {
		INIT_LIST_HEAD(&bysource[i]);
		INIT_LIST_HEAD(&byipsproto[i]);
	}

	IP_NF_ASSERT(ip_conntrack_destroyed == NULL);
	ip_conntrack_destroyed = &ip_nat_cleanup_conntrack;

	return 0;
}

/* Clear NAT section of all conntracks, in case we're loaded again. */
static int clean_nat(const struct ip_conntrack *i, void *data)
{
	memset((void *)&i->nat, 0, sizeof(i->nat));
	return 0;
}

/* Not __exit: called from ip_nat_standalone.c:init_or_cleanup() --RR */
void ip_nat_cleanup(void)
{
	ip_ct_selective_cleanup(&clean_nat, NULL);
	ip_conntrack_destroyed = NULL;
	vfree(bysource);
}
