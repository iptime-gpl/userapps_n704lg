#define __NO_VERSION__
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/string.h>

#include <net/tcp.h>

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/lockhelp.h>

#ifdef HNDCTF
#include <ctf/hndctf.h>
extern int ip_conntrack_ipct_delete(struct nf_conn *ct, int ct_timeout);
#endif /* HNDCTF */

#define DEBUGP(format, args...)

/* Protects conntrack->proto.tcp */
static DECLARE_RWLOCK(tcp_lock);


/* Actually, I believe that neither ipmasq (where this code is stolen
   from) nor ipfilter do it exactly right.  A new conntrack machine taking
   into account packet loss (which creates uncertainty as to exactly
   the conntrack of the connection) is required.  RSN.  --RR */

static const char *tcp_conntrack_names[] = {
	"NONE",
	"ESTABLISHED",
	"SYN_SENT",
	"SYN_RECV",
	"FIN_WAIT",
	"TIME_WAIT",
	"CLOSE",
	"CLOSE_WAIT",
	"LAST_ACK",
	"LISTEN"
};


#ifdef CONFIG_EFM_PATCH

#define SECS *HZ
#define MINS * 60 SECS
#define HOURS * 60 MINS
#define DAYS * 24 HOURS

#if     0

#define TCP_TIMEOOUT_OPTIMIZATION
static unsigned long tcp_timeouts[]
= { 30 MINS,    /*      TCP_CONNTRACK_NONE,     */
#ifdef TCP_TIMEOOUT_OPTIMIZATION
    1 DAYS,     /*      TCP_CONNTRACK_ESTABLISHED,      */
#else
    5 DAYS,     /*      TCP_CONNTRACK_ESTABLISHED,      */
#endif
#ifdef TCP_TIMEOOUT_OPTIMIZATION
    5 SECS,     /*      TCP_CONNTRACK_SYN_SENT, */
#else
    2 MINS,     /*      TCP_CONNTRACK_SYN_SENT, */
#endif
    60 SECS,    /*      TCP_CONNTRACK_SYN_RECV, */
    2 MINS,     /*      TCP_CONNTRACK_FIN_WAIT, */
#ifdef TCP_TIMEOOUT_OPTIMIZATION
    3 SECS,     /*      TCP_CONNTRACK_TIME_WAIT,        */
#else
    2 MINS,     /*      TCP_CONNTRACK_TIME_WAIT,        */
#endif
    10 SECS,    /*      TCP_CONNTRACK_CLOSE,    */
    60 SECS,    /*      TCP_CONNTRACK_CLOSE_WAIT,       */
    30 SECS,    /*      TCP_CONNTRACK_LAST_ACK, */
    2 MINS,     /*      TCP_CONNTRACK_LISTEN,   */
};
#else
#define TCP_TIMEOOUT_OPTIMIZATION

unsigned long ip_ct_tcp_timeout_syn_recv =     60 SECS;
unsigned long ip_ct_tcp_timeout_fin_wait =      2 MINS;
unsigned long ip_ct_tcp_timeout_close_wait =   60 SECS;
unsigned long ip_ct_tcp_timeout_last_ack =     30 SECS;
unsigned long ip_ct_tcp_timeout_close =        10 SECS;
#ifdef TCP_TIMEOOUT_OPTIMIZATION
unsigned long ip_ct_tcp_timeout_established =   1 DAYS;
unsigned long ip_ct_tcp_timeout_syn_sent =      5 SECS;
unsigned long ip_ct_tcp_timeout_time_wait =     3 SECS;
#else
unsigned long ip_ct_tcp_timeout_established =   5 DAYS;
unsigned long ip_ct_tcp_timeout_syn_sent =      2 MINS;
unsigned long ip_ct_tcp_timeout_time_wait =     2 MINS;
#endif

static unsigned long * tcp_timeouts[]
= { 0,                                 /*      TCP_CONNTRACK_NONE */
    &ip_ct_tcp_timeout_established,    /*      TCP_CONNTRACK_ESTABLISHED,      */
    &ip_ct_tcp_timeout_syn_sent,       /*      TCP_CONNTRACK_SYN_SENT, */
    &ip_ct_tcp_timeout_syn_recv,       /*      TCP_CONNTRACK_SYN_RECV, */
    &ip_ct_tcp_timeout_fin_wait,       /*      TCP_CONNTRACK_FIN_WAIT, */
    &ip_ct_tcp_timeout_time_wait,      /*      TCP_CONNTRACK_TIME_WAIT,        */
    &ip_ct_tcp_timeout_close,          /*      TCP_CONNTRACK_CLOSE,    */
    &ip_ct_tcp_timeout_close_wait,     /*      TCP_CONNTRACK_CLOSE_WAIT,       */
    &ip_ct_tcp_timeout_last_ack,       /*      TCP_CONNTRACK_LAST_ACK, */
    0,                                 /*      TCP_CONNTRACK_LISTEN */
 };
