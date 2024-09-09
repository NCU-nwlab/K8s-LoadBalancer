#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <bpf/bpf_helpers.h>
#include <linux/atm.h>

#include "common_kern_user.h" /* defines: struct server_indo */

#define MAX_SERVERS 1

struct callback_ctx {
	unsigned short *buf;
	unsigned long *sum;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, struct server_info);
	__uint(max_entries, MAX_SERVERS);
}servers SEC(".maps");

/* These should be set by the user program */
// int pid;

static __always_inline struct server_info *get_server()
{
	struct server_info *sv;
	__u32 key = 0;
	sv = bpf_map_lookup_elem(&servers, &key);
	return sv;
}

static long callback(unsigned long *sum, unsigned short *buf, void *ctx)
{
	sum += *buf;
	buf++;
	return 0;
}

static __always_inline __u16 ip_checksum(unsigned short *buf, int bufsz)
{
	/* cannot use variable as the condition of while, or use bpf_loop helper function */
	unsigned long sum = 0;
	struct callback_ctx data = { .buf = buf, .sum = &sum };
	bpf_loop(bufsz/2, callback, &data, 0);

	if(bufsz%2 != 0){
		sum += *(unsigned char *)buf;
	}
	sum = (sum & 0xffff) + (sum >> 16);
    sum = (sum & 0xffff) + (sum >> 16);
	return ~sum;
}

static __always_inline int handle_ipv4(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	struct iphdr *iph;
	struct server_info *sv;

	__u32 off = 0;

	off += sizeof(struct ethhdr);
	iph = data + off;
	if (iph + 1 > data_end)
        return XDP_DROP;
	
	if(iph->protocol != IPPROTO_ICMP)
		return XDP_PASS;

	sv = get_server();
	if(!sv)
		return XDP_ABORTED;

	iph->daddr = sv->saddr;
	memcpy(eth->h_dest, sv->dmac, sizeof(eth->h_dest));

	iph->check = 0;
	iph->check = ip_checksum((__u16 *)iph, sizeof(struct iphdr));

	return XDP_TX;
}

SEC("xdp")
int xdp_redirect_func(struct xdp_md *ctx){
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	__u16 h_proto;
	
	if (eth + 1 > data_end)
		return XDP_DROP;

	h_proto = eth->h_proto;

	if (h_proto == htons(ETH_P_IP))
		return handle_ipv4(ctx);
	else
		return XDP_PASS;
}

SEC("xdp")
int xdp_pass_func(struct xdp_md *ctx)
{
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
