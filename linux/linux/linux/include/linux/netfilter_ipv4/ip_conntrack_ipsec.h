#ifndef _IP_CONNTRACK_IPSEC_H
#define _IP_CONNTRACK_IPSEC_H

//#include <linux/netfilter_ipv4/lockhelp.h>

/* Protects ipsec part of conntracks */
DECLARE_LOCK_EXTERN(ip_ipsec_lock);


/************************************************************
 *                     ISAKMP MASQ                          *
 *  This has to work with the existing UDP masq, which is   *
 *  why we go through all of the timeout table contortions  *
 *  and so forth...                                         *
 ************************************************************/

/*
 * ISAKMP uses 500/udp, and the traffic must come from
 * 500/udp (i.e. 500/udp <-> 500/udp), so we need to
 * check for ISAKMP UDP traffic and avoid changing the
 * source port number. In order to associate the data streams
 * we need to sniff the ISAKMP cookies as well.
 */
#define UDP_PORT_ISAKMP 500     /* ISAKMP default UDP port */

struct ip_ct_ipsec_isakmp {
	__u64	icookie;	/* initiator cookie */
	__u64	rcookie;	/* responder cookie */
};

/*
 * Split the 64-bit cookie into two 32-bit chunks for display
 */
#ifdef __LITTLE_ENDIAN
#define splitcookie(x)	(long unsigned int) ntohl(((x) & 0xFFFFFFFF)), (long unsigned int) ntohl(((x) >> 32))
#else
#define splitcookie(x)	(long unsigned int) ntohl(((x) >> 32)), (long unsigned int) ntohl(((x) & 0xFFFFFFFF))
#endif

#endif
