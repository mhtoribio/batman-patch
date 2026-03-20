#include <errno.h>
#include <inttypes.h>
#include <linux/if_ether.h>
#include <net/if.h>
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/batman_adv.h>

struct request_ctx {
    int err;
    bool done;
};

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s set <mesh-ifname> <hard-ifname> <neighbor-mac> <throughput-100kbps>\n"
            "  %s del <mesh-ifname> <hard-ifname> <neighbor-mac>\n"
            "  %s dump <mesh-ifname> [hard-ifname]\n"
            "\n"
            "Examples:\n"
            "  %s set bat0 veth0 02:00:00:00:00:02 50\n"
            "  %s del bat0 veth0 02:00:00:00:00:02\n"
            "  %s dump bat0\n"
            "  %s dump bat0 veth0\n",
            prog, prog, prog, prog, prog, prog, prog);
}

static int mac_from_string(const char *src, uint8_t mac[ETH_ALEN])
{
    unsigned int bytes[ETH_ALEN];
    int ret;

    ret = sscanf(src, "%2x:%2x:%2x:%2x:%2x:%2x",
                 &bytes[0], &bytes[1], &bytes[2],
                 &bytes[3], &bytes[4], &bytes[5]);
    if (ret != ETH_ALEN)
        return -EINVAL;

    for (size_t i = 0; i < ETH_ALEN; i++) {
        if (bytes[i] > 0xff)
            return -EINVAL;
        mac[i] = (uint8_t)bytes[i];
    }

    return 0;
}

static const char *mac_to_string(const uint8_t mac[ETH_ALEN], char *buf,
                                 size_t len)
{
    snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

static unsigned int ifindex_from_name(const char *ifname)
{
    unsigned int ifindex;

    ifindex = if_nametoindex(ifname);
    if (ifindex == 0)
        fprintf(stderr, "Interface not found: %s\n", ifname);

    return ifindex;
}

static int parse_u32(const char *src, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(src, &end, 10);
    if (errno != 0 || end == src || *end != '\0' || parsed > UINT32_MAX)
        return -EINVAL;

    *value = (uint32_t)parsed;
    return 0;
}

static int ack_handler(struct nl_msg *msg, void *arg)
{
    struct request_ctx *ctx = arg;

    (void)msg;
    ctx->done = true;
    ctx->err = 0;
    return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg)
{
    struct request_ctx *ctx = arg;

    (void)msg;
    ctx->done = true;
    ctx->err = 0;
    return NL_STOP;
}

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err,
                         void *arg)
{
    struct request_ctx *ctx = arg;

    (void)nla;
    ctx->done = true;
    ctx->err = err->error;
    return NL_STOP;
}

static int no_seq_check(struct nl_msg *msg, void *arg)
{
    (void)msg;
    (void)arg;
    return NL_OK;
}

static int ignore_valid(struct nl_msg *msg, void *arg)
{
    (void)msg;
    (void)arg;
    return NL_OK;
}

