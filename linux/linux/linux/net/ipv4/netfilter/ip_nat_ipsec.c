#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_nat_ipsec.h>
#include <linux/netfilter_ipv4/ip_conntrack_ipsec.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/proc_fs.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

#define ESP_INIT_TIME    60*HZ
#define ESP_KEEP_TIME    3600*4*HZ

struct tuple_table {
        struct ip_conntrack_tuple       tuple;
};

#define MAX_TUPLE_TABLE 80
static struct tuple_table tt_pool[MAX_TUPLE_TABLE];
static tt_pool_idx = 0;

struct tuple_table *alloc_tuple_table(u32 s_addr, u32 d_addr, u32 *tpid)
{
	struct tuple_table             *tt;
	struct ip_conntrack_tuple      *t, ct_tuple;
	struct ip_conntrack_tuple_hash *tuple_hash;
	int idx;

	/*** STEP 1 : for child tuple table ***/
	if (*tpid)
	{
		memset((char *)&(tt_pool[*tpid]), 0, sizeof(struct tuple_table));
		return &tt_pool[*tpid];
	}

	/*** STEP 2 : looking for a used tuple table ***/
	for (idx=0; idx < MAX_TUPLE_TABLE; idx++)
	{
		tt = &tt_pool[idx];
		t = &tt->tuple;

		if (t->src.ip == s_addr && t->dst.ip == d_addr)
		{
			memset((char *)tt, 0, sizeof(struct tuple_table));
			*tpid = idx;
			return tt;
		}
	}

	/*** STEP 3 : new a tuple table ***/
	for (idx=0; idx < MAX_TUPLE_TABLE; idx++)
	{
		tt = &tt_pool[idx];
		t = &tt->tuple;
		if (t->src.ip == 0 && t->dst.ip == 0)
			break;
	}

	if (idx >= MAX_TUPLE_TABLE)
	{
		for (idx=0; idx < MAX_TUPLE_TABLE; idx+=4)
		{
			tt = &tt_pool[idx];
			t = &tt->tuple;
			ct_tuple.src.ip =  t->src.ip;
			ct_tuple.src.u.esp.spi = t->src.u.esp.spi;
			ct_tuple.dst.ip = t->dst.ip;
			ct_tuple.dst.u.esp.peer_spi = t->dst.u.esp.peer_spi;
			ct_tuple.dst.protonum = IPPROTO_ESP;
			tuple_hash = ip_conntrack_find_get( &ct_tuple, NULL );

			if (!tuple_hash) 
			{
				printk("----> index : %d\n", idx);
				break;
			}
		}
	}

	if (idx >= MAX_TUPLE_TABLE)
		return NULL;

	tt_pool_idx = idx;
	memset((char *)&(tt_pool[tt_pool_idx]), 0, sizeof(struct tuple_table));
	*tpid = tt_pool_idx;

	return &tt_pool[tt_pool_idx];
}

static struct ip_conntrack_tuple *
put_esp_tuple(u32 s_addr, u32 d_addr, u32 ospi, u32 ispi, u32 *tpid)
{
	struct tuple_table      *tt;

	if((tt = alloc_tuple_table(s_addr, d_addr, tpid)) == NULL)
	{
		DEBUGP("put_esp_tuple: out of memory\n");
		return NULL;
	}
	tt->tuple.src.ip = s_addr;
	tt->tuple.dst.ip = d_addr;
	tt->tuple.src.u.esp.spi = ospi;
	tt->tuple.dst.u.esp.peer_spi = ispi;
	tt->tuple.dst.protonum = IPPROTO_ESP;

	DEBUGP("put_esp_tuple():");
	DUMP_TUPLE(&(tt->tuple));

	return (&tt->tuple); 
}

