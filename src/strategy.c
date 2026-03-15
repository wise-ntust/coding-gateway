#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "strategy.h"
#include "common.h"

#define MAX_REDUNDANCY  3.0f

struct strategy_ctx {
    struct path_state paths[MAX_PATHS];
    int   path_count;
    int   rr_idx;
    char  type[16];
    float base_redundancy;
    float loss_threshold;
    float ewma_alpha;
    int   path_credit[MAX_PATHS];
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
    ctx->ewma_alpha      = (cfg->ewma_alpha > 0.0f) ? cfg->ewma_alpha : 0.2f;
    snprintf(ctx->type, sizeof(ctx->type), "%s", cfg->strategy_type);

    for (i = 0; i < cfg->path_count; i++) {
        ctx->paths[i].cfg            = cfg->paths[i];
        ctx->paths[i].weight_current = cfg->paths[i].weight;
        ctx->paths[i].alive          = cfg->paths[i].enabled;
        ctx->paths[i].rtt_ms         = 5.0f;
        ctx->paths[i].loss_rate      = 0.0f;
        ctx->path_credit[i]          = (int)roundf(cfg->paths[i].weight);
        if (ctx->path_credit[i] < 1) ctx->path_credit[i] = 1;
    }
    return ctx;
}

void strategy_free(struct strategy_ctx *ctx) { free(ctx); }

int strategy_next_path(struct strategy_ctx *ctx)
{
    int i, best, any_alive;

    /* Check if any alive+enabled path exists. */
    any_alive = 0;
    for (i = 0; i < ctx->path_count; i++) {
        if (ctx->paths[i].cfg.enabled && ctx->paths[i].alive) {
            any_alive = 1;
            break;
        }
    }
    if (!any_alive) return -1;

    /* Refill credits if all alive+enabled paths are at or below 0. */
    {
        int need_refill = 1;
        for (i = 0; i < ctx->path_count; i++) {
            if (ctx->paths[i].cfg.enabled && ctx->paths[i].alive &&
                ctx->path_credit[i] > 0) {
                need_refill = 0;
                break;
            }
        }
        if (need_refill) {
            for (i = 0; i < ctx->path_count; i++) {
                if (ctx->paths[i].cfg.enabled && ctx->paths[i].alive) {
                    ctx->path_credit[i] =
                        (int)roundf(ctx->paths[i].weight_current);
                    if (ctx->path_credit[i] < 1) ctx->path_credit[i] = 1;
                }
            }
        }
    }

    /* Pick alive+enabled path with highest credit, breaking ties by index
     * (lowest index first, which preserves original RR behaviour for equal
     * weights when credits are always equal). */
    best = -1;
    for (i = 0; i < ctx->path_count; i++) {
        if (!ctx->paths[i].cfg.enabled || !ctx->paths[i].alive) continue;
        if (best == -1 || ctx->path_credit[i] > ctx->path_credit[best])
            best = i;
    }

    ctx->path_credit[best]--;
    return best;
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
        p->rtt_ms = ctx->ewma_alpha * rtt_ms + (1.0f - ctx->ewma_alpha) * p->rtt_ms;
    }

    observed_loss = received ? 0.0f : 1.0f;
    p->loss_rate = ctx->ewma_alpha * observed_loss +
                   (1.0f - ctx->ewma_alpha) * p->loss_rate;

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

void strategy_reload(struct strategy_ctx *ctx, const struct gateway_config *cfg)
{
    int i;

    ctx->base_redundancy = cfg->redundancy_ratio;
    ctx->loss_threshold  = cfg->probe_loss_threshold;
    ctx->ewma_alpha      = (cfg->ewma_alpha > 0.0f) ? cfg->ewma_alpha : 0.2f;
    snprintf(ctx->type, sizeof(ctx->type), "%s", cfg->strategy_type);

    /* Update per-path config while preserving runtime state. */
    for (i = 0; i < ctx->path_count && i < cfg->path_count; i++) {
        ctx->paths[i].cfg.weight  = cfg->paths[i].weight;
        ctx->paths[i].cfg.enabled = cfg->paths[i].enabled;
        ctx->paths[i].weight_current = cfg->paths[i].weight;
        /* If a path was just disabled via config, mark it dead immediately. */
        if (!cfg->paths[i].enabled) {
            ctx->paths[i].alive = false;
        } else {
            /* Update credit ceiling to reflect new weight. */
            ctx->path_credit[i] = (int)roundf(cfg->paths[i].weight);
            if (ctx->path_credit[i] < 1) ctx->path_credit[i] = 1;
        }
    }

    LOG_INFO("strategy reloaded: type=%s redundancy=%.2f loss_thresh=%.2f",
             ctx->type, ctx->base_redundancy, ctx->loss_threshold);
}
