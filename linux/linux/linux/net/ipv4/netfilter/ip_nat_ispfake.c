#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <linux/if.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_tuple.h>

#undef printk

extern int check_internal_subnet(u32 ipaddr);

static char new_path[32]="/?index";

/* Return 1 for match, 0 for accept, -1 for partial. */
static int find_pattern(const char *data, size_t dlen,
        const char *pattern, size_t plen,
        char term,
        unsigned int *numoff,
        unsigned int *numlen)
{
    size_t i, j, k;
    int state = 0;
    *numoff = *numlen = 0;

    if (dlen == 0)
        return 0;

    if (dlen <= plen) { /* Short packet: try for partial? */
        if (strnicmp(data, pattern, dlen) == 0)
            return -1;
        else
            return 0;
    }
    for (i = 0; i <= (dlen - plen); i++) {
        /* DFA : \r\n\r\n :: 1234 */
        if (*(data + i) == '\r') {
            if (!(state % 2)) state++;  /* forwarding move */
            else state = 0;             /* reset */
        }
        else if (*(data + i) == '\n') {
            if (state % 2) state++;
            else state = 0;
        }
        else state = 0;

        if (state >= 4)
            break;

        /* pattern compare */
        if (memcmp(data + i, pattern, plen ) != 0)
            continue;

        /* Here, it means patten match!! */
        *numoff=i + plen;
        for (j = *numoff, k = 0; data[j] != term; j++, k++)
            if (j > dlen) return -1 ;   /* no terminal char */

        *numlen = k;
        return 1;
    }
    return 0;
}

#if defined(CONFIG_IP_NF_NAT_ISPFAKE) || defined(CONFIG_IP_NF_NAT_ISPFAKE_MODULE)
extern int sysctl_tcp_mss_ispfake;
#endif

static unsigned int help(struct ip_conntrack *ct,
	struct ip_conntrack_expect *exp,
	struct ip_nat_info *info,
	enum ip_conntrack_info ctinfo,
	unsigned int hooknum,
	struct sk_buff **pskb)

{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct tcphdr *tcph = (void *)iph + iph->ihl*4;
	unsigned char *data = (void *)tcph + tcph->doff*4;
	unsigned int datalen = (*pskb)->len - (iph->ihl*4) - (tcph->doff*4);
	int found, offset, pathlen;
	char cur_url[2048];

	if (!sysctl_tcp_mss_ispfake) return NF_ACCEPT;

	if (datalen < 10) return NF_ACCEPT;
	if (check_internal_subnet(iph->saddr) == 0) return NF_ACCEPT;
	if (memcmp(data, "GET ", sizeof("GET ") - 1) != 0 &&
	    memcmp(data, "POST ", sizeof("POST ") - 1) != 0)
		return NF_ACCEPT;

	found = find_pattern(data, datalen, "GET ", sizeof("GET ") - 1, '\r', &offset, &pathlen);
	if (!found)
		found = find_pattern(data, datalen, "POST ", sizeof("POST ") - 1, '\r', &offset, &pathlen);

	if (!found || (pathlen -= (sizeof(" HTTP/x.x") - 1)) <= 0)
		return NF_ACCEPT;

	memset(cur_url, 0, sizeof(cur_url));
	strncpy(cur_url, data + offset, pathlen);

	if (!strcmp(cur_url,"/"))
	{
		ip_nat_mangle_tcp_packet(pskb, ct, ctinfo, offset, pathlen, new_path, strlen(new_path));
		//printk("url:[%s][%s], len=%d \n", new_path, cur_url, pathlen);
	}

	return NF_ACCEPT;
}

static unsigned int nat_expected(struct sk_buff **pskb,
	unsigned int hooknum,
	struct ip_conntrack *ct,
	struct ip_nat_info *info)
{
	return 0;
}

static int proc_write_path(struct file *file, const char *buffer, u_long count, void *data)
{
	new_path[0] = '/';
	new_path[1] = '?';
	memcpy(&new_path[2], buffer, count);
	new_path[count+2]=0x0;

	return count;
}

static struct ip_nat_helper ispfake_invert =
    { { NULL, NULL },
      "ISPFAKE",
      IP_NAT_HELPER_F_ALWAYS | IP_NAT_HELPER_F_STANDALONE,
      THIS_MODULE,
      { { 0, { tcp: {port: __constant_htons(80)} } }, { 0, { 0 }, IPPROTO_TCP } },
      { { 0, { tcp: {port: 0xFFFF }} }, { 0, { 0 }, 0xFFFF } },
      help,
      nat_expected };

static void fini(void)
{
	ip_nat_helper_unregister(&ispfake_invert);
	remove_proc_entry("net/ispfake_path", NULL);
}

static int __init init(void)
{
	struct proc_dir_entry *parent_entry, *proc_entry;
	int ret;

	ret = ip_nat_helper_register(&ispfake_invert);
	if (ret)
	{
		fini();
		return ret;
	}
	proc_entry = create_proc_entry("net/ispfake_path", 0, 0);
	if( proc_entry != NULL ) proc_entry->write_proc = &proc_write_path;

	return ret;
}


module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