static int 
update_esp_tuple(struct ip_conntrack_tuple *tuple, u32 key_spi, u32 update_spi)
{
	struct tuple_table              *tt;
	struct ip_conntrack_tuple       *t;
	int  idx;

	for (idx=0; idx < MAX_TUPLE_TABLE; idx++)
	{
		tt = &tt_pool[idx];
		t = &tt->tuple;
		//DUMP_TUPLE(&(tt->tuple));

		if (t->src.u.esp.spi == key_spi) 
		{
			t->dst.u.esp.peer_spi = update_spi;
			if (!t->src.ip) t->src.ip = tuple->dst.ip;
		}
		if (t->dst.u.esp.peer_spi == key_spi) 
		{
			t->src.u.esp.spi = update_spi;
			if(!t->dst.ip) t->dst.ip = tuple->dst.ip;
		}
	}

	return 0;
}

static struct ip_conntrack_tuple *
get_esp_tuple(u32 s_addr, u32 d_addr, u32 ospi)
{
	struct tuple_table              *tt;
	struct ip_conntrack_tuple       *t;
	int  idx;

	for (idx=0; idx < MAX_TUPLE_TABLE; idx++)
	{
		tt = &tt_pool[idx];
		t = &tt->tuple;

		DUMP_TUPLE(&(tt->tuple));

		if (t->src.ip == s_addr && t->dst.ip == d_addr && t->src.u.esp.spi == ospi) 
		{
			return t;
		}
	}

	DEBUGP("get_esp_tuple(): FAILED to lookup tuple: ");
	DEBUGP("%d.%d.%d.%d -> ", NIPQUAD(s_addr));
	DEBUGP("%d.%d.%d.%d ", NIPQUAD(d_addr));
	DEBUGP("O_SPI=%08x\n", ospi);
	return NULL;
}

static struct ip_conntrack_tuple *
in_get_esp_tuple(u32 s_addr, u32 d_addr, u32 ispi)
{
	struct tuple_table              *tt;
	struct ip_conntrack_tuple       *t;
	int  idx;

	for (idx=0; idx < MAX_TUPLE_TABLE; idx++)
	{
		tt = &tt_pool[idx];
		t = &tt->tuple;

		if (t->src.ip == s_addr && t->dst.ip == d_addr && t->src.u.esp.spi == ispi) 
		{
			return t;
		}
	}

	for (idx=0; idx < MAX_TUPLE_TABLE; idx++)
	{
		tt = &tt_pool[idx];
		t = &tt->tuple;

		if (t->src.ip == s_addr && (t->dst.ip == d_addr || t->dst.ip == 0) && t->src.u.esp.spi == 0) 
		{
			return t;
		}
	}

	DEBUGP("in_get_esp_tuple(): FAILED to lookup tuple: ");
	DEBUGP("%d.%d.%d.%d -> ", NIPQUAD(s_addr));
	DEBUGP("%d.%d.%d.%d ", NIPQUAD(d_addr));
	DEBUGP("O_SPI=%08x\n", ispi);
	return NULL;
}



/* ESP Protocol Registration */

static int esp_pkt_to_tuple(const void *datah, size_t datalen, struct ip_conntrack_tuple *tuple)
{
	u32 spi = *(u32 *)datah;
	struct ip_conntrack_tuple *tt;

	DEBUGP("> esp_pkt_to_tuple() : %08x\n", ntohl(spi));

	tuple->src.u.esp.spi = 0;
	tuple->dst.u.esp.peer_spi = 0;
	tt = get_esp_tuple(tuple->src.ip, tuple->dst.ip, spi);

	if (tt)
	{
		DEBUGP("  %08x:%08x -> %08x:%08x \n", tt->src.ip, ntohl(tt->src.u.esp.spi), tt->dst.ip, ntohl(tt->dst.u.esp.peer_spi));
		tuple->src.u.esp.spi = tt->src.u.esp.spi;
		tuple->dst.u.esp.peer_spi = tt->dst.u.esp.peer_spi;
	}

	return 1;
}

