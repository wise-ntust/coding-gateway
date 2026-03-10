#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>
#include <stddef.h>
#include <sys/select.h>
#include "config.h"
#include "codec.h"

/* Wire protocol constants */
#define WIRE_MAGIC        0xC0DE
#define WIRE_VERSION      0x01
#define TYPE_DATA         0x01
#define TYPE_PROBE        0x02
#define TYPE_PROBE_ECHO   0x03
#define TYPE_NACK         0x04

/*
 * Fixed wire header (14 bytes), followed by k coefficient bytes,
 * followed by payload_len bytes of (coded) payload.
 *
 * All multi-byte fields are big-endian on the wire.
 */
struct wire_header {
    uint16_t magic;
    uint8_t  version;
    uint8_t  type;
    uint32_t block_id;
    uint8_t  shard_idx;
    uint8_t  k;
    uint8_t  n;
    uint8_t  reserved;
    uint16_t payload_len;
} __attribute__((packed));

#define WIRE_HDR_SIZE  ((int)sizeof(struct wire_header))  /* 14 bytes */

struct transport_ctx;

/*
 * Allocate and initialise transport context.
 * Opens one send socket per enabled path and one shared listen socket.
 * listen_port: UDP port this node binds to for incoming datagrams.
 * Returns NULL on error.
 */
struct transport_ctx *transport_init(const struct gateway_config *cfg,
                                     uint16_t listen_port);

void transport_free(struct transport_ctx *ctx);

/*
 * Send one data shard on path[path_idx].
 * Returns 0 on success, -1 on error (logged; caller continues).
 */
int transport_send_shard(struct transport_ctx *ctx,
                         int path_idx,
                         uint32_t block_id,
                         uint8_t shard_idx, uint8_t k, uint8_t n,
                         const struct shard *s);

/*
 * Send a probe packet on path[path_idx].
 * timestamp_us: microseconds since epoch (for RTT measurement).
 */
int transport_send_probe(struct transport_ctx *ctx,
                         int path_idx,
                         uint64_t timestamp_us);

/*
 * Send a probe-echo packet on path[path_idx].
 * Echoes the original timestamp_us back to the sender for RTT measurement.
 */
int transport_send_probe_echo(struct transport_ctx *ctx,
                              int path_idx,
                              uint64_t timestamp_us);

/*
 * Send a NACK for block_id on path[path_idx].
 * RX calls this when a block expires undecoded.
 */
int transport_send_nack(struct transport_ctx *ctx,
                        int path_idx,
                        uint32_t block_id);

/*
 * Add all receive socket fds to rfds.
 * Returns nfds (highest fd + 1) for use with select().
 */
int transport_fill_fdset(struct transport_ctx *ctx, fd_set *rfds);

/*
 * Receive one datagram from whichever socket is readable in rfds.
 * On TYPE_DATA:       fills *hdr and *shard_out; sets *path_idx.
 * On TYPE_PROBE/_ECHO: fills *hdr and *probe_ts_out; sets *path_idx.
 *
 * Returns TYPE_DATA, TYPE_PROBE, TYPE_PROBE_ECHO, or -1 on error/no data.
 */
int transport_recv(struct transport_ctx *ctx, const fd_set *rfds,
                   struct wire_header *hdr,
                   struct shard *shard_out,
                   uint64_t *probe_ts_out,
                   int *path_idx);

#endif /* TRANSPORT_H */
