/* BNET extension for TCP NAT alteration. */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <linux/if.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_tuple.h>

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_conntrack_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_conntrack_lock)

#include <linux/netfilter_ipv4/listhelp.h>
#include <linux/netfilter_ipv4/lockhelp.h>


DECLARE_LOCK_EXTERN(ip_bnet_lock);

#define DEBUGP(format, args...)

#define BNET_PORT 6112
#define BNET_NEW_PORT_BASE	63000

#define CLIENT_INFO_OFFSET 16
#define CLIENT_INFO_PACKET_MIN_SIZE 80

#define CLIENT_INFO_OFFSET2 4

/*** BNET Starcraft 1.09 & 1.10 ***/
static u8 host_info_head[CLIENT_INFO_OFFSET] = {
        0xff, 0x50, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x38,
        0x58, 0x49, 0x50, 0x58, 0x45, 0x53
}; // host_info_head[2] : 0x32 == windows language dependancy value -> should be skipped

/*** BNET Free BNET Starcraft 1.08 ***/
static u8 host_info_head2[CLIENT_INFO_OFFSET2] = {
        0xff, 0x1E, 0x1A, 0x00
};

extern u32 get_skb_data( struct sk_buff **ppskb, char **ppdata, u32 *plen );

extern unsigned int ip_conntrack_htable_size;
extern struct list_head *ip_conntrack_hash;

unsigned int udp_client_ip = 0;

static int ip_tuple_mask_cmp (
        const struct ip_conntrack_tuple_hash *i,
        const struct ip_conntrack_tuple *tuple,
        const struct ip_conntrack_tuple *mask)
{
        return ip_ct_tuple_mask_cmp(&i->tuple, tuple, mask);
}

unsigned int ipt_target_callback(struct sk_buff **pskb, unsigned int hooknum)
{
        struct ip_conntrack *ct;
        enum ip_conntrack_info ctinfo;
        char *skbdata = NULL, *pdata = NULL;
        u32  skblen = 0, datalen = 0;
        u32  addrpos = 0, addrlen = 0;
        u32  offset, i;
        struct ip_conntrack_tuple ct_tuple, udp_tuple, udp_tuple_mask;
        struct ip_conntrack_tuple_hash *tuple_hash;

        if ((*pskb)->len < CLIENT_INFO_PACKET_MIN_SIZE)
                return NF_ACCEPT;

        get_skb_data(pskb, &skbdata, &skblen);
        ct = ip_conntrack_get(*pskb, &ctinfo);
        if(!ct)
        {
                //printk("ct is NULL\n");
                return NF_ACCEPT;
        }

        pdata = skbdata;
        datalen = skblen;

        offset = 0;
        while ( (offset != CLIENT_INFO_OFFSET) &&
		((host_info_head[offset] == (u8)(*pdata++)) || (offset == 2)) && ++offset);

        if (offset!= CLIENT_INFO_OFFSET)
        {
                pdata = skbdata;
                datalen = skblen;

                offset = 0;
                while ( (offset != CLIENT_INFO_OFFSET2) &&
                        (host_info_head2[offset++] == (u8)(*pdata++)));
                if( offset != CLIENT_INFO_OFFSET2 )
                        return NF_ACCEPT;
        }
        else
        {
                addrpos = (pdata + 8) - skbdata;
                addrlen = sizeof(unsigned int);

                ip_nat_mangle_tcp_packet(pskb, ct, ctinfo, addrpos, addrlen,
                        (char *) &ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip, addrlen);
        }

        ct_tuple.src.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
        ct_tuple.src.u.udp.port = htons(BNET_PORT);
        ct_tuple.dst.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
        ct_tuple.dst.u.udp.port = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.udp.port;
        ct_tuple.dst.protonum = IPPROTO_UDP;
        tuple_hash = ip_conntrack_find_get( &ct_tuple, NULL );

        if( tuple_hash )
        {
                if(del_timer( &tuple_hash->ctrack->timeout ))
                        tuple_hash->ctrack->timeout.function( (unsigned long)tuple_hash->ctrack );
        }

        udp_tuple.src.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
        udp_tuple.src.u.udp.port = 0;
        udp_tuple.dst.ip = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip;
        udp_tuple.dst.u.udp.port = htons(BNET_PORT);
        udp_tuple.dst.protonum = IPPROTO_UDP;
        udp_tuple_mask.src.ip = 0xffffffff;
        udp_tuple_mask.src.u.udp.port = 0;
        udp_tuple_mask.dst.ip = 0xffffffff;
        udp_tuple_mask.dst.u.udp.port = 0xffff;
        udp_tuple_mask.dst.protonum = 0xffff;

        for (i=0; !tuple_hash && i < ip_conntrack_htable_size; i++)
        {
                tuple_hash = LIST_FIND(
                                  &ip_conntrack_hash[i],
                                  ip_tuple_mask_cmp,
                                  struct ip_conntrack_tuple_hash *,
                                  &udp_tuple, &udp_tuple_mask);

        }

        if( tuple_hash )
        {
                //printk("=> Found ip : %08x\n", udp_tuple.dst.ip);
                if(del_timer( &tuple_hash->ctrack->timeout ))
                        tuple_hash->ctrack->timeout.function( (unsigned long)tuple_hash->ctrack );
        }

        udp_client_ip = ntohl(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip);

        (*pskb)->nfmark = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip;
#if	0
        (*pskb)->nfmark2 = htons((udp_client_ip & 0xff) + BNET_NEW_PORT_BASE);
        (*pskb)->nfflag = ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip;
#endif

        return NF_QUEUE;
}