static int print_override_entry(struct nl_msg *msg, void *arg)
{
    static struct nla_policy policy[NUM_BATADV_ATTR] = {
        [BATADV_ATTR_MESH_IFINDEX] = { .type = NLA_U32 },
        [BATADV_ATTR_MESH_IFNAME] = { .type = NLA_STRING },
        [BATADV_ATTR_HARD_IFINDEX] = { .type = NLA_U32 },
        [BATADV_ATTR_HARD_IFNAME] = { .type = NLA_STRING },
        [BATADV_ATTR_NEIGH_ADDRESS] = {
            .minlen = ETH_ALEN,
            .maxlen = ETH_ALEN
        },
        [BATADV_ATTR_THROUGHPUT_OVERRIDE] = { .type = NLA_U32 },
    };
    struct nlattr *attrs[NUM_BATADV_ATTR] = {};
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    const uint8_t *mac;
    uint32_t mesh_ifindex;
    uint32_t hard_ifindex;
    uint32_t throughput;
    char macbuf[18];
    const char *mesh_ifname = "-";
    const char *hard_ifname = "-";
    double mbps;
    int err;

    (void)arg;

    err = nla_parse(attrs, BATADV_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
                    genlmsg_attrlen(gnlh, 0), policy);
    if (err < 0) {
        fprintf(stderr, "Failed to parse dump entry: %s\n", nl_geterror(err));
        return NL_STOP;
    }

    if (!attrs[BATADV_ATTR_MESH_IFINDEX] ||
            !attrs[BATADV_ATTR_HARD_IFINDEX] ||
            !attrs[BATADV_ATTR_NEIGH_ADDRESS] ||
            !attrs[BATADV_ATTR_THROUGHPUT_OVERRIDE]) {
        fprintf(stderr, "Dump entry missing required attributes\n");
        return NL_STOP;
    }

    mesh_ifindex = nla_get_u32(attrs[BATADV_ATTR_MESH_IFINDEX]);
    hard_ifindex = nla_get_u32(attrs[BATADV_ATTR_HARD_IFINDEX]);
    throughput = nla_get_u32(attrs[BATADV_ATTR_THROUGHPUT_OVERRIDE]);
    mac = nla_data(attrs[BATADV_ATTR_NEIGH_ADDRESS]);

    if (attrs[BATADV_ATTR_MESH_IFNAME])
        mesh_ifname = nla_get_string(attrs[BATADV_ATTR_MESH_IFNAME]);

    if (attrs[BATADV_ATTR_HARD_IFNAME])
        hard_ifname = nla_get_string(attrs[BATADV_ATTR_HARD_IFNAME]);

    mbps = throughput / 10.0;
    printf("%s(%u) %s(%u) %s %" PRIu32 " %.1fMbps\n",
           mesh_ifname, mesh_ifindex,
           hard_ifname, hard_ifindex,
           mac_to_string(mac, macbuf, sizeof(macbuf)),
           throughput, mbps);

    return NL_OK;
}

static int send_and_process(struct nl_sock *sock, struct nl_msg *msg,
                            nl_recvmsg_msg_cb_t valid_cb, void *valid_arg)
{
    struct request_ctx ctx = {
        .err = 1,
        .done = false,
    };
    int err;

    nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, valid_cb, valid_arg);
    nl_socket_modify_cb(sock, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ctx);
    nl_socket_modify_cb(sock, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ctx);
    nl_socket_modify_cb(sock, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, no_seq_check, NULL);
    nl_socket_modify_err_cb(sock, NL_CB_CUSTOM, error_handler, &ctx);

    err = nl_send_auto(sock, msg);
    if (err < 0)
        return err;

    while (!ctx.done) {
        err = nl_recvmsgs_default(sock);
        if (err < 0)
            return err;
    }

    return ctx.err;
}

static int open_batadv_socket(struct nl_sock **psock, int *pfamily)
{
    struct nl_sock *sock;
    int family;
    int err;

    sock = nl_socket_alloc();
    if (!sock)
        return -ENOMEM;

    err = genl_connect(sock);
    if (err < 0) {
        nl_socket_free(sock);
        return err;
    }

    family = genl_ctrl_resolve(sock, BATADV_NL_NAME);
    if (family < 0) {
        nl_socket_free(sock);
        return family;
    }

    *psock = sock;
    *pfamily = family;
    return 0;
}

static int do_set(int family, struct nl_sock *sock, unsigned int mesh_ifindex,
                  unsigned int hard_ifindex, const uint8_t mac[ETH_ALEN],
                  uint32_t throughput)
{
    struct nl_msg *msg;
    int err;

    msg = nlmsg_alloc();
    if (!msg)
        return -ENOMEM;

    if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, family, 0, 0,
                     BATADV_CMD_SET_NEIGH_THROUGHPUT_OVERRIDE, 1)) {
        nlmsg_free(msg);
        return -ENOBUFS;
    }

    err = nla_put_u32(msg, BATADV_ATTR_MESH_IFINDEX, mesh_ifindex);
    if (err < 0)
        goto out;

    err = nla_put_u32(msg, BATADV_ATTR_HARD_IFINDEX, hard_ifindex);
    if (err < 0)
        goto out;

    err = nla_put(msg, BATADV_ATTR_NEIGH_ADDRESS, ETH_ALEN, mac);
    if (err < 0)
        goto out;

    err = nla_put_u32(msg, BATADV_ATTR_THROUGHPUT_OVERRIDE, throughput);
    if (err < 0)
        goto out;

    err = send_and_process(sock, msg, ignore_valid, NULL);

