#ifndef _IP_NAT_IPSEC_H
#define _IP_NAT_IPSEC_H

/* Protects ipsec part of conntracks */
DECLARE_LOCK_EXTERN(ip_ipsec_lock);

struct ip_nat_ipsec_esp_info {
	__u32	ospi;
	__u32	ispi;
	int	blocking;
	int	ocnt;
	int	squelched;
};

#endif