static unsigned int bnet_nat_expected(struct sk_buff **pskb,
		 unsigned int hooknum,
		 struct ip_conntrack *ct,
		 struct ip_nat_info *info)
{
	return 0;
}

static int bnet_nat_mangle(struct ip_conntrack *ct,
                           enum ip_conntrack_info ctinfo,
                           struct sk_buff **pskb)
{
	struct iphdr   *iph = (*pskb)->nh.iph;

#if defined(CONFIG_DRIVERLEVEL_REAL_IPCLONE) || defined(CONFIG_DRIVERLEVEL_REAL_IPCLONE_MODULE)
	if (ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip == htonl(0xc0a8ff02))
	{  /* 192.168.255.2 */
		//printk("--> src : %08x, %08x \n", iph->saddr, htonl(0xc0a8ff02));
		return 0;
	}
	else
#endif
	return ((check_internal_subnet(iph->saddr)) ? 1 : 0);
}


static unsigned int help(struct ip_conntrack *ct,
                         struct ip_conntrack_expect *exp,
			 struct ip_nat_info *info,
			 enum ip_conntrack_info ctinfo,
			 unsigned int hooknum,
			 struct sk_buff **pskb)
{
	struct iphdr   *iph = (*pskb)->nh.iph;
	struct tcphdr  *th = (struct tcphdr *)((u8*)iph + (iph->ihl * 4));
	unsigned int rc;

	if (!bnet_nat_mangle(ct, ctinfo, pskb))
		rc = NF_ACCEPT;
	else if (th->rst)
		rc = NF_QUEUE;
	else if (th->fin && th->ack)
		rc = NF_QUEUE;
	else
		rc = ipt_target_callback(pskb, hooknum);

	return rc;
}


static struct ip_nat_helper bnet_invert =
    { { NULL, NULL },
      "BNET",
      IP_NAT_HELPER_F_ALWAYS | IP_NAT_HELPER_F_STANDALONE,
      THIS_MODULE,
      { { 0, { tcp: {port: __constant_htons(BNET_PORT) }} }, { 0, { 0 }, IPPROTO_TCP } },
      { { 0, { tcp: {port: 0xFFFF } }}, { 0, { 0 }, 0xFFFF } },
      help,
      bnet_nat_expected };

/* Not __exit: called from init() */
static void fini(void)
{
	printk("ip_nat_bnet: unregistering port %d\n", BNET_PORT);
	ip_nat_helper_unregister(&bnet_invert);
}

static int __init init(void)
{
	int ret;

	printk("ip_nat_bnet: Trying to register for port %d\n", BNET_PORT);
	ret = ip_nat_helper_register(&bnet_invert);

	if (ret) {
		DEBUGP("ip_nat_bnet: error registering helper for port %d\n", BNET_PORT);
		fini();
		return ret;
	}

	return ret;
}


module_init(init);
module_exit(fini);
