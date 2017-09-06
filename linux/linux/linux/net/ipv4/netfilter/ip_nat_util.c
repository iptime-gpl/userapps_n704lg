#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/tcp.h>

#include <linux/module.h>

int str_to_u32( char *pbuf, u32 *pval )
{
        int n = 0;

        *pval = 0;
        while( pbuf[n] >= '0' && pbuf[n] <= '9' )
        {
                *pval = (*pval * 10) + (pbuf[n] - '0');
                n++;
        }

        return n;
}

u32 ipstr_to_u32 ( char *ipstr )
{
        u32 ipaddr = 0;
        int n = 0, i, tmp;

	while (isdigit(ipstr[n]) || ipstr[n] == '.')
        {
                if (ipstr[n] == '.')
                {
                        ipaddr = (ipaddr << 8) | tmp;
                        n++;
                }
                else
                {
                        i = str_to_u32(&ipstr[n], &tmp);
                        n += i;
                }
        }
        ipaddr = (ipaddr << 8) | tmp;

        return ipaddr;
}

int next_line( char **ppbuf, u32 *pbuflen, char **ppline, u32 *plinelen )
{
        char *pbuf = *ppbuf;
        u32 buflen = *pbuflen;
        u32 linelen = 0;

        do
        {
                while( *pbuf != '\r' && *pbuf != '\n' )
                {
                        if( buflen <= 1 )
                        {
                                return 0;
                        }
                        pbuf++;
                        linelen++;
                        buflen--;
                }

                if( buflen > 1 && *pbuf == '\r' && *(pbuf+1) == '\n' )
                {
                        pbuf++;
                        buflen--;
                }
                pbuf++;
                buflen--;
        }
        while( buflen > 0 && (*pbuf == ' ' || *pbuf == '\t') );

        *ppline = *ppbuf;
        *plinelen = linelen;
        *ppbuf = pbuf;
        *pbuflen = buflen;

        return linelen;
}

u32 get_skb_data( struct sk_buff **ppskb, char **ppdata, u32 *plen )
{
        struct iphdr   *iph = (*ppskb)->nh.iph;
        struct tcphdr  *th;
        struct udphdr  *uh;
        char *pbuf;
        char *pend;

	if (iph->protocol == IPPROTO_TCP)
	{
		th = (struct tcphdr *)((u8*)iph + (iph->ihl * 4));
        	pbuf = (char*)th + (th->doff * 4);
	}
	else
	{
		uh = (struct udphdr *)((u8*)iph + (iph->ihl * 4));
        	pbuf = (char*)uh + sizeof(struct udphdr);
	}

        pend = (*ppskb)->h.raw + (*ppskb)->len;
        *ppdata = pbuf;
        *plen = pend - pbuf;

        return (iph->saddr);
}

extern struct net_device *dev_get_by_name(const char *name);
int check_local_ip(u32 ipaddr)
{
        struct in_device *in_dev;
        struct in_ifaddr **ifap = NULL;
        struct in_ifaddr *ifa = NULL;
	struct net_device *dev;

//	printk("check_local_ip ipaddr -> %08x\n", ipaddr );
#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
	dev = dev_get_by_name("br0");
#else
	dev = dev_get_by_name("eth0");
#endif
	if(dev == NULL)
	{
		printk("check_local_ip:NO local interface exists\n");
		return 0;
	}

        in_dev = (struct in_device *)(dev->ip_ptr);
        if (in_dev == NULL)
                return 0;
        for (ifap = &in_dev->ifa_list; (ifa=*ifap) != NULL; ifap = &ifa->ifa_next)
        {
                //printk("ifaddr=%08x : %08x, mask %08x\n",ifa->ifa_address,ipaddr,ifa->ifa_mask);
                if (ipaddr == ifa->ifa_address)
                        return 1;
        }
        return 0;
}

int check_internal_subnet(u32 ipaddr)
{
        struct in_device *in_dev;
        struct in_ifaddr **ifap = NULL;
        struct in_ifaddr *ifa = NULL;
	struct net_device *dev;

#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
	dev = dev_get_by_name("br0");
#else
	dev = dev_get_by_name("eth0");
#endif
	
	if(dev == NULL )
	{
		printk("check_internal_subnet:NO local interface exists\n");
		return 0;
	}

        in_dev = (struct in_device *)(dev->ip_ptr);
        if (in_dev == NULL)
                return 0;

        for (ifap = &in_dev->ifa_list; (ifa=*ifap) != NULL; ifap = &ifa->ifa_next)
        {
                //printk("ifaddr=%08x : %08x, mask %08x\n",ifa->ifa_address,ipaddr,ifa->ifa_mask);
                if ((ipaddr & ifa->ifa_mask) == (ifa->ifa_address & ifa->ifa_mask))
                {
                        //printk("==> same subnet\n");
                        return (ifa->ifa_address & ifa->ifa_mask);
                }       
        }
        return 0;
}

#define xtod(c)         ((c) <= '9' ? '0' - (c) : 'a' - (c) - 10)
long atoi(register char *p)
{
        register long n;
        register int c, neg = 0;

        if (p == NULL)
                return 0;

        if (!isdigit(c = *p)) {
                while (isspace(c))
                        c = *++p;
                switch (c) {
                case '-':
                        neg++;
                case '+': /* fall-through */
                        c = *++p;
                }
                if (!isdigit(c)) 
                        return (0); 
        } 
        if (c == '0' && *(p + 1) == 'x') {
                p += 2;
                c = *p;
                n = xtod(c);
                while ((c = *++p) && isxdigit(c)) {
                        n *= 16; /* two steps to avoid unnecessary overflow */
                        n += xtod(c); /* accum neg to avoid surprises at MAX */
                }
        } else {
                n = '0' - c;
                while ((c = *++p) && isdigit(c)) {
                        n *= 10; /* two steps to avoid unnecessary overflow */
                        n += '0' - c; /* accum neg to avoid surprises at MAX */
                }
        }
        return (neg ? n : -n);
}


EXPORT_SYMBOL(get_skb_data);
EXPORT_SYMBOL(atoi);
EXPORT_SYMBOL(check_internal_subnet);
EXPORT_SYMBOL(ipstr_to_u32);


