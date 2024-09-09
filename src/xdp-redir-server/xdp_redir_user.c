#include <stdio.h>
#include <stdlib.h>
#include <net/if.h>
#include <unistd.h>
#include <errno.h>

/* for parsing options */
#include <getopt.h>

/* for convert IPv4 address between text and binary form */
#include <arpa/inet.h>

/* use libbpf-devel and libxdp-devel*/
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <xdp/libxdp.h>

/* use json-c for parsing json*/
#include <json-c/json.h>

/* defines: struct server_info */
#include "common_kern_user.h"

static const struct option long_options[] = {
	{"dev",         required_argument,	NULL, 'd' },

	{"skb-mode",    no_argument,		NULL, 'S' },

	{"native-mode", no_argument,		NULL, 'N' },

	{"auto-mode",   no_argument,		NULL, 'A' },

	{"unload",      required_argument,	NULL, 'U' },

    {"progname",    required_argument,  NULL, 'p' },

	{0, 0, NULL,  0 }
};

/* feature toggles for synchronization */
enum Sync_Features{
    SYNC_JSON
};

struct config {
    enum xdp_attach_mode attach_mode;
    enum Sync_Features sync_toggle;
    int interval;   /* poll stats interval */
	int ifindex; /* interface index */
	char *ifname;
	bool do_unload;
	__u32 prog_id;
	char obj_filename[512];
	char progname[32];
    char section_name[32];
};

void parse_cmdline_args(int argc, char **argv, struct config *cfg)
{
    int longindex = 0;
    int opt;
    /* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "d:SNAU:p:",
				  long_options, &longindex)) != -1) {
        //printf("Successfully parse option element: %c\n", (char)opt);
		switch (opt) {
		case 'd': /* --dev */
            cfg->ifname = optarg;
            cfg->ifindex = if_nametoindex(optarg);
            if (cfg->ifindex <= 0) {
                printf("get ifname from interface name failed\n");
            }
            break;
        case 'S': /* --skb-mode */
            cfg->attach_mode = XDP_MODE_SKB;
            break;
        case 'N': /* --native-mode */
            cfg->attach_mode = XDP_MODE_NATIVE;
            break;
        case 'A': /* --auto-mode */
            cfg->attach_mode = XDP_MODE_UNSPEC;
			break;
        case 'U': /* --unload */
            cfg->do_unload = true;
			cfg->prog_id = atoi(optarg);
			break;
        case 'p': /* --progname */
            strncpy((char *)&cfg->progname, optarg, sizeof(cfg->progname));
            break;
        default:
            exit(EXIT_FAILURE);
            break;
        }
    }   
}

int do_unload(struct config *cfg)
{
	struct xdp_multiprog *mp = NULL;
	enum xdp_attach_mode mode;
	int err = EXIT_FAILURE;
	// DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);

	mp = xdp_multiprog__get_from_ifindex(cfg->ifindex);
	if (libxdp_get_error(mp)) {
		fprintf(stderr, "Unable to get xdp_dispatcher program: %s\n",
			strerror(errno));
		goto out;
	} else if (!mp) {
		fprintf(stderr, "No XDP program loaded on %s\n", cfg->ifname);
		mp = NULL;
		goto out;
	}

    /* unload one prog with pid */
	struct xdp_program *prog = NULL;

	while ((prog = xdp_multiprog__next_prog(prog, mp))) {
		if (xdp_program__id(prog) == cfg->prog_id) {
			mode = xdp_multiprog__attach_mode(mp);
			goto found;
		}
	}

	if (xdp_multiprog__is_legacy(mp)) {
		prog = xdp_multiprog__main_prog(mp);
		if (xdp_program__id(prog) == cfg->prog_id) {
			mode = xdp_multiprog__attach_mode(mp);
			goto found;
		}
	}

	prog = xdp_multiprog__hw_prog(mp);
	if (xdp_program__id(prog) == cfg->prog_id) {
		mode = XDP_MODE_HW;
		goto found;
	}

	printf("Program with ID %u not loaded on %s\n",
		cfg->prog_id, cfg->ifname);
	err = -ENOENT;
	goto out;

found:
	printf("Detaching XDP program with ID %u from %s\n",
			 xdp_program__id(prog), cfg->ifname);
	err = xdp_program__detach(prog, cfg->ifindex, mode, 0);
	if (err) {
		fprintf(stderr, "Unable to detach XDP program: %s\n",
			strerror(-err));
		goto out;
}
	
out:
	xdp_multiprog__close(mp);
	return err ? EXIT_FAILURE : EXIT_SUCCESS;
}