#endif

#endif


#define sNO TCP_CONNTRACK_NONE
#define sES TCP_CONNTRACK_ESTABLISHED
#define sSS TCP_CONNTRACK_SYN_SENT
#define sSR TCP_CONNTRACK_SYN_RECV
#define sFW TCP_CONNTRACK_FIN_WAIT
#define sTW TCP_CONNTRACK_TIME_WAIT
#define sCL TCP_CONNTRACK_CLOSE
#define sCW TCP_CONNTRACK_CLOSE_WAIT
#define sLA TCP_CONNTRACK_LAST_ACK
#define sLI TCP_CONNTRACK_LISTEN
#define sIV TCP_CONNTRACK_MAX

static enum tcp_conntrack tcp_conntracks[2][5][TCP_CONNTRACK_MAX] = {
	{
/*	ORIGINAL */
/* 	  sNO, sES, sSS, sSR, sFW, sTW, sCL, sCW, sLA, sLI 	*/
/*syn*/	{sSS, sES, sSS, sSR, sSS, sSS, sSS, sSS, sSS, sLI },
/*fin*/	{sTW, sFW, sSS, sTW, sFW, sTW, sCL, sTW, sLA, sLI },
/*ack*/	{sES, sES, sSS, sES, sFW, sTW, sCL, sCW, sLA, sES },
/*rst*/ {sCL, sCL, sSS, sCL, sCL, sTW, sCL, sCL, sCL, sCL },
/*none*/{sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV }
	},
	{
/*	REPLY */
/* 	  sNO, sES, sSS, sSR, sFW, sTW, sCL, sCW, sLA, sLI 	*/
/*syn*/	{sSR, sES, sSR, sSR, sSR, sSR, sSR, sSR, sSR, sSR },
/*fin*/	{sCL, sCW, sSS, sTW, sTW, sTW, sCL, sCW, sLA, sLI },
/*ack*/	{sCL, sES, sSS, sSR, sFW, sTW, sCL, sCW, sCL, sLI },
/*rst*/ {sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sLA, sLI },
/*none*/{sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV }
	}
};

static int tcp_pkt_to_tuple(const void *datah, size_t datalen,
			    struct ip_conntrack_tuple *tuple)
{
	const struct tcphdr *hdr = datah;

	tuple->src.u.tcp.port = hdr->source;
	tuple->dst.u.tcp.port = hdr->dest;

	return 1;
}

