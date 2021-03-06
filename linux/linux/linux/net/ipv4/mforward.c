#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <linux/skbuff.h>
#include <linux/brlock.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/if_bridge.h>
#include <linux/divert.h>
#include <net/dst.h>
#include <net/pkt_sched.h>
#include <net/profile.h>
#include <net/checksum.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/module.h>
#if defined(CONFIG_NET_RADIO) || defined(CONFIG_NET_PCMCIA_RADIO)
#include <linux/wireless.h>             /* Note : will define WIRELESS_EXT */
#include <net/iw_handler.h>
#endif  /* CONFIG_NET_RADIO || CONFIG_NET_PCMCIA_RADIO */

#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>

#include <linux/icmp.h>

static int mforward_wl = 0;

extern unsigned int get_dev_ip( struct net_device *dev );
extern int (*multicast_forward_handler)(struct sk_buff *skb,struct net_device *dev);
extern u_int16_t ip_nat_cheat_check(u_int32_t oldvalinv, u_int32_t newval, u_int16_t oldcheck);

#undef printk
#define IF_WAN "vlan1"
static int multi_forward(struct sk_buff *skb, struct net_device *rx_dev)
{
	struct ethhdr *ethh = skb->mac.ethernet;
	struct iphdr *iph = (struct iphdr *)(skb->data);

	if ((ethh->h_proto == htons(ETH_P_IP)) && ((ntohl(iph->daddr) & 0xf0000000) == 0xe0000000)) 
	{
		struct net_device *destdev = NULL;

#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
		if (!strcmp(skb->dev->name, "br0"))
#else
		if (!strcmp(skb->dev->name, "vlan2"))
#endif
		{       /* skip UPNP IP Address */
			if ((ntohl(iph->daddr) & 0xff000000) != 0xef000000) 
			{
				u32 oldip, newip;

                                destdev = dev_get_by_name(IF_WAN);
                                oldip = iph->saddr;
                                iph->saddr =  get_dev_ip( destdev );
                                newip = iph->saddr;
                                iph->check = ip_nat_cheat_check( ~oldip, newip, iph->check );
				if ((iph->protocol == IPPROTO_UDP) && ((ntohs(iph->frag_off) & 0xff) == 0))
				{
					struct udphdr *uh = (struct udphdr *)((u_int32_t *)iph + iph->ihl);
					uh->check = 0;
				}
                                memcpy(&skb->mac.ethernet->h_source[0], &destdev->dev_addr[0], 6);
			}
		}
		else if (!strcmp(skb->dev->name, IF_WAN)) 
		{
#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
			destdev = dev_get_by_name(mforward_wl ? "br0" : "vlan2");
#else
			destdev = dev_get_by_name("vlan2");
#endif
			memcpy(&skb->mac.ethernet->h_source[0], &destdev->dev_addr[0], 6);
		}

		if (destdev)
		{
			skb->dev = destdev;
			skb_push(skb, ETH_HLEN);
			dev_queue_xmit(skb);
			dev_put(rx_dev);
			return 1;
		}
	}
	return 0;
}


static int proc_read(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
        char *p = buffer;
        int len;

        if (multicast_forward_handler)
                p += sprintf(p, "1");
        else
                p += sprintf(p, "0");

        len = p - buffer;
        if( len <= offset+length ) *eof = 1;
        *start = buffer + offset;
        len -= offset;
        if( len > length ) len = length;
        if( len < 0 ) len = 0;

        return len;
}

static int proc_write( struct file *file, const char *buffer, u_long count, void *data )
{
        if (buffer[0] == '1')
                multicast_forward_handler = multi_forward;
        else
                multicast_forward_handler = NULL;

        return count;
}



#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
static int proc_read_mforward_wl(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
        char *p = buffer;
        int len;

        p += sprintf(p, "%d", mforward_wl);

        len = p - buffer;
        if( len <= offset+length ) *eof = 1;
        *start = buffer + offset;
        len -= offset;
        if( len > length ) len = length;
        if( len < 0 ) len = 0;

        return len;
}

static int proc_write_mforward_wl( struct file *file, const char *buffer, u_long count, void *data )
{
        if (buffer[0] == '1')
                mforward_wl = 1;
        else
                mforward_wl = 0;

        return count;
}
#endif

static int create_proc_multiforward(void)
{
        struct proc_dir_entry *proc_entry;

        proc_entry = create_proc_entry("driver/multiforward",0,0);
	if(proc_entry)
        {
                proc_entry->read_proc=&proc_read;
                proc_entry->write_proc = &proc_write;
        }
#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
        proc_entry = create_proc_entry("driver/mforward_wl",0,0);
        if(proc_entry)
        {
                proc_entry->read_proc=&proc_read_mforward_wl;
                proc_entry->write_proc=&proc_write_mforward_wl;
        }
#endif
        return 0;
}

static int delete_proc_multiforward(void)
{
        remove_proc_entry("driver/multiforward", NULL );
        return 0;
}

static void fini(void)
{
	multicast_forward_handler = NULL;
	delete_proc_multiforward();
}

static int __init init(void)
{
	printk("Multicast Forward Module Init.\n");
#ifdef CONFIG_MULTICAST_FORWARD_MODULE
	multicast_forward_handler = multi_forward;
#endif

	create_proc_multiforward();
	return 0;
}

module_init(init);
module_exit(fini);

