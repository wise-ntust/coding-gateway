#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "strategy.h"
#include "common.h"

#define EWMA_ALPHA      0.2f
#define MAX_REDUNDANCY  3.0f

struct strategy_ctx {
    struct path_state paths[MAX_PATHS];
    int   path_count;
    int   rr_idx;
    char  type[16];
    float base_redundancy;
    float loss_threshold;
};

struct strategy_ctx *strategy_init(const struct gateway_config *cfg)
{
    struct strategy_ctx *ctx;
    int i;

    ctx = (struct strategy_ctx *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->path_count      = cfg->path_count;
    ctx->base_redundancy = cfg->redundancy_ratio;
    ctx->loss_threshold  = cfg->probe_loss_threshold;
    snprintf(ctx->type, sizeof(ctx->type), "%s", cfg->strategy_type);

    for (i = 0; i < cfg->path_count; i++) {
        ctx->paths[i].cfg            = cfg->paths[i];
        ctx->paths[i].weight_current = cfg->paths[i].weight;
        ctx->paths[i].alive          = cfg->paths[i].enabled;
        ctx->paths[i].rtt_ms         = 5.0f;
        ctx->paths[i].loss_rate      = 0.0f;
    }
    return ctx;
}

void strategy_free(struct strategy_ctx *ctx) { free(ctx); }

int strategy_next_path(struct strategy_ctx *ctx)
{
    int i, idx;
    for (i = 0; i < ctx->path_count; i++) {
        idx = (ctx->rr_idx + i) % ctx->path_count;
        if (ctx->paths[idx].cfg.enabled && ctx->paths[idx].alive) {
            ctx->rr_idx = (idx + 1) % ctx->path_count;
            return idx;
        }
    }
    return -1;
}

int strategy_compute_n(struct strategy_ctx *ctx, int k)
{
    int n_alive = 0, n_total = 0, n, i;
    float ratio;

    if (k < 1) return -1;

    for (i = 0; i < ctx->path_count; i++) {
        if (!ctx->paths[i].cfg.enabled) continue;
        n_total++;
        if (ctx->paths[i].alive) n_alive++;
    }

    ratio = ctx->base_redundancy;
    if (n_alive == 0) {
        ratio = MAX_REDUNDANCY;
    } else if (n_alive < n_total) {
        ratio = ctx->base_redundancy * ((float)n_total / (float)n_alive);
        if (ratio > MAX_REDUNDANCY) ratio = MAX_REDUNDANCY;
    }

    n = (int)ceilf((float)k * ratio);
    if (n < k)     n = k;
    if (n > MAX_N) n = MAX_N;
    return n;
}

void strategy_update_probe(struct strategy_ctx *ctx,
                            int path_idx, uint64_t rtt_us, bool received)
{
    struct path_state *p;
    float observed_loss;

    if (path_idx < 0 || path_idx >= ctx->path_count) return;
    p = &ctx->paths[path_idx];

    p->probes_sent++;
    if (received) {
        float rtt_ms;
        p->probes_recv++;
        rtt_ms = (float)rtt_us / 1000.0f;
        p->rtt_ms = EWMA_ALPHA * rtt_ms + (1.0f - EWMA_ALPHA) * p->rtt_ms;
    }

    observed_loss = received ? 0.0f : 1.0f;
    p->loss_rate = EWMA_ALPHA * observed_loss +
                   (1.0f - EWMA_ALPHA) * p->loss_rate;

    /* Hysteresis: only flip alive state at 1x and 0.5x threshold. */
    if (p->loss_rate > ctx->loss_threshold)
        p->alive = false;
    if (p->loss_rate < ctx->loss_threshold * 0.5f)
        p->alive = true;
}

struct path_state *strategy_get_path_state(struct strategy_ctx *ctx, int idx)
{
    if (idx < 0 || idx >= ctx->path_count) return NULL;
    return &ctx->paths[idx];
}

int strategy_path_count(struct strategy_ctx *ctx)
{
    return ctx->path_count;
}
