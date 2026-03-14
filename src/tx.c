#include <string.h>
#include <sys/time.h>
#include "tx.h"
#include "codec.h"
#include "metrics.h"

void tx_block_init(struct tx_block *blk, uint32_t block_id)
{
    memset(blk, 0, sizeof(*blk));
    blk->block_id = block_id;
}

bool tx_block_add_pkt(struct tx_block *blk, const uint8_t *pkt,
                      uint16_t len, int k)
{
    if (blk->pkt_count == 0)
        gettimeofday(&blk->first_pkt_time, NULL);

    if (blk->pkt_count < k) {
        memcpy(blk->pkts[blk->pkt_count], pkt, (size_t)len);
        blk->pkt_len[blk->pkt_count] = len;
        blk->pkt_count++;
    }
    return blk->pkt_count >= k;
}

bool tx_block_needs_flush(const struct tx_block *blk, int timeout_ms)
{
    struct timeval now;
    long elapsed;
    if (blk->pkt_count == 0) return false;
    gettimeofday(&now, NULL);
    elapsed = (long)(now.tv_sec  - blk->first_pkt_time.tv_sec)  * 1000L
            + (long)(now.tv_usec - blk->first_pkt_time.tv_usec) / 1000L;
    return elapsed >= (long)timeout_ms;
}

void tx_block_flush(struct tx_block *blk,
                    struct transport_ctx *tctx,
                    struct strategy_ctx *sctx,
                    int k)
{
    struct shard shards[MAX_N];
    int n, i, path;

    if (blk->pkt_count == 0) return;

    /* Pad shorter packets to the max length in this block */
    {
        uint16_t max_len = 0;
        int j;
        for (j = 0; j < blk->pkt_count; j++)
            if (blk->pkt_len[j] > max_len) max_len = blk->pkt_len[j];
        for (j = 0; j < blk->pkt_count; j++) {
            if (blk->pkt_len[j] < max_len)
                memset(blk->pkts[j] + blk->pkt_len[j], 0,
                       (size_t)(max_len - blk->pkt_len[j]));
        }
    }

    /* If fewer than k packets arrived, fill remaining slots with zeros */
    {
        int actual_k = blk->pkt_count;
        int j;
        for (j = actual_k; j < k; j++) {
            memset(blk->pkts[j], 0, MAX_PAYLOAD);
            blk->pkt_len[j] = blk->pkt_len[0];
        }
    }

    n = strategy_compute_n(sctx, k);

    encode_block((const uint8_t (*)[MAX_PAYLOAD])blk->pkts,
                 blk->pkt_len, k, n, shards);

    for (i = 0; i < n; i++) {
        path = strategy_next_path(sctx);
        if (path < 0) {
            LOG_WARN("no alive path for shard %d of block %u", i, blk->block_id);
            continue;
        }
        if (transport_send_shard(tctx, path, blk->block_id,
                                 (uint8_t)i, (uint8_t)k, (uint8_t)n,
                                 &shards[i]) != 0)
            LOG_WARN("send_shard failed: block=%u shard=%d path=%d",
                     blk->block_id, i, path);
        else
            g_metrics.shards_sent[path]++;
    }

    g_metrics.blocks_encoded++;
    LOG_DBG("flushed block %u: k=%d n=%d", blk->block_id, k, n);
}
