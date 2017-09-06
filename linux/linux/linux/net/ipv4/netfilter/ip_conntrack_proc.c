#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/netfilter.h>
#include <linux/in.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_tuple.h>

static struct proc_dir_entry *proc_entry;
extern long atoi(register char *p);

static void convert_data(char *data, u32 *ip, u32 *mask, u16 *proto, u16 *dport)
{
	char *p, buffer[32];
	int m, i;

	strcpy(buffer, data);
	/* ip */	
	p = strtok(buffer, " ");
	*ip = htonl(ipstr_to_u32(p));
	/* mask */	
	p = strtok(NULL, " ");	
	m = atoi(p);
	*mask = 0;
	for (i = m; i > 0; i--)
		*mask |= (1 << (32-i));
	*mask = htonl(*mask);
	/* protocol */
	p = strtok(NULL, " ");	
	if (p) 
	{
		*proto = (u16)atoi(p);
		/* dport */
		p = strtok(NULL, " ");	
		if (p) 
			*dport = (u16)atoi(p);
		else 
			*dport = 0;
	}
	else 
		*proto = 0;
	
}

#undef printk

static int kill_ct_original(const struct ip_conntrack *i, void *data)
{
	u32  src_ipaddr=0, mask=0;
	u16  proto=0, dport=0;
	int rst = 0;

	/* WBM connection */
	if (ntohs(i->tuplehash[IP_CT_DIR_REPLY].tuple.src.u.all) == 55555)
		return 0;

	if (!strcmp("ALL IP\n", (char *)data))
		return 1;

	convert_data((char *)data, &src_ipaddr, &mask, &proto, &dport);

	rst = ((src_ipaddr & mask) == 
		(i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip & mask));

	if (rst && proto)
	{
		if (proto == IPPROTO_ICMP)
		{
			rst = (proto == i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum);
		}
		else if (dport)
		{
			rst = ((proto == i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum) &&
			       (htons(dport) == i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all));
		}
	}
	return rst;
}

static int kill_ct_reply(const struct ip_conntrack *i, void *data)
{
	u32  src_ipaddr=0, mask=0;
	u16  proto=0, dport=0;
	int rst = 0;

	/* WBM connection */
	if (ntohs(i->tuplehash[IP_CT_DIR_REPLY].tuple.src.u.all) == 55555)
		return 0;

	if (!strcmp("ALL IP\n", (char *)data))
		return 1;

	convert_data((char *)data, &src_ipaddr, &mask, &proto, &dport);

	rst = ((src_ipaddr & mask) == 
		(i->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip & mask));

	if (rst && proto)
	{
		if (proto == IPPROTO_ICMP)
		{
			rst = (proto == i->tuplehash[IP_CT_DIR_REPLY].tuple.dst.protonum);
		}
		else if (dport)
		{
			rst = ((proto == i->tuplehash[IP_CT_DIR_REPLY].tuple.dst.protonum) &&
			       (htons(dport) == i->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u.all));
		}
	}
	return rst;
}

static int proc_write( struct file *file, const char *buffer, u_long count, void *data )
{
	ip_ct_selective_cleanup(kill_ct_original, (void *)buffer);
	ip_ct_selective_cleanup(kill_ct_reply, (void *)buffer);
	return count;
}


static u32 init_conntrack_proc(void)
{
        struct proc_dir_entry *conn_proc;

        proc_entry = proc_mkdir( "ctproc", 0 );
	if (!proc_entry)
              return -1;

        conn_proc = create_proc_entry("cleanup", 0, proc_entry);
        if( conn_proc != NULL )
	{
              conn_proc->write_proc = &proc_write;
	}
	return 0;
}

static void fini(void)
{
        remove_proc_entry("cleanup", proc_entry);
}

static int __init init(void)
{
	init_conntrack_proc();
	return 0;
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
