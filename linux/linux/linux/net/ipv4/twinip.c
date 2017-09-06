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
#define IPCLONE_WAN1_VIRTUAL_IP 0xc0a8ff02 /* 192.168.255.2 */
#define IPCLONE_WAN2_VIRTUAL_IP 0xc0a8ff03 /* 192.168.255.3 */
u32 ipclone_virtual_ip[2] = {0,0};
u32 ipclone_wanip[2] = {0,0};
extern unsigned char  ipclone_mac[2][6];
u8  ipclone_skip[2] = {1,1};
static void ipclone_from_lan_to_wan(struct sk_buff *skb);
static void ipclone_from_wan_to_lan(struct sk_buff *skb);
extern unsigned int get_dev_ip( struct net_device *dev );
static int create_proc_ipclone(void);
extern u_int16_t ip_nat_cheat_check(u_int32_t oldvalinv, u_int32_t newval, u_int16_t oldcheck);

extern u32 ipstr_to_u32 ( char *ipstr );

extern void (*twinip_rx_handler)(struct sk_buff *skb);
extern void (*twinip_tx_handler)(struct sk_buff *skb, char *ifname);

void twinip_rx(struct sk_buff *skb)
{
	if (ipclone_virtual_ip[0] || ipclone_virtual_ip[1])
	{
#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
		if (!strcmp(skb->dev->name, "br0"))
#else
		if (!strcmp(skb->dev->name, "eth0"))
#endif
			ipclone_from_lan_to_wan(skb);
        }
}

void twinip_tx(struct sk_buff *skb, char *ifname)
{
        if (ipclone_virtual_ip[0] || ipclone_virtual_ip[1])
        {
#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
                if (!strcmp(ifname, "br0"))
#else
                if (!strcmp(ifname, "eth0"))
#endif
                        ipclone_from_wan_to_lan(skb);
        }
}

static int compare_mac(unsigned char *m1, unsigned char *m2)
{
        int rc = 0;

        if ((m1[0] == m2[0]) && (m1[1] == m2[1]) &&
            (m1[2] == m2[2]) && (m1[3] == m2[3]) &&
            (m1[4] == m2[4]) && (m1[5] == m2[5]))
                rc = 1;

        return rc;
}

#if	0
int print_packet(unsigned char *data, int size )
{
	int i;

	for( i = 0 ; i < size; i++ ) 
	{
		if(!(i%16)) printk("\n");
		printk("%02x ", data[i] );
	}

}
#endif

