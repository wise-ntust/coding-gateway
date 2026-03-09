#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "transport.h"
#include "common.h"

struct path_sock {
    int               fd;
    struct sockaddr_in remote;
    int               enabled;
};

struct transport_ctx {
    struct path_sock  paths[MAX_PATHS];
    int               path_count;
    int               recv_fd;
};

struct transport_ctx *transport_init(const struct gateway_config *cfg,
                                     uint16_t listen_port)
{
    struct transport_ctx *ctx;
    int i;

    ctx = (struct transport_ctx *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->recv_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->recv_fd < 0) {
        LOG_ERR("recv socket() failed");
        free(ctx);
        return NULL;
    }

    {
        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family      = AF_INET;
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        bind_addr.sin_port        = htons(listen_port);
        if (bind(ctx->recv_fd, (struct sockaddr *)&bind_addr,
                 sizeof(bind_addr)) < 0) {
            LOG_ERR("bind on port %u failed", (unsigned)listen_port);
            close(ctx->recv_fd);
            free(ctx);
            return NULL;
        }
    }

    ctx->path_count = cfg->path_count;
    for (i = 0; i < cfg->path_count; i++) {
        const struct path_config *p = &cfg->paths[i];
        ctx->paths[i].enabled = p->enabled ? 1 : 0;
        ctx->paths[i].fd      = -1;
        if (!p->enabled) continue;

        ctx->paths[i].fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (ctx->paths[i].fd < 0) {
            LOG_ERR("send socket() failed for path %s", p->name);
            continue;
        }
        ctx->paths[i].remote.sin_family = AF_INET;
        ctx->paths[i].remote.sin_port   = htons(p->remote_port);
        if (inet_pton(AF_INET, p->remote_ip,
                      &ctx->paths[i].remote.sin_addr) != 1) {
            LOG_ERR("invalid remote_ip for path %s", p->name);
            close(ctx->paths[i].fd);
            ctx->paths[i].fd = -1;
        }
    }
    return ctx;
}

void transport_free(struct transport_ctx *ctx)
{
    int i;
    if (!ctx) return;
    if (ctx->recv_fd >= 0) close(ctx->recv_fd);
    for (i = 0; i < ctx->path_count; i++)
        if (ctx->paths[i].fd >= 0) close(ctx->paths[i].fd);
    free(ctx);
}

static int send_buf(struct transport_ctx *ctx, int path_idx,
                    const void *buf, size_t len)
{
    struct path_sock *ps = &ctx->paths[path_idx];
    ssize_t n;
    if (!ps->enabled || ps->fd < 0) return -1;
    n = sendto(ps->fd, buf, len, 0,
               (const struct sockaddr *)&ps->remote, sizeof(ps->remote));
    if (n < 0)
        LOG_WARN("sendto path %d: %s", path_idx, strerror(errno));
    return (n == (ssize_t)len) ? 0 : -1;
}

int transport_send_shard(struct transport_ctx *ctx,
                         int path_idx,
                         uint32_t block_id,
                         uint8_t shard_idx, uint8_t k, uint8_t n,
                         const struct shard *s)
{
    uint8_t buf[WIRE_HDR_SIZE + MAX_K + MAX_PAYLOAD];
    struct wire_header *hdr = (struct wire_header *)buf;
    size_t total;

    hdr->magic       = htons(WIRE_MAGIC);
    hdr->version     = WIRE_VERSION;
    hdr->type        = TYPE_DATA;
    hdr->block_id    = htonl(block_id);
    hdr->shard_idx   = shard_idx;
    hdr->k           = k;
    hdr->n           = n;
    hdr->reserved    = 0;
    hdr->payload_len = htons(s->len);

    memcpy(buf + WIRE_HDR_SIZE, s->coeffs, (size_t)k);
    memcpy(buf + WIRE_HDR_SIZE + k, s->data, s->len);
    total = (size_t)(WIRE_HDR_SIZE + k) + s->len;
    return send_buf(ctx, path_idx, buf, total);
}

int transport_send_probe(struct transport_ctx *ctx, int path_idx,
                          uint64_t timestamp_us)
{
    uint8_t buf[WIRE_HDR_SIZE + 8];
    struct wire_header *hdr = (struct wire_header *)buf;
    uint32_t hi, lo;

    memset(hdr, 0, WIRE_HDR_SIZE);
    hdr->magic   = htons(WIRE_MAGIC);
    hdr->version = WIRE_VERSION;
    hdr->type    = TYPE_PROBE;

    hi = htonl((uint32_t)(timestamp_us >> 32));
    lo = htonl((uint32_t)(timestamp_us & 0xFFFFFFFFu));
    memcpy(buf + WIRE_HDR_SIZE,     &hi, 4);
    memcpy(buf + WIRE_HDR_SIZE + 4, &lo, 4);
    return send_buf(ctx, path_idx, buf, sizeof(buf));
}

int transport_fill_fdset(struct transport_ctx *ctx, fd_set *rfds)
{
    FD_SET(ctx->recv_fd, rfds);
    return ctx->recv_fd + 1;
}

int transport_recv(struct transport_ctx *ctx, const fd_set *rfds,
                   struct wire_header *hdr,
                   struct shard *shard_out,
                   uint64_t *probe_ts_out,
                   int *path_idx)
{
    uint8_t buf[WIRE_HDR_SIZE + MAX_K + MAX_PAYLOAD];
    ssize_t n;

    if (!FD_ISSET(ctx->recv_fd, rfds)) return -1;

    n = recv(ctx->recv_fd, buf, sizeof(buf), 0);
    if (n < WIRE_HDR_SIZE) return -1;

    memcpy(hdr, buf, WIRE_HDR_SIZE);
    if (ntohs(hdr->magic) != WIRE_MAGIC) return -1;

    hdr->block_id    = ntohl(hdr->block_id);
    hdr->payload_len = ntohs(hdr->payload_len);
    *path_idx = 0;

    if (hdr->type == TYPE_DATA) {
        if (hdr->k > MAX_K)           return -1;
        if (hdr->payload_len > MAX_PAYLOAD) return -1;
        if (n < (ssize_t)(WIRE_HDR_SIZE + (int)hdr->k + (int)hdr->payload_len)) return -1;
        memcpy(shard_out->coeffs, buf + WIRE_HDR_SIZE, hdr->k);
        memcpy(shard_out->data,   buf + WIRE_HDR_SIZE + hdr->k,
               hdr->payload_len);
        shard_out->len = hdr->payload_len;
        return TYPE_DATA;
    }
    if (hdr->type == TYPE_PROBE || hdr->type == TYPE_PROBE_ECHO) {
        if (n < (ssize_t)(WIRE_HDR_SIZE + 8)) return -1;  /* truncated probe */
        if (probe_ts_out) {
            uint32_t hi, lo;
            memcpy(&hi, buf + WIRE_HDR_SIZE,     4);
            memcpy(&lo, buf + WIRE_HDR_SIZE + 4, 4);
            *probe_ts_out = ((uint64_t)ntohl(hi) << 32) |
                             (uint64_t)ntohl(lo);
        }
        return (int)hdr->type;
    }
    return -1;
}
