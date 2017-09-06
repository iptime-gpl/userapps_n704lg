#ifndef _IPT_HAIRPIN_H_target
#define _IPT_HAIRPIN_H_target

enum ipt_hairpin_dir
{
	IPT_HAIRPIN_IN = 1,
	IPT_HAIRPIN_OUT = 2
};

struct ipt_hairpin_info {
	enum ipt_hairpin_dir dir;
	unsigned int ip;
};

#endif /*_IPT_HAIRPIN_H_target*/