static void ipclone_from_lan_to_wan(struct sk_buff *skb)
{
        int ipcloned[2] = {0,0}, ipcloned_id;
        struct ethhdr *ethh = skb->mac.ethernet;
        unsigned int src_ip, dst_ip;
        int dest_clone = 0;

        ipcloned[0] = compare_mac(ethh->h_source, ipclone_mac[0]);
        ipcloned[1] = compare_mac(ethh->h_source, ipclone_mac[1]);
        ipcloned_id = (ipcloned[1]) ? 1 : 0;

#if 0
        {
        struct iphdr *iph = (struct iphdr *)(ethh + 1);
        printk("== RX => %s [%d]: ", skb->dev->name, ipcloned[0]);

        if (htons(ethh->h_proto) == ETH_P_IP)
                printk(" %08x -> %08x : ", iph->saddr, iph->daddr);
        else
                printk(" NO IP Type : %04x ", ethh->h_proto);

        printk(" %02x-%02x-%02x-%02x-%02x-%02x ",
                ethh->h_dest[0], ethh->h_dest[1], ethh->h_dest[2],
                ethh->h_dest[3], ethh->h_dest[4], ethh->h_dest[5]);
        printk(" : %02x-%02x-%02x-%02x-%02x-%02x \n",
                ethh->h_source[0], ethh->h_source[1], ethh->h_source[2],
                ethh->h_source[3], ethh->h_source[4], ethh->h_source[5]);
        }
#endif

        if (!ipcloned[0] && !ipcloned[1])
        {
                if (htons(ethh->h_proto) == ETH_P_ARP)
                {
                        //struct arphdr *arph = (struct arphdr *)(ethh + 1);
                        struct arphdr *arph = skb->nh.arph;
                        char *target = (char *)(arph + 1) + 16; /* target ip address */

                        if (arph->ar_op != htons(ARPOP_REPLY)) return;

                        dest_clone = 1;

                        if (*(u32 *)target == htonl(ipclone_virtual_ip[0]))
                                ipcloned_id = 0;
                        else if (*(u32 *)target == htonl(ipclone_virtual_ip[1]))
                                ipcloned_id = 1;
                        else
                                return;
                }
                else
                        return;
        }

        if(htons(ethh->h_proto) == ETH_P_IP)
        {
                struct iphdr *iph = (struct iphdr *)(skb->nh.iph);
                src_ip = iph->saddr;
                dst_ip = iph->daddr;

                if ((((get_dev_ip(skb->dev) & htonl(0xffffff00)) == (src_ip & htonl(0xffffff00))) || src_ip == 0) &&
                      (ipcloned[0] || ipcloned[1]))
                {
                        ipclone_skip[ipcloned_id] = 1;
                        printk("x2xx %08x \n", iph->daddr);
                }
                else if (ipcloned[0] || ipcloned[1])
                {

#if defined CONFIG_ARCH_TIMEG && defined CONFIG_BRIDGE_PASSTHROUGH
                        //if( br_device && compare_mac(br_device->dev_addr, ethh->h_dest ))
                        if( !strcmp(skb->dev->name, "br0") && compare_mac(skb->dev->dev_addr, ethh->h_dest ))
#endif
                        {
                                int oldip=0, newip=0;

                                if (dest_clone)
                                {
                                        oldip = dst_ip;
                                        iph->daddr = htonl(ipclone_virtual_ip[ipcloned_id]);
                                        newip = iph->daddr;

                                        printk("---> dest clone : old : %08x, new : %08x\n", oldip, newip);
                                }
                                else
                                {
                                        oldip = src_ip;
                                        iph->saddr = htonl(ipclone_virtual_ip[ipcloned_id]);
                                        newip = iph->saddr;
                                }

                                iph->check = ip_nat_cheat_check( ~oldip, newip, iph->check );

                                if (iph->protocol == 17 && !(htons(iph->frag_off) & 0xff))
                                {
                                        struct udphdr *udph = (struct udphdr *)((char *)iph + (iph->ihl<<2));
                                        if (udph->check)
                                                udph->check = ip_nat_cheat_check( ~oldip, newip, udph->check );
                                }
                                else if (iph->protocol == 6 && !(htons(iph->frag_off) & 0xff))
                                {
                                        struct tcphdr *tcph = (struct tcphdr *)((char *)iph + (iph->ihl<<2));
                                        tcph->check = ip_nat_cheat_check( ~oldip, newip, tcph->check );
                                }
                                //printk("REAL IP clone Rx-  %08x, %08x\n", iph->saddr, iph->daddr);

                                ipclone_skip[ipcloned_id] = 0;
                                return;
                        }
                }
        }
        else if (htons(ethh->h_proto) == ETH_P_ARP)
        {
                //struct arphdr *arph = (struct arphdr *)(ethh + 1);
                struct arphdr *arph = skb->nh.arph;
                char *source = (char *)(arph + 1) + 6; /* source ip address */
                char *target = (char *)(arph + 1) + 16; /* target ip address */

                printk("arp(L2W) : op:%d, (src :%08x) (dst : %08x)\n", ntohs(arph->ar_op), *(u32 *)source, *(u32 *)target);

                if (arph->ar_op == htons(ARPOP_REQUEST) && (ipcloned[0] || ipcloned[1]))
                {
                        if ((get_dev_ip(skb->dev) & htonl(0xffffff00)) == (*(u32 *)source & htonl(0xffffff00)))
                        {
                                ipclone_skip[ipcloned_id] = 1; // still use a private ip on twinip PC
                        }
                        else if (!*(u32 *)source)
                        {
                                printk("   1. arp-req(source) : %08x -> ",  *(u32 *)source);
                                *(__u32 *)source = *(__u32 *)target;
                                printk("      %08x  \n", *(u32 *)source);
                        }
                        else if ((*(u32 *)source !=  *(__u32 *)target) &&
                                (*(u32 *)source != htonl(ipclone_virtual_ip[ipcloned_id])))
                        {       // CASE 1: Twin IP ARP request
                                printk("   2. arp-req(source) : %08x -> ",  *(u32 *)source);
                                *(u32 *)source = htonl(ipclone_virtual_ip[ipcloned_id]); // WAN_IP -> Virtual_IP
                                printk("      %08x  \n", *(u32 *)source);
                                ipclone_skip[ipcloned_id] = 0;
                        }
                        else if ((*(u32 *)source ==  *(u32 *)target) &&
                                (*(u32 *)source == htonl(ipclone_wanip[ipcloned_id])))
                        {       // Gratuitous ARP from Twin IP
                                printk("   5. arp-req(gratuitous) : %08x -> ",  *(u32 *)target);
                                *(u32 *)source = htonl(ipclone_virtual_ip[ipcloned_id]); // WAN_IP -> Virtual_IP
                                *(u32 *)target = htonl(ipclone_virtual_ip[ipcloned_id]); // WAN_IP -> Virtual_IP
                                printk("      %08x  \n", *(u32 *)target);
                        }
                }
                else if (arph->ar_op == htons(ARPOP_REPLY) && *(u32 *)source !=  *(u32 *)target)
                {
                        if (*(u32 *)source == htonl(ipclone_wanip[ipcloned_id]) && (ipcloned[0] || ipcloned[1]))
                        {       // CASE 2: Twin IP ARP reply
                                printk("   3. arp-rsp(source) : %08x -> ",  *(u32 *)source);
                                *(u32 *)source = htonl(ipclone_virtual_ip[ipcloned_id]);  // WAN_IP -> Virtual_IP
                                printk("      %08x  \n", *(u32 *)source);
                        }
                        else if ((*(__u32 *)source == htonl((ipclone_virtual_ip[0] & 0xffffff00) | 0x1)) && dest_clone)
                        {       // CASE 3: Private IP that communicate with Twin IP : ARP reply
                                printk("   4. arp-rsp(target) : %08x -> ",  *(u32 *)target);
                                *(u32 *)target = htonl(ipclone_wanip[ipcloned_id]);  // Virtual_IF_IP -> WAN_IP
                                printk("      %08x  \n", *(u32 *)target);
                        }
                }

                return;
	}
}

