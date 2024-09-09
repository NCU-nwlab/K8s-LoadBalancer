/* This common_kern_user.h is used by both kernel and
 * user space programs, for sharing common struct.
 */
#ifndef __COMMON_KERN_USER_H
#define __COMMON_KERN_USER_H

#include <linux/if_ether.h>

/* The data stored in the map */
struct server_info {
	__u32 saddr;
	__u8 dmac[ETH_ALEN];
};

#endif /* __COMMON_KERN_USER_H */