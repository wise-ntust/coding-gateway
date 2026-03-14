#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include "common.h"
#include "config.h"
#include "gf256.h"
#include "codec.h"
#include "tun.h"
#include "transport.h"
#include "strategy.h"
#include "metrics.h"

/* -------------------------------------------------------------------------
 * TX side: current block being assembled from TUN reads
 * ---------------------------------------------------------------------- */
struct tx_block {
    uint32_t       block_id;
    uint8_t        pkts[MAX_K][MAX_PAYLOAD];
    uint16_t       pkt_len[MAX_K];
    int            pkt_count;
    struct timeval first_pkt_time;
};

/* -------------------------------------------------------------------------
 * RX side: per-block receive buffer inside the sliding window
 * ---------------------------------------------------------------------- */
struct rx_block {
    uint32_t    block_id;
    struct shard shards[MAX_N];
    bool         received[MAX_N];
    int          recv_count;
    bool         decoded;
    uint64_t     first_recv_us; /* time first shard arrived */
};

struct rx_window {
    struct rx_block slots[MAX_WINDOW];
    uint32_t        base_id;
};

/* -------------------------------------------------------------------------
 * Global running flag — set to 0 by SIGINT/SIGTERM
 * ---------------------------------------------------------------------- */
static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t reload_flag = 0;

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

static void sighup_handler(int sig)
{
    (void)sig;
    reload_flag = 1;
}

/* -------------------------------------------------------------------------
 * Helper: microseconds since epoch
 * ---------------------------------------------------------------------- */
static uint64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

/* -------------------------------------------------------------------------
 * Helper: milliseconds elapsed since 'start'
 * ---------------------------------------------------------------------- */
static long elapsed_ms(const struct timeval *start)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (long)(now.tv_sec  - start->tv_sec)  * 1000L
         + (long)(now.tv_usec - start->tv_usec) / 1000L;
}

/* -------------------------------------------------------------------------
 * Flush the TX block: encode and send all shards via strategy
 * ---------------------------------------------------------------------- */
