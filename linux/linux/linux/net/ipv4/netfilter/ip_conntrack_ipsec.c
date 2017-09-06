#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>

#include <linux/netfilter_ipv4/lockhelp.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>

#define CONFIG_DEBUG_IPSEC

#define ISAKMP_UDP_KEEP_TIME    180*HZ

DECLARE_LOCK(ip_ipsec_lock);
struct module *ip_conntrack_ipsec = THIS_MODULE;
static int help(const struct iphdr *iph, size_t len,
		struct ip_conntrack *ct, enum ip_conntrack_info ctinfo)
{
	struct udphdr *udph = (void *)iph + iph->ihl * 4;
	__u64  *dptr, icookie, rcookie;

	if (ctinfo != IP_CT_IS_REPLY)
		return NF_ACCEPT;

	dptr = (__u64 *)((unsigned char *)udph + sizeof(struct udphdr));
	icookie = dptr[0];
	rcookie = dptr[1];

#ifdef CONFIG_DEBUG_IPSEC
	printk(KERN_DEBUG "ip_masq_out_create_isakmp(): ");
	printk("%d.%d.%d.%d -> ", NIPQUAD(iph->saddr));
	printk("%d.%d.%d.%d ", NIPQUAD(iph->daddr));
	printk("i%08lX%08lX r%08lX%08lX\n", splitcookie(icookie), splitcookie(rcookie));
#endif /* CONFIG_IP_MASQ_DEBUG_IPSEC */

	ct->help.ct_ipsec_info.icookie = icookie;
	ct->help.ct_ipsec_info.rcookie = rcookie;
	ip_ct_refresh(ct, ISAKMP_UDP_KEEP_TIME);

	return NF_ACCEPT;
}

static struct ip_conntrack_helper ipsec_out =
	{ { NULL, NULL },
	"IPSEC",
	IP_CT_HELPER_F_REUSE_EXPECT,
	THIS_MODULE,
	16,
	300,
	{ { 0, { __constant_htons(UDP_PORT_ISAKMP) } },
	{ 0, { 0 }, IPPROTO_UDP } },
	{ { 0, { 0xFFFF } },
	{ 0, { 0 }, 0xFFFF } },
	help }
;

static int __init init(void)
{
	int err;

	printk("IPSEC netfilter connection tracking: ");
	if((err = ip_conntrack_helper_register(&ipsec_out)))
		printk("register failed!\n");
	else
		printk("registered\n");

	return 0;
}

static void fini(void)
{
	ip_conntrack_helper_unregister(&ipsec_out);
}

EXPORT_SYMBOL(ip_ipsec_lock);
EXPORT_SYMBOL(ip_conntrack_ipsec);

module_init(init);
module_exit(fini);


