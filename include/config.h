#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

struct path_config {
    char     name[32];
    char     interface[16];
    char     remote_ip[40];
    uint16_t remote_port;
    float    weight;
    bool     enabled;
};

struct gateway_config {
    char   mode[8];              /* "tx", "rx", "both" */
    char   tun_name[16];
    char   tun_addr[20];         /* CIDR, e.g. "10.0.0.1/30" */
    int    k;
    float  redundancy_ratio;
    int    block_timeout_ms;
    int    max_payload;
    int    window_size;
    char   strategy_type[16];   /* "fixed", "weighted", "adaptive" */
    int    probe_interval_ms;
    float  probe_loss_threshold;
    int    listen_port;          /* UDP port this node binds for incoming datagrams */
    struct path_config paths[MAX_PATHS];
    int    path_count;
    int  metrics_port;      /* TCP port for Prometheus /metrics (0 = disabled) */
};

/*
 * Load config from an INI file.
 * Returns 0 on success, -1 on error (message printed to stderr).
 */
int config_load(const char *path, struct gateway_config *cfg);

#endif /* CONFIG_H */
