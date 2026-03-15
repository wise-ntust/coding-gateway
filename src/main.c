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
#include "tun.h"
#include "transport.h"
#include "strategy.h"
#include "metrics.h"
#include "tx.h"
#include "rx.h"
#include "crypto.h"

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t reload_flag = 0;
static volatile sig_atomic_t g_sig_received = 0;

static void sig_handler(int sig)
{
    g_sig_received = sig;
    running = 0;
}

static void sighup_handler(int sig)
{
    (void)sig;
    reload_flag = 1;
}

int main(int argc, char *argv[])
{
    struct gateway_config  cfg;
    struct transport_ctx  *tctx = NULL;
    struct strategy_ctx   *sctx = NULL;
    int                    tun_fd = -1;
    int                    metrics_fd = -1;
    bool                   is_tx, is_rx;

    struct timeval last_probe_tv;
    gettimeofday(&last_probe_tv, NULL);

    struct tx_block  tx;
    uint32_t         next_block_id = 0;
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
    g_log_level = cfg.log_level;

    /* Initialise crypto */
    struct crypto_ctx crypto;
    if (cfg.crypto_key[0] != '\0') {
        uint8_t key_bin[CRYPTO_KEY_LEN];
        if (crypto_parse_key(cfg.crypto_key, key_bin) == 0) {
            crypto_init(&crypto, key_bin);
            LOG_INFO("encryption enabled");
        } else {
            LOG_ERR("invalid crypto_key (need 64 hex chars)");
            return 1;
        }
    } else {
        crypto_init(&crypto, NULL);
    }

    LOG_INFO("mode=%s tun=%s listen_port=%d k=%d",
             cfg.mode, cfg.tun_name, cfg.listen_port, cfg.k);

    is_tx = (strcmp(cfg.mode, "tx") == 0 || strcmp(cfg.mode, "both") == 0);
    is_rx = (strcmp(cfg.mode, "rx") == 0 || strcmp(cfg.mode, "both") == 0);

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

    memset(&g_metrics, 0, sizeof(g_metrics));
    if (cfg.metrics_port > 0) {
        metrics_fd = metrics_listen((uint16_t)cfg.metrics_port);
        if (metrics_fd < 0)
            LOG_WARN("metrics exporter disabled (bind failed)");
    }

    tx_block_init(&tx, next_block_id++);
    rx_window_init(&rx_win);

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

    while (running) {
        fd_set         rfds;
        struct timeval timeout;
        int            nfds, ret;
        long           to_ms;

        FD_ZERO(&rfds);
        nfds = transport_fill_fdset(tctx, &rfds);

        if (is_tx) {
            FD_SET(tun_fd, &rfds);
            if (tun_fd + 1 > nfds) nfds = tun_fd + 1;
        }
        if (metrics_fd >= 0) {
            FD_SET(metrics_fd, &rfds);
            if (metrics_fd + 1 > nfds) nfds = metrics_fd + 1;
        }

        to_ms = (long)cfg.block_timeout_ms;
        if ((long)cfg.probe_interval_ms < to_ms)
            to_ms = (long)cfg.probe_interval_ms;
        if (to_ms < 1) to_ms = 1;
        timeout.tv_sec  = to_ms / 1000L;
        timeout.tv_usec = (to_ms % 1000L) * 1000L;

        ret = select(nfds, &rfds, NULL, NULL, &timeout);
        if (ret < 0) {
            if (!running) break;
            if (reload_flag) goto do_reload;
            LOG_WARN("select() error");
            continue;
        }

        /* SIGHUP reload */
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
                    g_log_level              = new_cfg.log_level;
                    LOG_INFO("config reloaded via SIGHUP");
                } else {
                    LOG_WARN("SIGHUP reload failed, keeping current config");
                }
            }
            continue;
        }

        /* TX: read packet from TUN */
        if (is_tx && FD_ISSET(tun_fd, &rfds)) {
            uint8_t pkt_buf[MAX_PAYLOAD];
            ssize_t pkt_len = tun_read(tun_fd, pkt_buf, sizeof(pkt_buf));
            if (pkt_len > 0) {
                if (tx_block_add_pkt(&tx, pkt_buf, (uint16_t)pkt_len, cfg.k)) {
                    tx_block_flush(&tx, tctx, sctx, cfg.k, &crypto);
                    tx_block_init(&tx, next_block_id++);
                }
            }
        }

        /* TX: block timeout flush */
        if (is_tx && tx_block_needs_flush(&tx, cfg.block_timeout_ms)) {
            tx_block_flush(&tx, tctx, sctx, cfg.k, &crypto);
            tx_block_init(&tx, next_block_id++);
        }

        /* RX: receive UDP datagram */
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
                rx_window_insert(&rx_win, &hdr, &shard_in, cfg.window_size, &crypto);
                if (rx_window_try_decode(&rx_win, hdr.block_id,
                                         hdr.k, cfg.window_size, tun_fd)) {
                    g_metrics.decode_success++;
                    /* Latency: find the block to read first_recv_us */
                }
                rx_window_advance(&rx_win, cfg.window_size);
            } else if (type == TYPE_PROBE) {
                transport_send_probe_echo(tctx, path_idx, probe_ts);
            } else if (type == TYPE_PROBE_ECHO && is_tx) {
                uint64_t rtt_us = now_us() - probe_ts;
                strategy_update_probe(sctx, path_idx, rtt_us, true);
                LOG_DBG("PROBE_ECHO path=%d rtt=%llu us",
                        path_idx, (unsigned long long)rtt_us);
            }
        }

        /* Metrics */
        if (metrics_fd >= 0 && FD_ISSET(metrics_fd, &rfds)) {
            char  pnames[MAX_PATHS][32];
            float loss[MAX_PATHS];
            float rtt[MAX_PATHS];
            int   pc = strategy_path_count(sctx);
            int   pi;
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
                           loss, rtt, cfg.redundancy_ratio);
        }

        /* TX: send probes */
        if (is_tx) {
            struct timeval now_tv;
            long probe_elapsed;
            gettimeofday(&now_tv, NULL);
            probe_elapsed = (long)(now_tv.tv_sec  - last_probe_tv.tv_sec)  * 1000L
                          + (long)(now_tv.tv_usec - last_probe_tv.tv_usec) / 1000L;
            if (probe_elapsed >= (long)cfg.probe_interval_ms) {
                int p, pc = strategy_path_count(sctx);
                uint64_t ts = now_us();
                for (p = 0; p < pc; p++) {
                    struct path_state *ps = strategy_get_path_state(sctx, p);
                    if (ps && ps->alive) {
                        transport_send_probe(tctx, p, ts);
                        ps->probes_sent++;
                    }
                }
                last_probe_tv = now_tv;
            }
        }
    }

    LOG_INFO("received signal %d (%s), shutting down",
             (int)g_sig_received,
             g_sig_received == SIGTERM ? "SIGTERM" : "SIGINT");

    if (is_tx && tx.pkt_count > 0) {
        LOG_INFO("draining TX block: %d pending packet(s)", tx.pkt_count);
        tx_block_flush(&tx, tctx, sctx, cfg.k, &crypto);
    }

    if (metrics_fd >= 0) close(metrics_fd);
    strategy_free(sctx);
    transport_free(tctx);
    close(tun_fd);
    return 0;
}
