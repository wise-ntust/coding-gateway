#ifndef TX_H
#define TX_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include "common.h"
#include "transport.h"
#include "strategy.h"
#include "crypto.h"

struct tx_block {
    uint32_t       block_id;
    uint8_t        pkts[MAX_K][MAX_PAYLOAD];
    uint16_t       pkt_len[MAX_K];
    int            pkt_count;
    struct timeval first_pkt_time;
};

void tx_block_init(struct tx_block *blk, uint32_t block_id);

/* Add a packet to the block. Returns true if block is now full. */
bool tx_block_add_pkt(struct tx_block *blk, const uint8_t *pkt,
                      uint16_t len, int k);

/* Returns true if block has packets and timeout has elapsed. */
bool tx_block_needs_flush(const struct tx_block *blk, int timeout_ms);

/* Encode and send all shards via strategy. Encrypts shard payloads if crypto enabled. */
void tx_block_flush(struct tx_block *blk,
                    struct transport_ctx *tctx,
                    struct strategy_ctx *sctx,
                    int k,
                    const struct crypto_ctx *crypto);

#endif /* TX_H */