static int esp_invert_tuple(struct ip_conntrack_tuple *tuple, const struct ip_conntrack_tuple *orig)
{
	DEBUGP("> esp_invert_tuple() \n");

	tuple->src.u.esp.spi = orig->dst.u.esp.peer_spi;
	tuple->dst.u.esp.peer_spi = orig->src.u.esp.spi;

	return 1;
}

static unsigned int esp_print_tuple(char *buffer, const struct ip_conntrack_tuple *tuple)
{
	return sprintf(buffer, "spi=%08x ", ntohl(tuple->src.u.esp.spi));
}

/* Print out the private part of the conntrack. */
static unsigned int esp_print_conntrack(char *buffer, const struct ip_conntrack *conntrack)
{
        return 0;
}

static int esp_established(
	struct ip_conntrack *ct, struct iphdr *iph, 
	size_t len, enum ip_conntrack_info ctinfo)
{
	char *data =  (void *)iph + iph->ihl*4;
	u32  spi = *(u32 *)data;
	int  dir = CTINFO2DIR(ctinfo);
	struct ip_conntrack_tuple *tuple;

	DEBUGP("> esp_established() : %d : %08x\n", CTINFO2DIR(ctinfo), iph->saddr);
	//DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
	//DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	ip_ct_refresh(ct, ESP_INIT_TIME);

	if (check_internal_subnet(iph->saddr))
	{
		tuple = get_esp_tuple(ct->tuplehash[dir].tuple.src.ip, ct->tuplehash[dir].tuple.dst.ip, spi);
		if (tuple == NULL)
		{
			int tpid = 0;
			tuple = put_esp_tuple(ct->tuplehash[dir].tuple.src.ip, ct->tuplehash[dir].tuple.dst.ip, spi, 0, &tpid);
			if (!tuple)
				return 0;
			tpid++;
			put_esp_tuple(ct->tuplehash[dir].tuple.dst.ip, ct->tuplehash[dir].tuple.src.ip, 0, spi, &tpid);
			tpid++;
			put_esp_tuple(ct->tuplehash[!dir].tuple.src.ip, 0, 0, spi, &tpid);
			tpid++;
			put_esp_tuple(0, ct->tuplehash[!dir].tuple.src.ip, spi, 0, &tpid);
		}
		else
		{
			DEBUGP(">hit : %08x:%08x -> %08x:%08x \n",
				tuple->src.ip, ntohl(tuple->src.u.esp.spi), tuple->dst.ip, ntohl(tuple->dst.u.esp.peer_spi));
		}
	}
	else
	{
		tuple = in_get_esp_tuple(ct->tuplehash[dir].tuple.src.ip, ct->tuplehash[dir].tuple.dst.ip, spi);

		if (tuple)
		{
			if (!tuple->src.u.esp.spi)
			{
				DEBUGP(">update : %08x, tuple dst : %08x \n", spi, ntohl(tuple->dst.u.esp.peer_spi));
				update_esp_tuple(&(ct->tuplehash[dir].tuple), tuple->dst.u.esp.peer_spi, spi);
			}
			else
			{
				DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
				DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);
				DEBUGP("tuple src : %08x, tuple dst : %08x \n", ntohl(tuple->src.u.esp.spi), ntohl(tuple->dst.u.esp.peer_spi));

				ip_ct_refresh(ct, ESP_KEEP_TIME);
			}

			ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.esp.spi = tuple->dst.u.esp.peer_spi;
			ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.esp.peer_spi = spi;
			ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u.esp.spi = spi;
			ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u.esp.peer_spi = tuple->dst.u.esp.peer_spi;
		}
	}


	return NF_ACCEPT;
}

static int esp_new(struct ip_conntrack *conntrack, struct iphdr *iph, size_t len)
{
	DEBUGP("> esp_new() \n");
	return 1;
}


struct ip_conntrack_protocol ip_conntrack_protocol_esp =
{
	{ NULL, NULL }, IPPROTO_ESP, "esp",
	esp_pkt_to_tuple, esp_invert_tuple, esp_print_tuple, esp_print_conntrack,
	esp_established, esp_new, NULL, NULL, NULL 
};

