#include <linux/module.h>
#include <linux/ip.h>
#include <linux/tcp.h>

#include <linux/netfilter_ipv4/ip_tables.h>
//#include <linux/netfilter_ipv4/ip_conntrack.h>
//#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_tuple.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ipt_HAIRPIN.h>

/* This rwlock protects the main hash table, protocol/helper/expected
 *    registrations, conntrack timers*/
#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_conntrack_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_conntrack_lock)

#include <linux/netfilter_ipv4/listhelp.h>
#include <linux/netfilter_ipv4/lockhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

#define PRINT_TUPLE(tp)                                          \
	DEBUGP("tuple %p: %u %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",       \
	(tp), (tp)->dst.protonum,                                \
	NIPQUAD((tp)->src.ip), ntohs((tp)->src.u.tcp.port),              \
	NIPQUAD((tp)->dst.ip), ntohs((tp)->dst.u.tcp.port))


extern unsigned int ip_conntrack_htable_size;
extern struct list_head *ip_conntrack_hash;


static int ip_tuple_mask_cmp (
	const struct ip_conntrack_tuple_hash *i,
	const struct ip_conntrack_tuple *tuple,
	const struct ip_conntrack_tuple *mask)
{
	return ip_ct_tuple_mask_cmp(&i->tuple, tuple, mask);
}

#undef printk

static unsigned int
hairpin_out(struct sk_buff **pskb,
		unsigned int hooknum,
		const struct net_device *in,
		const struct net_device *out,
		const void *targinfo,
		void *userinfo)
{
    const struct ipt_hairpin_info *info = targinfo;
    struct ip_conntrack *ct = NULL;
    enum ip_conntrack_info ctinfo;
    struct ip_nat_multi_range newrange;
    struct iphdr *iph = (*pskb)->nh.iph;
    int rc;

    IP_NF_ASSERT(hooknum == NF_IP_POST_ROUTING);
    DEBUGP("############# %s ############\n", __FUNCTION__);

    if (check_local_ip(iph->saddr)) 
	    return IPT_CONTINUE;

    ct = ip_conntrack_get(*pskb, &ctinfo);
    IP_NF_ASSERT(ct && (ctinfo == IP_CT_NEW));

    PRINT_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
    PRINT_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

    /* Alter the destination of imcoming packet. */
    newrange = ((struct ip_nat_multi_range)
	    { 1, { { IP_NAT_RANGE_MAP_IPS,
	             info->ip,
	             info->ip,
	             { ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port },
	             { ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port },
	           } } });

    /* Hand modified range to generic setup. */
    rc = ip_nat_setup_info(ct, &newrange, hooknum);

    DEBUGP("*--- After ip_nat_setup()\n");
    PRINT_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
    PRINT_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

    return rc;
}


static unsigned int
hairpin_in(struct sk_buff **pskb,
		unsigned int hooknum,
		const struct net_device *in,
		const struct net_device *out,
		const void *targinfo,
		void *userinfo)
{
    struct ip_conntrack *ct = NULL, *found_ct = NULL;
    struct ip_conntrack_tuple tuple_mask;
    struct ip_conntrack_tuple_hash *tuple_hash = NULL;
    enum ip_conntrack_info ctinfo;
    struct ip_nat_multi_range newrange;
    int i, rc;

    IP_NF_ASSERT(hooknum == NF_IP_PRE_ROUTING);
    DEBUGP("############# %s ############\n", __FUNCTION__);

    ct = ip_conntrack_get(*pskb, &ctinfo);
    IP_NF_ASSERT(ct && (ctinfo == IP_CT_NEW));

    PRINT_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
    PRINT_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

    tuple_mask.src.ip = 0x0;
    tuple_mask.src.u.udp.port = 0;
    tuple_mask.dst.ip = 0xffffffff;
    tuple_mask.dst.u.udp.port = 0xffff;

    for (i=0; !tuple_hash && i < ip_conntrack_htable_size; i++)
    {
	    tuple_hash = LIST_FIND(
			    &ip_conntrack_hash[i],
		            ip_tuple_mask_cmp,
			    struct ip_conntrack_tuple_hash *,
			    &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple, &tuple_mask);		    

	    if (tuple_hash && 
		ip_ct_tuple_mask_cmp(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple, 
			&tuple_hash->ctrack->tuplehash[IP_CT_DIR_REPLY].tuple, 
			&tuple_mask))
	    {
		    if (tuple_hash->ctrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip == 
			ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip)
			    continue;
		    found_ct = tuple_hash->ctrack;
    		    PRINT_TUPLE(&found_ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
    		    PRINT_TUPLE(&found_ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	    }
    }

    if (!found_ct) 
	    return IPT_CONTINUE;

    /* Alter the destination of imcoming packet. */
    newrange = ((struct ip_nat_multi_range)
	    { 1, { { (IP_NAT_RANGE_PROTO_SPECIFIED | IP_NAT_RANGE_MAP_IPS),
	             found_ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip,
	             found_ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip,
	             { found_ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port },
	             { found_ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port },
	           } } });

    /* Hand modified range to generic setup. */
    rc = ip_nat_setup_info(ct, &newrange, hooknum);

    DEBUGP("*--- After ip_nat_setup()\n");
    PRINT_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
    PRINT_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

    return rc;	
}

static unsigned int
hairpin_target(struct sk_buff **pskb,
		unsigned int hooknum,
		const struct net_device *in,
		const struct net_device *out,
		const void *targinfo,
		void *userinfo)
{
    const struct ipt_hairpin_info *info = targinfo;
    const struct iphdr *iph = (*pskb)->nh.iph;

    DEBUGP("%s: type = %s\n", __FUNCTION__, (info->dir == IPT_HAIRPIN_IN) ? "in" : "out"); 

    /* The Port-hairpin only supports TCP and UDP. */
    if ((iph->protocol != IPPROTO_TCP) && (iph->protocol != IPPROTO_UDP))
	return IPT_CONTINUE;

    if (info->dir == IPT_HAIRPIN_OUT)
	return hairpin_out(pskb, hooknum, in, out, targinfo, userinfo);
    else if (info->dir == IPT_HAIRPIN_IN)
	return hairpin_in(pskb, hooknum, in, out, targinfo, userinfo);

    return IPT_CONTINUE;
}

static int
hairpin_check(const char *tablename,
	       const struct ipt_entry *e,
	       void *targinfo,
	       unsigned int targinfosize,
	       unsigned int hook_mask)
{
	const struct ipt_hairpin_info *info = targinfo;

	if ((strcmp(tablename, "nat") != 0)) {
		DEBUGP("hairpin_check: bad table `%s'.\n", tablename);
		return 0;
	}
	if (targinfosize != IPT_ALIGN(sizeof(*info))) {
		DEBUGP("hairpin_check: size %u.\n", targinfosize);
		return 0;
	}
	return 1;
}

static struct ipt_target hairpin_reg =
 { { NULL, NULL }, "HAIRPIN", hairpin_target, hairpin_check, NULL, THIS_MODULE };

static int __init init(void)
{
	if (ipt_register_target(&hairpin_reg))
	{
		printk("=========> ipt_HAIRPIN registre ERROR \n");
		return -EINVAL;
	}

	printk("ipt_HAIRPIN register.\n");
	return 0;
}

static void __exit fini(void)
{
	ipt_unregister_target(&hairpin_reg);
}

module_init(init);
module_exit(fini);