static void ipclone_from_wan_to_lan(struct sk_buff *skb)
{
        struct ethhdr *ethh = (struct ethhdr *)(skb->data);
        int ipcloned[2] = {0,0}, ipcloned_id;

#if 0
        {
        struct iphdr *iph = (struct iphdr *)(ethh + 1);
        printk("== TX => %s : ", skb->dev->name);

        if (htons(ethh->h_proto) == ETH_P_IP)
                printk(" %08x -> %08x : ", iph->saddr, iph->daddr);
        else
                printk(" NO IP Type : %04x ", ethh->h_proto);

        printk(" %02x-%02x-%02x-%02x-%02x-%02x ",
                ethh->h_dest[0], ethh->h_dest[1], ethh->h_dest[2],
                ethh->h_dest[3], ethh->h_dest[4], ethh->h_dest[5]);
        printk(" : %02x-%02x-%02x-%02x-%02x-%02x \n",
                ethh->h_source[0], ethh->h_source[1], ethh->h_source[2],
                ethh->h_source[3], ethh->h_source[4], ethh->h_source[5]);
        }
#endif
        ipcloned[0] = compare_mac(ethh->h_dest, ipclone_mac[0]);
        ipcloned[1] = compare_mac(ethh->h_dest, ipclone_mac[1]);
        ipcloned_id = (ipcloned[1]) ? 1 : 0;

        if (htons(ethh->h_proto) == ETH_P_IP)
        {
                if (ipclone_skip[ipcloned_id] || !ipclone_virtual_ip[ipcloned_id])
                {
                        ; /* nothing */
                }
                else if (ipcloned[0] || ipcloned[1])
                {
                        struct iphdr *iph = (struct iphdr *)(ethh + 1);
                        u32 oldip=0, newip=0;

                        oldip = iph->daddr;
                        iph->daddr = htonl(ipclone_wanip[ipcloned_id]);
                        newip = iph->daddr;
                        // printk("->old : %08x, new : %08x\n", oldip, newip);
                        iph->check = ip_nat_cheat_check( ~oldip, newip, iph->check );

                        if (iph->protocol == 17 && !(htons(iph->frag_off) & 0xff))
                        {
                                struct udphdr *udph = (struct udphdr *)((char *)iph + (iph->ihl<<2));
                                if (udph->check)
                                        udph->check = ip_nat_cheat_check( ~oldip, newip, udph->check );
                        }
                        else if (iph->protocol == 6 && !(htons(iph->frag_off) & 0xff))
                        {
                                struct tcphdr *tcph = (struct tcphdr *)((char *)iph + (iph->ihl<<2));
                                tcph->check = ip_nat_cheat_check( ~oldip, newip, tcph->check );
                        }
                        else if (iph->protocol == 1) /* ICMP */
                        {
                                /* Type 3 : Unreachable,
                                Code 3 : Destination unreachable, Code 4 : Need to fragment */

                                struct icmphdr *icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
                                if (icmph->type == 3 && (icmph->code == 3 || icmph->code == 4))
                                {
                                        struct iphdr *iph = (struct iphdr *)(icmph + 1);
                                        oldip = iph->saddr;
                                        iph->saddr = htonl(ipclone_wanip[ipcloned_id]);
                                        newip = iph->saddr;

                                        iph->check = ip_nat_cheat_check( ~oldip, newip, iph->check );
                                }
                        }
                }
        }
        else if (htons(ethh->h_proto) == ETH_P_ARP)
        {
                //struct arphdr *arph = (struct arphdr *)(ethh + 1);
                struct arphdr *arph = skb->nh.arph;
                char *target = (char *)(arph + 1) + 16; /* target address */
                char *source = (char *)(arph + 1) + 6; /* source ip address */

                printk("arp(W2L) : op:%d, (src :%08x) (dst : %08x)\n", ntohs(arph->ar_op), *(u32 *)source, *(u32 *)target);

                if (arph->ar_op == htons(ARPOP_REQUEST) && (!ipclone_skip[0] || !ipclone_skip[1]))
                {

                        if ((*(u32 *)target == htonl(ipclone_virtual_ip[0])) && !ipclone_skip[0])
                        {       // CASE 2 : CPU ARP request to Twin IP
                                printk("   1. arp-req(target) : %08x -> ",  *(u32 *)target);
                                *(u32 *)target = htonl(ipclone_wanip[0]); /* Virtual_IP -> WAN1_IP */
                                printk("      %08x  \n", *(u32 *)target);
                        }
                        else if ((*(u32 *)target == htonl(ipclone_virtual_ip[1])) && !ipclone_skip[1])
                        {       // CASE 2 : CPU ARP request to Twin IP
                                printk("   2. arp-req(target) : %08x -> ",  *(u32 *)target);
                                *(u32 *)target = htonl(ipclone_wanip[1]); /* Virtual_IP -> WAN2_IP */
                                printk("      %08x  \n", *(u32 *)target);
                        }
                        else if ((*(u32 *)source == htonl(ipclone_wanip[0])) || (*(u32 *)source == htonl(ipclone_wanip[1])))
                        {       // CASE 3: CPU ARP request to Private IP that communicate with Twin IP
                                printk("   3. arp-req(source) : %08x -> ",  *(u32 *)source);
                                *(u32 *)source = htonl((ipclone_virtual_ip[0] & 0xffffff00) | 0x1);
                                printk("      %08x  \n", *(u32 *)source);
                        }
                }
                else if (arph->ar_op == htons(ARPOP_REPLY) && (!ipclone_skip[0] || !ipclone_skip[1]))
                {
                        if ((*(u32 *)target == htonl(ipclone_virtual_ip[ipcloned_id])))
                        {       // CASE 1: CPU ARP reply to Twin IP
                                printk("   4. arp-rsq(target) [%d] : %08x -> ", ipclone_skip[0], *(u32 *)target);
                                *(u32 *)target = htonl(ipclone_wanip[ipcloned_id]);  /*  Virtual_IP -> WAN2_IP */
                                printk("%08x \n", *(u32 *)target);
                        }
                }
        }
}

