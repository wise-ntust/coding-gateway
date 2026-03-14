#include <string.h>
#include <unistd.h>
#include "rx.h"
#include "tun.h"
#include "metrics.h"

#define RX_BLOCK_TIMEOUT_US 50000ULL

void rx_window_init(struct rx_window *win)
{
    memset(win, 0, sizeof(*win));
}

static struct rx_block *rx_find(struct rx_window *win, uint32_t block_id,
                                int window_size)
{
    int i;
    if ((int32_t)(block_id - win->base_id) < 0) return NULL;
    if ((int32_t)(block_id - win->base_id) >= window_size) return NULL;

    for (i = 0; i < window_size; i++) {
        if (win->slots[i].recv_count > 0 &&
            win->slots[i].block_id == block_id)
            return &win->slots[i];
    }
    for (i = 0; i < window_size; i++) {
        if (win->slots[i].recv_count == 0) {
            memset(&win->slots[i], 0, sizeof(win->slots[i]));
            win->slots[i].block_id = block_id;
            return &win->slots[i];
        }
    }
    return NULL;
}

void rx_window_insert(struct rx_window *win, const struct wire_header *hdr,
                      const struct shard *s, int window_size)
{
    struct rx_block *blk = rx_find(win, hdr->block_id, window_size);
    int idx;
    if (!blk) {
        LOG_DBG("rx_window: no slot for block %u", hdr->block_id);
        return;
    }
    if (blk->decoded) return;

    idx = hdr->shard_idx;
    if (idx >= MAX_N) return;
    if (blk->received[idx]) return;

    blk->shards[idx] = *s;
    blk->received[idx] = true;
    blk->recv_count++;
    if (blk->recv_count == 1)
        blk->first_recv_us = now_us();
}

static bool try_decode(struct rx_block *blk, int k, int tun_fd)
{
    struct shard  k_shards[MAX_K];
    uint8_t       out[MAX_K][MAX_PAYLOAD];
    uint16_t      out_len[MAX_K];
    int           collected = 0;
    memset(k_shards, 0, sizeof(k_shards));
    int           i;

    if (blk->decoded) return false;
    if (blk->recv_count < k) return false;

    for (i = 0; i < MAX_N && collected < k; i++) {
        if (blk->received[i])
            k_shards[collected++] = blk->shards[i];
    }

    if (decode_block(k_shards, k, out, out_len) != 0) {
        LOG_WARN("decode_block failed for block %u", blk->block_id);
        return false;
    }

    blk->decoded = true;

    for (i = 0; i < k; i++) {
        uint16_t ip_len = out_len[i];
        if (ip_len >= 20) {
            uint8_t version = (out[i][0] >> 4) & 0x0F;
            if (version == 4) {
                uint16_t total = (uint16_t)((out[i][2] << 8) | out[i][3]);
                if (total >= 20 && total <= ip_len)
                    ip_len = total;
            } else if (version == 6 && ip_len >= 40) {
                uint16_t payload = (uint16_t)((out[i][4] << 8) | out[i][5]);
                uint16_t total = payload + 40;
                if (total >= 40 && total <= ip_len)
                    ip_len = total;
            }
        }
        if (tun_write(tun_fd, out[i], ip_len) < 0)
            LOG_WARN("tun_write failed for block %u pkt %d", blk->block_id, i);
    }
    LOG_DBG("decoded block %u", blk->block_id);
    return true;
}

bool rx_window_try_decode(struct rx_window *win, uint32_t block_id,
                          int k, int window_size, int tun_fd)
{
    struct rx_block *blk = rx_find(win, block_id, window_size);
    if (!blk) return false;
    return try_decode(blk, k, tun_fd);
}

void rx_window_advance(struct rx_window *win, int window_size)
{
    int  i;
    bool found;
    do {
        found = false;
        for (i = 0; i < window_size; i++) {
            if (win->slots[i].recv_count > 0 &&
                win->slots[i].block_id == win->base_id) {
                bool expired = false;
                if (!win->slots[i].decoded) {
                    if (now_us() - win->slots[i].first_recv_us >= RX_BLOCK_TIMEOUT_US)
                        expired = true;
                }
                if (win->slots[i].decoded || expired) {
                    memset(&win->slots[i], 0, sizeof(win->slots[i]));
                    win->base_id++;
                    found = true;
                }
                break;
            }
        }
        if (!found) {
            bool has_newer = false;
            for (i = 0; i < window_size; i++) {
                if (win->slots[i].recv_count > 0 &&
                    (int32_t)(win->slots[i].block_id - win->base_id) > 0) {
                    has_newer = true;
                    break;
                }
            }
            if (has_newer) {
                win->base_id++;
                found = true;
            }
        }
    } while (found);
}