static void flush_block(struct tx_block *blk,
                        struct transport_ctx *tctx,
                        struct strategy_ctx  *sctx,
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
            blk->pkt_len[j] = blk->pkt_len[0]; /* same len as first pkt */
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

/* -------------------------------------------------------------------------
 * RX window helpers
 * ---------------------------------------------------------------------- */
static struct rx_block *rx_window_find(struct rx_window *win, uint32_t block_id,
                                       int window_size)
{
    int i;
    /* Accept blocks within the window ahead of base */
    if ((int32_t)(block_id - win->base_id) < 0) return NULL; /* old block */
    if ((int32_t)(block_id - win->base_id) >= window_size)   return NULL; /* too far ahead */

    for (i = 0; i < window_size; i++) {
        if (win->slots[i].recv_count > 0 &&
            win->slots[i].block_id == block_id)
            return &win->slots[i];
    }
    /* Allocate a free slot */
    for (i = 0; i < window_size; i++) {
        if (win->slots[i].recv_count == 0) {
            memset(&win->slots[i], 0, sizeof(win->slots[i]));
            win->slots[i].block_id = block_id;
            return &win->slots[i];
        }
    }
    return NULL; /* window full */
}

static void rx_window_insert(struct rx_window *win,
                             const struct wire_header *hdr,
                             const struct shard *s,
                             int window_size)
{
    struct rx_block *blk = rx_window_find(win, hdr->block_id, window_size);
    int idx;
    if (!blk) {
        LOG_DBG("rx_window: no slot for block %u", hdr->block_id);
        return;
    }
    if (blk->decoded) return;

    idx = hdr->shard_idx;
    if (idx >= MAX_N) return;
    if (blk->received[idx]) return; /* duplicate */

    blk->shards[idx] = *s;
    blk->received[idx] = true;
    blk->recv_count++;
    if (blk->recv_count == 1)
        blk->first_recv_us = now_us();
}

/* -------------------------------------------------------------------------
 * Attempt decode of a block; write recovered packets to TUN if successful.
 * Returns true if decoded (or already was decoded).
 * ---------------------------------------------------------------------- */
static bool try_decode_block(struct rx_block *blk, int k, int tun_fd)
{
    struct shard  k_shards[MAX_K];
    uint8_t       out[MAX_K][MAX_PAYLOAD];
    uint16_t      out_len[MAX_K];
    int           collected = 0;
    int           i;

    if (blk->decoded) return true;
    if (blk->recv_count < k) return false;

    /* Gather k shards */
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
        /* Extract actual packet length from IP header to strip padding. */
        uint16_t ip_len = out_len[i];
        if (ip_len >= 20) {
            uint8_t version = (out[i][0] >> 4) & 0x0F;
            if (version == 4) {
                /* IPv4: total length at bytes 2-3 (includes header) */
                uint16_t total = (uint16_t)((out[i][2] << 8) | out[i][3]);
                if (total >= 20 && total <= ip_len)
                    ip_len = total;
            } else if (version == 6 && ip_len >= 40) {
                /* IPv6: payload length at bytes 4-5 (excludes 40-byte header) */
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

/* -------------------------------------------------------------------------
 * Advance RX window base past decoded/expired blocks
 * ---------------------------------------------------------------------- */
/* Block eviction timeout: if a block sits undecoded for this many us,
 * assume its remaining shards are permanently lost and advance past it.
 * 50 ms: 5× block_timeout_ms, works for both ping (500 ms interval)
 * and high-rate iperf3 (blocks arrive every ~11 ms at 10 Mbps). */
#define RX_BLOCK_TIMEOUT_US 50000ULL

static void rx_window_advance(struct rx_window *win, int window_size)
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
                break; /* slot found for base_id, stop searching */
            }
        }
        /* No slot holds base_id — block was completely missed; skip it */
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

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    struct gateway_config  cfg;
    struct transport_ctx  *tctx = NULL;
    struct strategy_ctx   *sctx = NULL;
    int                    tun_fd = -1;
    int                    metrics_fd = -1;
    bool                   is_tx, is_rx;

    /* Probe timer state */
    struct timeval last_probe_tv;
    gettimeofday(&last_probe_tv, NULL);

    /* TX state */
    struct tx_block tx;
    uint32_t        next_block_id = 0;

    /* RX state */
    struct rx_window rx_win;

    const char *config_path;

    if (argc < 3 || strcmp(argv[1], "--config") != 0) {
        fprintf(stderr, "usage: coding-gateway --config <path>\n");
        return 1;
    }
    config_path = argv[2];

    srand((unsigned int)time(NULL));
    gf256_init();

    if (config_load(config_path, &cfg) != 0)
        return 1;

    LOG_INFO("mode=%s tun=%s listen_port=%d k=%d",
             cfg.mode, cfg.tun_name, cfg.listen_port, cfg.k);

    is_tx = (strcmp(cfg.mode, "tx") == 0 || strcmp(cfg.mode, "both") == 0);
    is_rx = (strcmp(cfg.mode, "rx") == 0 || strcmp(cfg.mode, "both") == 0);

    /* Open TUN for all modes — TX reads packets to encode, RX writes decoded */
    tun_fd = tun_open(cfg.tun_name);
    if (tun_fd < 0) {
        LOG_ERR("tun_open failed for %s", cfg.tun_name);
        return 1;
    }
    if (tun_configure(cfg.tun_name, cfg.tun_addr) != 0) {
        LOG_ERR("tun_configure failed");
        close(tun_fd);
        return 1;
    }

    tctx = transport_init(&cfg, (uint16_t)cfg.listen_port);
    if (!tctx) {
        LOG_ERR("transport_init failed");
        close(tun_fd);
        return 1;
    }

    sctx = strategy_init(&cfg);
    if (!sctx) {
        LOG_ERR("strategy_init failed");
        transport_free(tctx);
        close(tun_fd);
        return 1;
    }

    /* Initialise metrics exporter */
    memset(&g_metrics, 0, sizeof(g_metrics));
    if (cfg.metrics_port > 0) {
        metrics_fd = metrics_listen((uint16_t)cfg.metrics_port);
        if (metrics_fd < 0)
            LOG_WARN("metrics exporter disabled (bind failed)");
    }

    /* Initialise TX block */
    memset(&tx, 0, sizeof(tx));
    tx.block_id = next_block_id++;

    /* Initialise RX window */
    memset(&rx_win, 0, sizeof(rx_win));
    rx_win.base_id = 0;

    /* Signal handling */
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sig_handler;
        sigaction(SIGINT,  &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);

        sa.sa_handler = sighup_handler;
        sigaction(SIGHUP, &sa, NULL);
    }

    LOG_INFO("event loop starting");

    /* ------------------------------------------------------------------ */
    while (running) {
        fd_set         rfds;
        struct timeval timeout;
        int            nfds, ret;
        long           to_ms;

        FD_ZERO(&rfds);

        /* Always listen for incoming UDP */
        nfds = transport_fill_fdset(tctx, &rfds);

        /* TX mode also polls the TUN fd */
        if (is_tx) {
            FD_SET(tun_fd, &rfds);
            if (tun_fd + 1 > nfds) nfds = tun_fd + 1;
        }

        /* Metrics listener */
        if (metrics_fd >= 0) {
            FD_SET(metrics_fd, &rfds);
            if (metrics_fd + 1 > nfds) nfds = metrics_fd + 1;
        }

        /* Compute select timeout: minimum of block_timeout and probe_interval */
        to_ms = (long)cfg.block_timeout_ms;
        if ((long)cfg.probe_interval_ms < to_ms)
            to_ms = (long)cfg.probe_interval_ms;
        if (to_ms < 1) to_ms = 1;

        timeout.tv_sec  = to_ms / 1000L;
        timeout.tv_usec = (to_ms % 1000L) * 1000L;

        ret = select(nfds, &rfds, NULL, NULL, &timeout);
        if (ret < 0) {
            if (!running) break;
            /* SIGHUP interrupts select() with EINTR — not an error */
            if (reload_flag) goto do_reload;
            LOG_WARN("select() error");
            continue;
        }

        /* ---- SIGHUP: hot-reload config -------------------------------- */
        if (reload_flag) {
do_reload:
            reload_flag = 0;
            {
                struct gateway_config new_cfg;
                if (config_load(config_path, &new_cfg) == 0) {
                    strategy_reload(sctx, &new_cfg);
                    cfg.redundancy_ratio     = new_cfg.redundancy_ratio;
                    cfg.probe_interval_ms    = new_cfg.probe_interval_ms;
                    cfg.probe_loss_threshold = new_cfg.probe_loss_threshold;
                    cfg.block_timeout_ms     = new_cfg.block_timeout_ms;
                    LOG_INFO("config reloaded via SIGHUP");
                } else {
                    LOG_WARN("SIGHUP reload failed, keeping current config");
                }
            }
            continue;
        }

        /* ---- TX: read packet from TUN -------------------------------- */
        if (is_tx && FD_ISSET(tun_fd, &rfds)) {
            uint8_t pkt_buf[MAX_PAYLOAD];
            ssize_t pkt_len = tun_read(tun_fd, pkt_buf, sizeof(pkt_buf));
            if (pkt_len > 0) {
                if (tx.pkt_count == 0)
                    gettimeofday(&tx.first_pkt_time, NULL);

                if (tx.pkt_count < cfg.k) {
                    memcpy(tx.pkts[tx.pkt_count], pkt_buf, (size_t)pkt_len);
                    tx.pkt_len[tx.pkt_count] = (uint16_t)pkt_len;
                    tx.pkt_count++;
                }

                if (tx.pkt_count >= cfg.k) {
                    flush_block(&tx, tctx, sctx, cfg.k);
                    memset(&tx, 0, sizeof(tx));
                    tx.block_id = next_block_id++;
                }
            }
        }

        /* ---- TX: block timeout flush --------------------------------- */
        if (is_tx && tx.pkt_count > 0) {
            if (elapsed_ms(&tx.first_pkt_time) >= (long)cfg.block_timeout_ms) {
                flush_block(&tx, tctx, sctx, cfg.k);
                memset(&tx, 0, sizeof(tx));
                tx.block_id = next_block_id++;
            }
        }

        /* ---- RX (and TX for probe echoes): receive UDP datagram ------ */
        {
            struct wire_header hdr;
            struct shard       shard_in;
            uint64_t           probe_ts = 0;
            int                path_idx = 0;
            int                type;

            type = transport_recv(tctx, &rfds, &hdr, &shard_in,
                                  &probe_ts, &path_idx);
            if (type == TYPE_DATA && is_rx) {
                g_metrics.shards_received[path_idx]++;
                rx_window_insert(&rx_win, &hdr, &shard_in, cfg.window_size);
                {
                    struct rx_block *blk =
                        rx_window_find(&rx_win, hdr.block_id, cfg.window_size);
                    if (blk != NULL) {
                        bool was_decoded = blk->decoded;
                        if (try_decode_block(blk, hdr.k, tun_fd)) {
                            if (!was_decoded) {
                                g_metrics.decode_success++;
                                metrics_record_latency(now_us() - blk->first_recv_us);
                            }
                        } else if (blk->recv_count >= hdr.k && !blk->decoded) {
                            g_metrics.decode_failure++;
                        }
                    }
                }
                rx_window_advance(&rx_win, cfg.window_size);
            } else if (type == TYPE_PROBE) {
                LOG_DBG("PROBE received on path %d ts=%llu",
                        path_idx, (unsigned long long)probe_ts);
                transport_send_probe_echo(tctx, path_idx, probe_ts);
            } else if (type == TYPE_PROBE_ECHO && is_tx) {
                struct timeval now_tv;
                uint64_t       now_usec;
                uint64_t       rtt_us;
                gettimeofday(&now_tv, NULL);
                now_usec = (uint64_t)now_tv.tv_sec * 1000000ULL
                         + (uint64_t)now_tv.tv_usec;
                rtt_us = now_usec - probe_ts;
                strategy_update_probe(sctx, path_idx, rtt_us, true);
                LOG_DBG("PROBE_ECHO path=%d rtt=%llu us",
                        path_idx, (unsigned long long)rtt_us);
            }
        }

        /* ---- Metrics: handle /metrics HTTP requests ------------------- */
        if (metrics_fd >= 0 && FD_ISSET(metrics_fd, &rfds)) {
            char  pnames[MAX_PATHS][32];
            float loss[MAX_PATHS];
            float rtt[MAX_PATHS];
            int   pc = strategy_path_count(sctx);
            int   pi;
            float cur_ratio = cfg.redundancy_ratio;
            for (pi = 0; pi < pc; pi++) {
                struct path_state *ps = strategy_get_path_state(sctx, pi);
                if (ps) {
                    memcpy(pnames[pi], ps->cfg.name, sizeof(pnames[pi]));
                    loss[pi] = ps->loss_rate;
                    rtt[pi]  = ps->rtt_ms;
                } else {
                    pnames[pi][0] = '\0';
                    loss[pi] = 0.0f;
                    rtt[pi]  = 0.0f;
                }
            }
            metrics_handle(metrics_fd,
                           (const char (*)[32])pnames, pc,
                           loss, rtt, cur_ratio);
        }

        /* ---- TX: send probes if interval elapsed --------------------- */
        if (is_tx) {
            struct timeval now_tv;
            long           probe_elapsed;
            gettimeofday(&now_tv, NULL);
            probe_elapsed = (long)(now_tv.tv_sec  - last_probe_tv.tv_sec)  * 1000L
                          + (long)(now_tv.tv_usec - last_probe_tv.tv_usec) / 1000L;

            if (probe_elapsed >= (long)cfg.probe_interval_ms) {
                int  p, pc = strategy_path_count(sctx);
                uint64_t ts = now_us();
                for (p = 0; p < pc; p++) {
                    struct path_state *ps = strategy_get_path_state(sctx, p);
                    if (ps && ps->alive) {
                        transport_send_probe(tctx, p, ts);
                        strategy_get_path_state(sctx, p)->probes_sent++;
                    }
                }
                last_probe_tv = now_tv;
            }
        }
    }
    /* ------------------------------------------------------------------ */

    LOG_INFO("shutting down");

    /* Flush any partial TX block before exit */
    if (is_tx && tx.pkt_count > 0)
        flush_block(&tx, tctx, sctx, cfg.k);

    if (metrics_fd >= 0) close(metrics_fd);
    strategy_free(sctx);
    transport_free(tctx);
    close(tun_fd);
    return 0;
}