static int proc_read_ipclone(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
        char *p = buffer;
        int len;

        p += sprintf(p, "%08x %02x-%02x-%02x-%02x-%02x-%02x\n",
                ipclone_virtual_ip[0],
                ipclone_mac[0][0], ipclone_mac[0][1], ipclone_mac[0][2],
                ipclone_mac[0][3], ipclone_mac[0][4], ipclone_mac[0][5]);
        p += sprintf(p, "%08x %02x-%02x-%02x-%02x-%02x-%02x\n",
                ipclone_virtual_ip[1],
                ipclone_mac[1][0], ipclone_mac[1][1], ipclone_mac[1][2],
                ipclone_mac[1][3], ipclone_mac[1][4], ipclone_mac[1][5]);

        len = p - buffer;
        if( len <= offset+length ) *eof = 1;
        *start = buffer + offset;
        len -= offset;
        if( len > length ) len = length;
        if( len < 0 ) len = 0;

        return len;
}

static int proc_write_ipclone(struct file *file, const char *buffer, u_long count, void *data)
{
        char *deli, *start, buf[32], *buf2;
        int mac[2][6], w,i;

        memcpy( buf, buffer, count);
        buf[count]=0x0;

        w = 0;
        buf2 = strtok(buf, " ");
        do
        {
                if (strlen(buf2) < strlen("00-00-00-00-00-00"))
                        break;

                for(i=0, start=buf2; i<6  ; i++ )
                {
                        deli = strchr(start, '-' );
                        if(deli)
                                *deli = 0x0;
                        mac[w][i] = simple_strtoul(start, NULL, 16);
                        start = deli+1;
                }
                w++;
        } while ((w < 2) && ((buf2 = strtok(NULL, " ")) != NULL));

        for (w = 0; w < 2; w++)
        {
                if (mac[w][0] == 0 && mac[w][1] == 0 && mac[w][2] == 0 &&
                    mac[w][3] == 0 && mac[w][4] == 0 && mac[w][5] == 0)
                {
                        ipclone_virtual_ip[w] = 0;
                        ipclone_skip[w] = 1;
                }
                else
                {
                        ipclone_virtual_ip[w] = (w) ? IPCLONE_WAN2_VIRTUAL_IP : IPCLONE_WAN1_VIRTUAL_IP;
                }
                for (i=0; i<6; i++)
                        ipclone_mac[w][i] = (unsigned int)mac[w][i];
        }

        return count;
}


