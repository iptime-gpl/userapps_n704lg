/* Port-hairpining target. 
 *
 * Copyright (C) 2003, CyberTAN Corporation
 * All Rights Reserved.
 */

/* Shared library add-on to iptables to add port-hairpin support. */

#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <iptables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ipt_HAIRPIN.h>

/* Function which prints out usage message. */
static void
help(void)
{
	printf(
"HAIRPIN options:\n"
" --dir <in|out:gateway_local_ip>"  
"\n");
}

static struct option opts[] = {
	{ "dir", 1, 0, '1' },
	{ 0 }
};

/* Initialize the target. */
static void
init(struct ipt_entry_target *t, unsigned int *nfcache)
{
	/* Can't cache this */
	*nfcache |= NFC_UNKNOWN;
}

/* Function which parses command options; returns true if it
   ate an option */
static int
parse(int c, char **argv, int invert, unsigned int *flags,
      const struct ipt_entry *entry,
      struct ipt_entry_target **target)
{
	struct ipt_hairpin_info *info = (struct ipt_hairpin_info *)(*target)->data;
	struct in_addr *ip;

	switch (c) {
	case '1':
		if (!strcasecmp(optarg, "in"))
		{
			info->dir = IPT_HAIRPIN_IN;
			info->ip = 0;
		}
		else if (!strncasecmp(optarg, "out", 3))
		{
			char *colon;

			info->dir = IPT_HAIRPIN_OUT;
			colon = strchr(optarg, ':');
			ip = dotted_to_addr(colon+1);
			if (!ip)
			{
				exit_error(PARAMETER_PROBLEM, "Bad IP address `%s'\n", optarg);
				return 0;
			}
			info->ip = ip->s_addr;
		}
		break;
	default:
		exit_error(PARAMETER_PROBLEM, "Invalid Option !!!\n");
		return 0;
	}
	
	return 1;
}

/* Final check; don't care. */
static void final_check(unsigned int flags)
{
}

static void print_ipaddr(unsigned int ipaddr)
{
	struct in_addr a;
	a.s_addr = ipaddr;
	printf("%s", addr_to_dotted(&a));
}

/* Prints out the targinfo. */
static void
print(const struct ipt_ip *ip,
      const struct ipt_entry_target *target,
      int numeric)
{
	struct ipt_hairpin_info *info = (struct ipt_hairpin_info *)target->data;

	printf("HAIRPIN ");
	if (info->ip)
	{
		printf("out:");
		print_ipaddr(info->ip);
	}
	else
		printf("in");
}

/* Saves the union ipt_targinfo in parsable form to stdout. */
static void
save(const struct ipt_ip *ip, const struct ipt_entry_target *target)
{
	struct ipt_hairpin_info *info = (struct ipt_hairpin_info *)target->data;

	if (info->ip)
	{
		printf("--dir out:");
		print_ipaddr(info->ip);
	}
	else
		printf("--dir in ");
}

struct iptables_target hairpin
= { NULL,
    "HAIRPIN",
    IPTABLES_VERSION,
    IPT_ALIGN(sizeof(struct ipt_hairpin_info)),
    IPT_ALIGN(sizeof(struct ipt_hairpin_info)),
    &help,
    &init,
    &parse,
    &final_check,
    &print,
    &save,
    opts
};

void _init(void)
{
	register_target(&hairpin);
}
