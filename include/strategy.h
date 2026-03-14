#ifndef STRATEGY_H
#define STRATEGY_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

struct path_state {
    struct path_config cfg;
    float    loss_rate;       /* EWMA of probe loss, 0.0-1.0 */
    float    rtt_ms;          /* EWMA of round-trip time */
    float    weight_current;  /* effective weight (adaptive adjusts) */
    bool     alive;
    uint64_t probes_sent;
    uint64_t probes_recv;
    uint64_t shards_sent;
};

struct strategy_ctx;

struct strategy_ctx *strategy_init(const struct gateway_config *cfg);
void strategy_free(struct strategy_ctx *ctx);

/*
 * Choose the next path index to send a shard on.
 * Returns a valid path index (0..path_count-1), or -1 if no alive path.
 * Uses round-robin for fixed/weighted strategies.
 */
int strategy_next_path(struct strategy_ctx *ctx);

/*
 * Compute n = ceil(k * effective_redundancy_ratio).
 * Adaptive mode scales redundancy up when paths are dead.
 * Result is clamped to [k, MAX_N].
 */
int strategy_compute_n(struct strategy_ctx *ctx, int k);

/*
 * Update path stats from a received probe echo.
 * received: true if echo arrived, false if probe timed out.
 * rtt_us: round-trip time in microseconds (ignored when received=false).
 */
void strategy_update_probe(struct strategy_ctx *ctx,
                            int path_idx, uint64_t rtt_us, bool received);

struct path_state *strategy_get_path_state(struct strategy_ctx *ctx, int idx);
int strategy_path_count(struct strategy_ctx *ctx);

/*
 * Hot-reload strategy parameters from a freshly parsed config.
 * Updates base_redundancy, loss_threshold, strategy type, and per-path
 * weight/enabled flags.  Preserves runtime state (loss_rate, rtt_ms, alive,
 * probe counters) so adaptive feedback is not lost across reloads.
 */
void strategy_reload(struct strategy_ctx *ctx, const struct gateway_config *cfg);

#endif /* STRATEGY_H */