static int proc_read_wanip(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
        char *p = buffer;
        int len;

        p += sprintf(p, "%08x %08x", ipclone_wanip[0], ipclone_wanip[1] );

        len = p - buffer;
        if( len <= offset+length ) *eof = 1;
        *start = buffer + offset;
        len -= offset;
        if( len > length ) len = length;
        if( len < 0 ) len = 0;

        return len;
}

static int proc_write_wanip(struct file *file, const char *buffer, u_long count, void *data)
{
        char buf[32], *buf2;
        int w;

        memcpy(buf, buffer, count);
        buf[count]=0x0;

        w = 0;
        buf2 = strtok(buf, " ");
        do
        {
                ipclone_wanip[w] = ipstr_to_u32 ( buf2 );
                w++;

        } while ((buf2 = strtok(NULL, " ")) != NULL);

        return count;
}


#ifndef CONFIG_DRIVERLEVEL_REAL_IPCLONE_MODULE
static int proc_twinip_read(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
        char *p = buffer;
        int len;

        if (twinip_rx_handler)
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

static int proc_twinip_write( struct file *file, const char *buffer, u_long count, void *data )
{
        if (buffer[0] == '1')
        {
                twinip_rx_handler = twinip_rx;
                twinip_tx_handler = twinip_tx;
        }
        else
        {
                twinip_rx_handler = NULL;
                twinip_tx_handler = NULL;
        }

        return count;
}
#endif

static int create_proc_ipclone(void)
{
        struct proc_dir_entry *proc_entry;
        char name[128];

printk("==> create_proc_ipclone() form DEV.c \n");

        sprintf(name, "driver/ipclone");
        proc_entry = create_proc_entry(name,0,0);
        if(proc_entry)
        {
                proc_entry->write_proc=&proc_write_ipclone;
                proc_entry->read_proc=&proc_read_ipclone;
        }

        sprintf(name, "driver/wanip");
        proc_entry = create_proc_entry(name,0,0);
        if(proc_entry)
        {
                proc_entry->write_proc=&proc_write_wanip;
                proc_entry->read_proc=&proc_read_wanip;
        }

#ifndef CONFIG_DRIVERLEVEL_REAL_IPCLONE_MODULE
        proc_entry = create_proc_entry("driver/twinip",0,0);
        if(proc_entry)
        {
                proc_entry->read_proc = &proc_twinip_read;
                proc_entry->write_proc = &proc_twinip_write;
        }
#endif
        return 0;
}

static int delete_proc_ipclone(void)
{
	remove_proc_entry("driver/ipclone", NULL );
	remove_proc_entry("driver/wanip", NULL );
	return 0;
}

static void fini(void)
{
	delete_proc_ipclone();
	twinip_rx_handler = NULL;
	twinip_tx_handler = NULL;
}

static int __init init(void)
{
	create_proc_ipclone();
#ifdef CONFIG_DRIVERLEVEL_REAL_IPCLONE_MODULE
        twinip_rx_handler = twinip_rx;
        twinip_tx_handler = twinip_tx;
#endif
	return 0;
}

module_init(init);
module_exit(fini);

