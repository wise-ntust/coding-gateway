#ifndef RX_H
#define RX_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "codec.h"
#include "transport.h"

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

void rx_window_insert(struct rx_window *win, const struct wire_header *hdr,
                      const struct shard *s, int window_size);

/*
 * Try to decode a block and write recovered packets to TUN.
 * Returns true if the block was newly decoded on this call.
 */
bool rx_window_try_decode(struct rx_window *win, uint32_t block_id,
                          int k, int window_size, int tun_fd);

void rx_window_advance(struct rx_window *win, int window_size);

#endif /* RX_H */
