#include <linux/types.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/netfilter.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>

#ifdef CONFIG_EFM_PATCH
unsigned long ip_ct_udp_timeout = 30*HZ;
unsigned long ip_ct_udp_timeout_stream = 300*HZ;
#else
#define UDP_TIMEOUT (90*HZ)
#define UDP_STREAM_TIMEOUT (180*HZ)
#endif


#ifdef CONFIG_NETFILTER_SORIBADA_PATCH
/* ysyoo, 2002.11.11 for soribada */
#define UDP_TIMEOUT_5SEC (5*HZ)
#define UDP_TIMEOUT_1SEC (1*HZ)
#define SORIBADA_UDP_PORT  22321
#define SORIBADA_SEARCH_PORT 7674
#define REALP2P_UDP_PORT 5522
#define SIP_UDP_PORT 5060
#define WYZ070_SMS_UDP_PORT 5080

#define UDP_APP_MAX 10
static unsigned int udp_app_port[UDP_APP_MAX] = { 0,0,0,0,0,0,0,0,0,0 };
static udp_app_port_count = 0;

void udp_app_timeout_set(unsigned int app_port)
{
        //printk("==> udp_app_timeout_set : %d\n", app_port);
        udp_app_port[udp_app_port_count++] = htons(app_port); /* network order saving */
        if (udp_app_port_count == UDP_APP_MAX)
                udp_app_port_count = 0;
}

static unsigned int this_udp_is_app_timeout(unsigned int app_port)
{
        int i;

        for (i=0; i<UDP_APP_MAX; i++)
        {
                if (udp_app_port[i] == app_port)
                        return 1;
        }

        return 0;
}

#endif


static int udp_pkt_to_tuple(const void *datah, size_t datalen,
			    struct ip_conntrack_tuple *tuple)
{
	const struct udphdr *hdr = datah;

	tuple->src.u.udp.port = hdr->source;
	tuple->dst.u.udp.port = hdr->dest;

	return 1;
}

static int udp_invert_tuple(struct ip_conntrack_tuple *tuple,
			    const struct ip_conntrack_tuple *orig)
{
	tuple->src.u.udp.port = orig->dst.u.udp.port;
	tuple->dst.u.udp.port = orig->src.u.udp.port;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static unsigned int udp_print_tuple(char *buffer,
				    const struct ip_conntrack_tuple *tuple)
{
	return sprintf(buffer, "sport=%hu dport=%hu ",
		       ntohs(tuple->src.u.udp.port),
		       ntohs(tuple->dst.u.udp.port));
}

/* Print out the private part of the conntrack. */
static unsigned int udp_print_conntrack(char *buffer,
					const struct ip_conntrack *conntrack)
{
	return 0;
}

/* Returns verdict for packet, and may modify conntracktype */
static int udp_packet(struct ip_conntrack *conntrack,
		      struct iphdr *iph, size_t len,
		      enum ip_conntrack_info conntrackinfo)
{
#ifdef CONFIG_NETFILTER_SORIBADA_PATCH
        /* ysyoo, 2002.11.11 for soribada */
        if ( (conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all == htons(SORIBADA_UDP_PORT) &&
              conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.all == htons(SORIBADA_UDP_PORT)) ||

             (conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all == htons(SORIBADA_SEARCH_PORT) &&
              conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.all == htons(SORIBADA_SEARCH_PORT))
           )
        {
                ip_ct_refresh(conntrack, UDP_TIMEOUT_1SEC*2);
        }
        else if (conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all == htons(REALP2P_UDP_PORT))
        {
                ip_ct_refresh(conntrack, UDP_TIMEOUT_1SEC*2);
        }
        else if (conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all == htons(67))
        {
                ip_ct_refresh(conntrack, UDP_TIMEOUT_1SEC);
        }
        else if (this_udp_is_app_timeout(conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all) ||
                 this_udp_is_app_timeout(conntrack->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u.all))
        {
                ip_ct_refresh(conntrack, UDP_TIMEOUT_1SEC * 2);
        }
        /* Samsung Wzy070 request, udp_timeout is 600 */
        else if ( (conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all == htons(SIP_UDP_PORT)) ||
                (conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all == htons(WYZ070_SMS_UDP_PORT)) )
        {
                ip_ct_refresh(conntrack, UDP_TIMEOUT_1SEC * 600);
        }
        else
#endif
	/* If we've seen traffic both ways, this is some kind of UDP
	   stream.  Extend timeout. */
	if (conntrack->status & IPS_SEEN_REPLY) {
#ifdef CONFIG_IP_NF_IPSEC
                if (conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all == htons(UDP_PORT_ISAKMP))
                        ip_ct_refresh(conntrack, UDP_TIMEOUT_1SEC * 15);
                else
#endif
#ifdef CONFIG_EFM_PATCH
		ip_ct_refresh(conntrack, ip_ct_udp_timeout_stream);
#else
		ip_ct_refresh(conntrack, UDP_STREAM_TIMEOUT);
#endif
		/* Also, more likely to be important, and not a probe */
		set_bit(IPS_ASSURED_BIT, &conntrack->status);
	} else
#ifdef CONFIG_EFM_PATCH
		ip_ct_refresh(conntrack,ip_ct_udp_timeout);
#else
		ip_ct_refresh(conntrack, UDP_TIMEOUT);
#endif

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int udp_new(struct ip_conntrack *conntrack,
			     struct iphdr *iph, size_t len)
{
	return 1;
}

struct ip_conntrack_protocol ip_conntrack_protocol_udp
= { { NULL, NULL }, IPPROTO_UDP, "udp",
    udp_pkt_to_tuple, udp_invert_tuple, udp_print_tuple, udp_print_conntrack,
    udp_packet, udp_new, NULL, NULL, NULL };
