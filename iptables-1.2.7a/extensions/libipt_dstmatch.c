#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include <iptables.h>
#include <linux/netfilter_ipv4/ipt_dstmatch.h>

/* Function which prints out usage message. */
static void
help(void)
{
	printf(
"Destination IP address  v%s options:\n"
"[!] --name  name       IP pool filename\n"
"\n",
IPTABLES_VERSION);
}

static struct option opts[] = {
	{ "name", 1, 0, '1' },
	{0}
};

/* Initialize the match. */
static void
init(struct ipt_entry_match *m, unsigned int *nfcache)
{
	/* Can't cache this. */
	*nfcache |= NFC_UNKNOWN;
}

struct dstmatch_entry
{
	char sip[16];
	char eip[16];
};
#define MAX_ENTRY 	1000
static struct dstmatch_entry dmentry[MAX_ENTRY];

static int compare_ip(struct dstmatch_entry *e1, struct dstmatch_entry *e2)
{
	unsigned int ip1, ip2;

	ip1 = ntohl(inet_addr(e1->sip));
	ip2 = ntohl(inet_addr(e2->sip));

	return ((ip1 == ip2) ? 0 : (ip1 > ip2) ? 1 : -1);

}

static int sort_dstmatch(char *name)
{
	FILE *fp;
	char fname[128], fbuf[128], buffer[128], *ipstr;
	int i, idx = 0;
	
	sprintf(fname,"/etc/dstmatch/%s", name);
	fp = fopen(fname, "r");
	if (fp)
	{
		memset(&dmentry[0], 0, sizeof(struct dstmatch_entry)*MAX_ENTRY);
		while (fgets(fbuf, 128, fp))
		{
			ipstr = strchr(fbuf, '\r');
			if (ipstr) *ipstr = 0x0;
			ipstr = strchr(fbuf, '\n');
			if (ipstr) *ipstr = 0x0;
			ipstr = strchr(fbuf, ' ');
			if (ipstr) *ipstr = 0x0;

			strcpy(buffer, fbuf);
			ipstr = strtok(buffer, "~");

			if (!ipstr || !dotted_to_addr(ipstr))
				continue;

			strcpy(dmentry[idx].sip, ipstr);

			ipstr = strtok(NULL, "~");

			if (!ipstr)
				strcpy(dmentry[idx].eip, dmentry[idx].sip);
			else if (dotted_to_addr(ipstr))
				strcpy(dmentry[idx].eip, ipstr);
			else
				continue;

			if (++idx >= MAX_ENTRY) break;	
		}	
		fclose(fp);

		qsort(&dmentry[0], idx, sizeof(struct dstmatch_entry), (const void *)compare_ip);

		fp = fopen(fname, "w");
		if (fp)
		{
			for (i=0; i<idx; i++)
				fprintf(fp, "%s~%s\n", dmentry[i].sip, dmentry[i].eip);
			fclose(fp);
		}

		return 1;
	}

	return 0;
}

static void
parse_string(const unsigned char *s, struct ipt_dstmatch_info *info)
{
	if (strlen(s) <= IPT_DSTMATCH_NAME_LEN) strcpy(info->name, s);
	else exit_error(PARAMETER_PROBLEM, "STRING too long `%s'", s);
}

/* Function which parses command options; returns true if it
   ate an option */
static int
parse(int c, char **argv, int invert, unsigned int *flags,
      const struct ipt_entry *entry,
      unsigned int *nfcache,
      struct ipt_entry_match **match)
{
	struct ipt_dstmatch_info *dstmatchinfo = (struct ipt_dstmatch_info *)(*match)->data;

	switch (c) {
	case '1':
		check_inverse(optarg, &invert, &optind, 0);
		parse_string(argv[optind-1], dstmatchinfo);
		if (invert)
			dstmatchinfo->invert = 1;
		dstmatchinfo->options = 0;
		*flags = 1;
		if (!strcmp(argv[3], "-I") || !strcmp(argv[3], "-A"))
			return (sort_dstmatch(dstmatchinfo->name));
		else
			return 1;

	default:
		return 0;
	}
	return 1;
}

static void
print_dstmatch(char *name , int invert, int numeric)
{
	if (invert)
		fputc('!', stdout);
	printf("%s ", name);
}

static void
final_check(unsigned int flags)
{
	if (!flags)
		exit_error(PARAMETER_PROBLEM,
			   "DSTIP match: You must specify `--name'");
}

/* Prints out the matchinfo. */
static void
print(const struct ipt_ip *ip,
      const struct ipt_entry_match *match,
      int numeric)
{
	printf("dstmatch ");
	print_dstmatch(((struct ipt_dstmatch_info *)match->data)->name,
		  ((struct ipt_dstmatch_info *)match->data)->invert, 0);
}

/* Saves the union ipt_matchinfo in parsable form to stdout. */
static void
save(const struct ipt_ip *ip, const struct ipt_entry_match *match)
{
	printf("--name ");
	print_dstmatch(((struct ipt_dstmatch_info *)match->data)->name,
		  ((struct ipt_dstmatch_info *)match->data)->invert, 0);
}

static
struct iptables_match dstmatch
= { NULL,
    "dstmatch",
    IPTABLES_VERSION,
    IPT_ALIGN(sizeof(struct ipt_dstmatch_info)),
    IPT_ALIGN(sizeof(struct ipt_dstmatch_info)),
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
	register_match(&dstmatch);
}