static unsigned int outbound_esp(struct ip_conntrack *ct, u32 ospi)
{
	return 1;
}

static unsigned int inbound_esp(struct ip_conntrack *ct, u32 ospi)
{
	return 1;
}



/*************** NAT HELPER : not used ***************************/

static unsigned int esp_help(   struct ip_conntrack *ct,
                                struct ip_conntrack_expect *exp,
                                struct ip_nat_info *info,
                                enum ip_conntrack_info ctinfo,
                                unsigned int hooknum,
                                struct sk_buff **pskb)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	char *data =  (void *)iph + iph->ihl*4;
	u32  spi = *(u32 *)data;
	int  dir = CTINFO2DIR(ctinfo);

	//DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
	//DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	if (spi == 0)
	{
		DEBUGP("ESP SPI invalid \n");
	}
	else if (hooknum == NF_IP_POST_ROUTING && dir == IP_CT_DIR_ORIGINAL)
	{
		DEBUGP("ESP NAT HELP ORG -  SPI : %08x \n\n\n", ntohl(spi));
		outbound_esp(ct, spi);
	}
	else if (hooknum == NF_IP_PRE_ROUTING && dir == IP_CT_DIR_REPLY)
	{
		DEBUGP("ESP NAT HELP RPY -  SPI : %08x \n\n\n", ntohl(spi));
		inbound_esp(ct, spi);
	}
	else
	{
		DEBUGP("ESP NAT HELP NONE -  SPI : %08x \n\n\n", ntohl(spi));
		;
	}

	return NF_ACCEPT;
}


static struct ip_nat_helper esp =  { 
	{ NULL, NULL },
	"esp",
	IP_NAT_HELPER_F_ALWAYS,
	THIS_MODULE,
	{ { 0, { 0 } },
	{ 0, { 0 }, IPPROTO_ESP } },
	{ { 0, { 0 } },
	{ 0, { 0 }, 0xFFFF } },
	esp_help,
	NULL 
};


/*************** Proc File System  ***************************/

struct proc_dir_entry *ipsec_proc_entry = NULL;

static int ipsec_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *out = page;
	struct tuple_table      *tt;
	struct ip_conntrack_tuple *t;
	int len, idx;

	out += sprintf (out, "ESP tuple list\n");

	for (idx=0; idx < MAX_TUPLE_TABLE; idx++)
	{
		tt = &tt_pool[idx];
		t = &tt->tuple;

		if (t->dst.protonum)
		{
			out += sprintf(out, "tuple %p: %u %u.%u.%u.%u:%08x -> %u.%u.%u.%u:%08x,\n",
					t, t->dst.protonum,
					NIPQUAD(t->src.ip), ntohl(t->src.u.esp.spi),
					NIPQUAD(t->dst.ip), ntohl(t->dst.u.esp.peer_spi));
		}
        }

	len = out - page;
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;

	*start = page + off;

	return len;
}

static void pptp_proc_create( void )
{
	ipsec_proc_entry = create_proc_entry("ipsec", S_IFREG|S_IRUGO|S_IWUSR, &proc_root);
	if (ipsec_proc_entry == NULL) {
		printk("pptp.c: unable to initialise /proc/ipsec\n");
		return;
	}
	ipsec_proc_entry->data = (void*)NULL;
	ipsec_proc_entry->read_proc = ipsec_proc_read;
}

static int __init init(void)
{
	int ret = 0;

	printk("IPSEC netfilter NAT helper: ");

#if 0
	ret = ip_nat_helper_register(&esp);

	if (ret == 0)
		printk("registered\n");
	else
		printk("register fail\n");
#endif

	memset(tt_pool, 0, sizeof(struct tuple_table) * MAX_TUPLE_TABLE);
	ip_conntrack_protocol_register(&ip_conntrack_protocol_esp);

	pptp_proc_create();

	return ret;
}

static void __exit fini(void)
{
	ip_nat_helper_unregister(&esp);
}

module_init(init);
module_exit(fini);