static int tcp_invert_tuple(struct ip_conntrack_tuple *tuple,
			    const struct ip_conntrack_tuple *orig)
{
	tuple->src.u.tcp.port = orig->dst.u.tcp.port;
	tuple->dst.u.tcp.port = orig->src.u.tcp.port;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static unsigned int tcp_print_tuple(char *buffer,
				    const struct ip_conntrack_tuple *tuple)
{
	return sprintf(buffer, "sport=%hu dport=%hu ",
		       ntohs(tuple->src.u.tcp.port),
		       ntohs(tuple->dst.u.tcp.port));
}

/* Print out the private part of the conntrack. */
static unsigned int tcp_print_conntrack(char *buffer,
					const struct ip_conntrack *conntrack)
{
	enum tcp_conntrack state;

	READ_LOCK(&tcp_lock);
	state = conntrack->proto.tcp.state;
	READ_UNLOCK(&tcp_lock);

	return sprintf(buffer, "%s ", tcp_conntrack_names[state]);
}

static unsigned int get_conntrack_index(const struct tcphdr *tcph)
{
	if (tcph->rst) return 3;
	else if (tcph->syn) return 0;
	else if (tcph->fin) return 1;
	else if (tcph->ack) return 2;
	else return 4;
}

/* Returns verdict for packet, or -1 for invalid. */
static int tcp_packet(struct ip_conntrack *conntrack,
		      struct iphdr *iph, size_t len,
		      enum ip_conntrack_info ctinfo)
{
	enum tcp_conntrack newconntrack, oldtcpstate;
	struct tcphdr *tcph = (struct tcphdr *)((u_int32_t *)iph + iph->ihl);

	/* We're guaranteed to have the base header, but maybe not the
           options. */
	if (len < (iph->ihl + tcph->doff) * 4) {
		DEBUGP("ip_conntrack_tcp: Truncated packet.\n");
		return -1;
	}

	WRITE_LOCK(&tcp_lock);
	oldtcpstate = conntrack->proto.tcp.state;
	newconntrack
		= tcp_conntracks
		[CTINFO2DIR(ctinfo)]
		[get_conntrack_index(tcph)][oldtcpstate];

	/* Invalid */
	if (newconntrack == TCP_CONNTRACK_MAX) {
		DEBUGP("ip_conntrack_tcp: Invalid dir=%i index=%u conntrack=%u\n",
		       CTINFO2DIR(ctinfo), get_conntrack_index(tcph),
		       conntrack->proto.tcp.state);
		WRITE_UNLOCK(&tcp_lock);
		return -1;
	}

	conntrack->proto.tcp.state = newconntrack;

	/* Poor man's window tracking: record SYN/ACK for handshake check */
	if (oldtcpstate == TCP_CONNTRACK_SYN_SENT
	    && CTINFO2DIR(ctinfo) == IP_CT_DIR_REPLY
	    && tcph->syn && tcph->ack)
		conntrack->proto.tcp.handshake_ack
			= htonl(ntohl(tcph->seq) + 1);

	/* If only reply is a RST, we can consider ourselves not to
	   have an established connection: this is a fairly common
	   problem case, so we can delete the conntrack
	   immediately.  --RR */
	if (!(conntrack->status & IPS_SEEN_REPLY) && tcph->rst) {
		WRITE_UNLOCK(&tcp_lock);
		if (del_timer(&conntrack->timeout))
			conntrack->timeout.function((unsigned long)conntrack);
	} else {
		/* Set ASSURED if we see see valid ack in ESTABLISHED after SYN_RECV */
		if (oldtcpstate == TCP_CONNTRACK_SYN_RECV
		    && CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL
		    && tcph->ack && !tcph->syn
		    && tcph->ack_seq == conntrack->proto.tcp.handshake_ack)
			set_bit(IPS_ASSURED_BIT, &conntrack->status);

		WRITE_UNLOCK(&tcp_lock);
#ifdef CONFIG_EFM_PATCH
		ip_ct_refresh(conntrack, *tcp_timeouts[newconntrack]);
#else
		ip_ct_refresh(conntrack, 
			sysctl_ip_conntrack_tcp_timeouts[newconntrack]);
#endif
	}

#ifdef HNDCTF
	/* Remove the ipc entries on receipt of FIN or RST */
	if (CTF_ENAB(kcih) && (conntrack->ctf_flags & CTF_FLAGS_CACHED) &&
	    (tcph->fin || tcph->rst))
		ip_conntrack_ipct_delete(conntrack, 0);
#endif /* HNDCTF */

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int tcp_new(struct ip_conntrack *conntrack,
		   struct iphdr *iph, size_t len)
{
	enum tcp_conntrack newconntrack;
	struct tcphdr *tcph = (struct tcphdr *)((u_int32_t *)iph + iph->ihl);

	/* Don't need lock here: this conntrack not in circulation yet */
	newconntrack
		= tcp_conntracks[0][get_conntrack_index(tcph)]
		[TCP_CONNTRACK_NONE];

	/* Invalid: delete conntrack */
	if (newconntrack == TCP_CONNTRACK_MAX) {
		DEBUGP("ip_conntrack_tcp: invalid new deleting.\n");
		return 0;
	}

	conntrack->proto.tcp.state = newconntrack;
	return 1;
}

static int tcp_exp_matches_pkt(struct ip_conntrack_expect *exp,
			       struct sk_buff **pskb)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct tcphdr *tcph = (struct tcphdr *)((u_int32_t *)iph + iph->ihl);
	unsigned int datalen;

	datalen = (*pskb)->len - iph->ihl*4 - tcph->doff*4;

	return between(exp->seq, ntohl(tcph->seq), ntohl(tcph->seq) + datalen);
}

struct ip_conntrack_protocol ip_conntrack_protocol_tcp
= { { NULL, NULL }, IPPROTO_TCP, "tcp",
    tcp_pkt_to_tuple, tcp_invert_tuple, tcp_print_tuple, tcp_print_conntrack,
    tcp_packet, tcp_new, NULL, tcp_exp_matches_pkt, NULL };
