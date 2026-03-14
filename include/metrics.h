#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include "common.h"

/* Global counters — incremented from main loop, read by HTTP handler. */
struct metrics {
    uint64_t shards_sent[MAX_PATHS];
    uint64_t shards_received[MAX_PATHS];
    uint64_t decode_success;
    uint64_t decode_failure;
    uint64_t blocks_encoded;

    /* Block decode latency histogram (microseconds from first shard to decode).
     * Bucket boundaries in ms: 1, 5, 10, 25, 50, 100, 250, 500. */
#define METRICS_LATENCY_BUCKETS 8
    uint64_t latency_bucket[METRICS_LATENCY_BUCKETS];
    uint64_t latency_count;
    double   latency_sum_ms;
};

/* Single global instance — defined in metrics.c */
extern struct metrics g_metrics;

/* Latency bucket upper bounds in milliseconds. */
extern const double metrics_latency_bounds[METRICS_LATENCY_BUCKETS];

/* Record one decode latency sample (microseconds). */
void metrics_record_latency(uint64_t latency_us);

/*
 * Open a non-blocking TCP listener on the given port for /metrics.
 * Returns the listener fd (>= 0) or -1 on error.
 */
int metrics_listen(uint16_t port);

/*
 * Handle a single ready connection on the metrics listener fd.
 * Call this when select() indicates the listener fd is readable.
 * Accepts the connection, writes the Prometheus response, and closes it.
 *
 * path_names: array of path name strings (from config) for labels.
 * path_count: number of paths.
 * loss_rates: current per-path loss rate (from strategy).
 * rtt_ms:     current per-path RTT in ms (from strategy).
 * redundancy_ratio: current effective redundancy ratio.
 */
void metrics_handle(int listener_fd,
                    const char (*path_names)[32],
                    int path_count,
                    const float *loss_rates,
                    const float *rtt_ms,
                    float redundancy_ratio);

#endif /* METRICS_H */
