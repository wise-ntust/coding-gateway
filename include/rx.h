#ifndef RX_H
#define RX_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "codec.h"
#include "transport.h"
#include "crypto.h"

struct rx_block {
    uint32_t     block_id;
    struct shard shards[MAX_N];
    bool         received[MAX_N];
    int          recv_count;
    bool         decoded;
    uint64_t     first_recv_us;
};

struct rx_window {
    struct rx_block slots[MAX_WINDOW];
    uint32_t        base_id;
};

void rx_window_init(struct rx_window *win);

/* Insert a received shard. Decrypts payload in-place if crypto enabled. */
void rx_window_insert(struct rx_window *win, const struct wire_header *hdr,
                      const struct shard *s, int window_size,
                      const struct crypto_ctx *crypto);

/*
 * Try to decode a block and write recovered packets to TUN.
 * Returns true if the block was newly decoded on this call.
 */
bool rx_window_try_decode(struct rx_window *win, uint32_t block_id,
                          int k, int window_size, int tun_fd);

void rx_window_advance(struct rx_window *win, int window_size);

/*
 * Extract actual IP packet length from header, stripping padding.
 * Supports IPv4 (version=4, total-length at bytes 2-3) and
 * IPv6 (version=6, payload-length at bytes 4-5, +40 header).
 * Returns the detected length, or padded_len if the packet is
 * too short or the header is unrecognized.
 */
uint16_t ip_packet_length(const uint8_t *pkt, uint16_t padded_len);

#endif /* RX_H */