void parse_mac(const char *buf, __u8 *mac)
{
    int values[ETH_ALEN];
    int i;

    if( ETH_ALEN == sscanf( buf, "%x:%x:%x:%x:%x:%x",
    &values[0], &values[1], &values[2],
    &values[3], &values[4], &values[5] ) )
    {
        /* convert to __u8 */
        for( i = 0; i < 6; ++i )
            mac[i] = (__u8) values[i];
    }
    else{
        fprintf(stderr, "could not parse %s\n", buf);
    }
}

int parse_server_info(struct server_info *dst, enum Sync_Features sync_toggle)
{
    int retval = 0;
    switch(sync_toggle) {
    case SYNC_JSON:
        FILE *fp;
        char buf[512];
        static const char *svinfo_filename = "server_info.json";

        struct json_object *parsed_json;
        struct json_object *ipv4;
        struct json_object *mac;

        fp = fopen(svinfo_filename, "r");
        fread(buf, 512, 1, fp);
        fclose(fp);

        parsed_json = json_tokener_parse(buf);
        json_object_object_get_ex(parsed_json, "ipv4", &ipv4);
        json_object_object_get_ex(parsed_json, "mac", &mac);

        /* assign & convert address form*/
        if(inet_pton(AF_INET, json_object_get_string(ipv4), &(dst->saddr)) != 1)
            retval = 1;
        parse_mac(json_object_get_string(mac), dst->dmac);
        break;
    default:
        retval = 1;
    }
    return retval;
}

static void poll_stats(int map_fd, struct config *cfg)
{
    struct server_info *sv;
    __u32 key = 0;
    
    while (1) {
        sleep(cfg->interval);
        /* check if server info json changed */
        if(parse_server_info(sv, cfg->sync_toggle) != 0){
            printf("parse server info failed\n");
            exit(EXIT_FAILURE);
        }
        bpf_map_update_elem(map_fd, &key, sv, BPF_ANY);
    }
}

int main(int argc, char *argv[])
{
    int map_fd, err;
    struct xdp_program *prog = NULL;
    struct bpf_object *bpf_obj;
    char errmsg[1024];

    struct config cfg = {
		.attach_mode = XDP_MODE_UNSPEC,
        .sync_toggle = SYNC_JSON,
        .interval = 1,
		.ifindex   = -1,
		.do_unload = false,
        .obj_filename = "xdp_redir_kern.o",
        .section_name = "xdp",
	};

    parse_cmdline_args(argc, argv, &cfg);

    /* Required option */
    if(cfg.ifindex <= 0) {
        fprintf(stderr, "ERR: required option --dev missing\n");
        return EXIT_FAILURE;
    }

    /* Unload a program by prog_id */
    if (cfg.do_unload) {
        printf("Unload program id: %d...\n", cfg.prog_id);
        err = do_unload(&cfg);
        if (err) {
			libxdp_strerror(err, errmsg, sizeof(errmsg));
			fprintf(stderr, "Couldn't unload XDP program %d: %s\n",
				cfg.prog_id, errmsg);
			return err;
		}

		printf("Success: Unloading XDP prog id: %d\n", cfg.prog_id);
		return 0;
    }
    /* Open a BPF object file */
    DECLARE_LIBBPF_OPTS(bpf_object_open_opts, bpf_opts);
    bpf_obj = bpf_object__open_file(cfg.obj_filename, &bpf_opts);
    err = libbpf_get_error(bpf_obj);
    if (err) {
        libxdp_strerror(err, errmsg, sizeof(errmsg));
        fprintf(stderr, "Couldn't open BPF object file %s: %s\n",
                cfg.obj_filename, errmsg);
        return err;
    }

    /* Load the XDP program */
    DECLARE_LIBXDP_OPTS(xdp_program_opts, xdp_opts,
                            .obj = bpf_obj,
                            .prog_name = cfg.progname);
	prog = xdp_program__create(&xdp_opts);
	err = libxdp_get_error(prog);
	if (err) {
		libxdp_strerror(err, errmsg, sizeof(errmsg));
		fprintf(stderr, "ERR: loading program %s: %s\n", cfg.progname, errmsg);
		return err;
	}

    /* Attach the XDP program to the net device XDP hook */
    err = xdp_program__attach(prog, cfg.ifindex, cfg.attach_mode, 0);
    if (err) {
        printf("Error: Set xdp fd on %d failed\n", cfg.ifindex);
        return err;
    }

    /* Find the map fd from bpf object */
    map_fd = bpf_object__find_map_fd_by_name(bpf_obj, "servers");
    if (map_fd < 0) {
        printf("Error: cannot find map by name\n");
        return map_fd;
    }

    poll_stats(map_fd, &cfg);
}