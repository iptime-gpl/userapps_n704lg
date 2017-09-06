/* Shared library add-on to iptables to add STAT target support. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#include <iptables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_STAT.h>

/* Function which prints out usage message. */
static void
help(void)
{
	printf(
"STAT target v%s options:\n"
"  --set-index value                   Set index value\n"
"\n",
IPTABLES_VERSION);
}

static struct option opts[] = {
	{ "set-index", 1, 0, '1' },
	{ 0 }
};

/* Initialize the target. */
static void
init(struct ipt_entry_target *t, unsigned int *nfcache)
{
}

/* Function which parses command options; returns true if it
   ate an option */
static int
parse(int c, char **argv, int invert, unsigned int *flags,
      const struct ipt_entry *entry,
      struct ipt_entry_target **target)
{
	struct ipt_stat_target_info *markinfo
		= (struct ipt_stat_target_info *)(*target)->data;

	switch (c) {
		char *end;
	case '1':
		markinfo->mark = strtoul(optarg, &end, 0);
		*flags = 1;
		break;

	default:
		*flags = 0;
		return 0;
	}

	return 1;
}

static void
final_check(unsigned int flags)
{
	if (!flags)
		exit_error(PARAMETER_PROBLEM,
		           "STAT target: Parameter --set-index is required");
}

static void
print_mark(unsigned long mark, int numeric)
{
	printf("0x%lx ", mark);
}

/* Prints out the targinfo. */
static void
print(const struct ipt_ip *ip,
      const struct ipt_entry_target *target,
      int numeric)
{
	const struct ipt_stat_target_info *markinfo =
		(const struct ipt_stat_target_info *)target->data;
	printf("STAT set ");
	print_mark(markinfo->mark, numeric);
}

/* Saves the union ipt_targinfo in parsable form to stdout. */
static void
save(const struct ipt_ip *ip, const struct ipt_entry_target *target)
{
	const struct ipt_stat_target_info *markinfo =
		(const struct ipt_stat_target_info *)target->data;

	printf("--set-index 0x%lx ", markinfo->mark);
}

struct iptables_target stat
= { NULL,
    "STAT",
    IPTABLES_VERSION,
    IPT_ALIGN(sizeof(struct ipt_stat_target_info)),
    IPT_ALIGN(sizeof(struct ipt_stat_target_info)),
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
	register_target(&stat);
}