out:
    nlmsg_free(msg);
    return err;
}

static int do_dump(int family, struct nl_sock *sock, unsigned int mesh_ifindex,
                   unsigned int hard_ifindex)
{
    struct nl_msg *msg;
    int err;

    msg = nlmsg_alloc();
    if (!msg)
        return -ENOMEM;

    if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, family, 0, NLM_F_DUMP,
                     BATADV_CMD_GET_NEIGH_THROUGHPUT_OVERRIDES, 1)) {
        nlmsg_free(msg);
        return -ENOBUFS;
    }

    err = nla_put_u32(msg, BATADV_ATTR_MESH_IFINDEX, mesh_ifindex);
    if (err < 0)
        goto out;

    if (hard_ifindex != 0) {
        err = nla_put_u32(msg, BATADV_ATTR_HARD_IFINDEX, hard_ifindex);
        if (err < 0)
            goto out;
    }

    err = send_and_process(sock, msg, print_override_entry, NULL);

out:
    nlmsg_free(msg);
    return err;
}

int main(int argc, char **argv)
{
    struct nl_sock *sock = NULL;
    uint8_t mac[ETH_ALEN];
    unsigned int mesh_ifindex;
    unsigned int hard_ifindex = 0;
    uint32_t throughput = 0;
    int family;
    int err;

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    err = open_batadv_socket(&sock, &family);
    if (err < 0) {
        fprintf(stderr, "Failed to open batadv netlink socket: %s\n",
                nl_geterror(err));
        return 1;
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc != 6) {
            usage(argv[0]);
            err = -EINVAL;
            goto out;
        }

        mesh_ifindex = ifindex_from_name(argv[2]);
        hard_ifindex = ifindex_from_name(argv[3]);
        if (mesh_ifindex == 0 || hard_ifindex == 0) {
            err = -ENODEV;
            goto out;
        }

        err = mac_from_string(argv[4], mac);
        if (err < 0) {
            fprintf(stderr, "Invalid MAC address: %s\n", argv[4]);
            goto out;
        }

        err = parse_u32(argv[5], &throughput);
        if (err < 0) {
            fprintf(stderr, "Invalid throughput value: %s\n", argv[5]);
            goto out;
        }

        err = do_set(family, sock, mesh_ifindex, hard_ifindex, mac,
                     throughput);
        if (err == 0)
            printf("Set override on %s/%s for %s to %" PRIu32 " (%.1fMbps)\n",
                   argv[2], argv[3], argv[4], throughput,
                   throughput / 10.0);
    } else if (strcmp(argv[1], "del") == 0) {
        if (argc != 5) {
            usage(argv[0]);
            err = -EINVAL;
            goto out;
        }

        mesh_ifindex = ifindex_from_name(argv[2]);
        hard_ifindex = ifindex_from_name(argv[3]);
        if (mesh_ifindex == 0 || hard_ifindex == 0) {
            err = -ENODEV;
            goto out;
        }

        err = mac_from_string(argv[4], mac);
        if (err < 0) {
            fprintf(stderr, "Invalid MAC address: %s\n", argv[4]);
            goto out;
        }

        err = do_set(family, sock, mesh_ifindex, hard_ifindex, mac, 0);
        if (err == 0)
            printf("Deleted override on %s/%s for %s\n",
                   argv[2], argv[3], argv[4]);
    } else if (strcmp(argv[1], "dump") == 0) {
        if (argc != 3 && argc != 4) {
            usage(argv[0]);
            err = -EINVAL;
            goto out;
        }

        mesh_ifindex = ifindex_from_name(argv[2]);
        if (mesh_ifindex == 0) {
            err = -ENODEV;
            goto out;
        }

        if (argc == 4) {
            hard_ifindex = ifindex_from_name(argv[3]);
            if (hard_ifindex == 0) {
                err = -ENODEV;
                goto out;
            }
        }

        err = do_dump(family, sock, mesh_ifindex, hard_ifindex);
    } else {
        usage(argv[0]);
        err = -EINVAL;
    }

out:
    if (err < 0) {
        fprintf(stderr, "Error: %s\n", nl_geterror(err));
        nl_socket_free(sock);
        return 1;
    }

    nl_socket_free(sock);
    return 0;
}
