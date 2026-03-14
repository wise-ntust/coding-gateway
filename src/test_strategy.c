#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "strategy.h"

static struct gateway_config make_cfg(int n_paths, float ratio, float thresh)
{
    struct gateway_config cfg;
    int i;
    memset(&cfg, 0, sizeof(cfg));
    cfg.redundancy_ratio = ratio;
    cfg.probe_loss_threshold = thresh;
    strncpy(cfg.strategy_type, "adaptive", sizeof(cfg.strategy_type) - 1);
    cfg.path_count = n_paths;
    for (i = 0; i < n_paths; i++) {
        snprintf(cfg.paths[i].name, sizeof(cfg.paths[i].name), "p%d", i);
        cfg.paths[i].weight = 1.0f;
        cfg.paths[i].enabled = true;
    }
    return cfg;
}

static void test_init_and_path_count(void)
{
    struct gateway_config cfg = make_cfg(3, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);
    assert(ctx != NULL);
    assert(strategy_path_count(ctx) == 3);
    strategy_free(ctx);
}

static void test_next_path_round_robin(void)
{
    struct gateway_config cfg = make_cfg(3, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);
    assert(strategy_next_path(ctx) == 0);
    assert(strategy_next_path(ctx) == 1);
    assert(strategy_next_path(ctx) == 2);
    assert(strategy_next_path(ctx) == 0);
    strategy_free(ctx);
}

static void test_next_path_skips_disabled(void)
{
    struct gateway_config cfg = make_cfg(3, 1.5f, 0.3f);
    cfg.paths[1].enabled = false;
    struct strategy_ctx *ctx = strategy_init(&cfg);
    assert(strategy_next_path(ctx) == 0);
    assert(strategy_next_path(ctx) == 2);
    assert(strategy_next_path(ctx) == 0);
    strategy_free(ctx);
}

static void test_next_path_all_dead(void)
{
    struct gateway_config cfg = make_cfg(2, 1.5f, 0.3f);
    cfg.paths[0].enabled = false;
    cfg.paths[1].enabled = false;
    struct strategy_ctx *ctx = strategy_init(&cfg);
    assert(strategy_next_path(ctx) == -1);
    strategy_free(ctx);
}

static void test_compute_n_basic(void)
{
    struct gateway_config cfg = make_cfg(2, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);
    assert(strategy_compute_n(ctx, 4) == 6);
    assert(strategy_compute_n(ctx, 2) == 3);
    strategy_free(ctx);
}

static void test_compute_n_scales_when_path_dead(void)
{
    struct gateway_config cfg = make_cfg(2, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);
    int i;
    for (i = 0; i < 20; i++)
        strategy_update_probe(ctx, 0, 0, false);
    assert(strategy_compute_n(ctx, 2) == 6);
    strategy_free(ctx);
}

static void test_compute_n_invalid_k(void)
{
    struct gateway_config cfg = make_cfg(1, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);
    assert(strategy_compute_n(ctx, 0) == -1);
    assert(strategy_compute_n(ctx, -1) == -1);
    strategy_free(ctx);
}

static void test_probe_ewma_and_hysteresis(void)
{
    struct gateway_config cfg = make_cfg(1, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);
    struct path_state *ps = strategy_get_path_state(ctx, 0);
    assert(ps->alive == true);

    int i;
    for (i = 0; i < 20; i++)
        strategy_update_probe(ctx, 0, 0, false);
    assert(ps->alive == false);

    for (i = 0; i < 30; i++)
        strategy_update_probe(ctx, 0, 1000, true);
    assert(ps->alive == true);
    assert(ps->rtt_ms > 0.0f && ps->rtt_ms < 5.0f);
    strategy_free(ctx);
}

static void test_probe_out_of_bounds(void)
{
    struct gateway_config cfg = make_cfg(1, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);
    strategy_update_probe(ctx, -1, 1000, true);
    strategy_update_probe(ctx, 99, 1000, true);
    strategy_free(ctx);
}

static void test_get_path_state_bounds(void)
{
    struct gateway_config cfg = make_cfg(2, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);
    assert(strategy_get_path_state(ctx, 0) != NULL);
    assert(strategy_get_path_state(ctx, 1) != NULL);
    assert(strategy_get_path_state(ctx, 2) == NULL);
    assert(strategy_get_path_state(ctx, -1) == NULL);
    strategy_free(ctx);
}

static void test_reload_preserves_runtime(void)
{
    struct gateway_config cfg = make_cfg(2, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);

    strategy_update_probe(ctx, 0, 5000, true);
    strategy_update_probe(ctx, 1, 10000, true);
    struct path_state *ps0 = strategy_get_path_state(ctx, 0);
    float rtt_before = ps0->rtt_ms;

    struct gateway_config cfg2 = make_cfg(2, 2.5f, 0.5f);
    strategy_reload(ctx, &cfg2);

    assert(ps0->rtt_ms == rtt_before);
    assert(strategy_compute_n(ctx, 2) >= 5);
    strategy_free(ctx);
}

int main(void)
{
    test_init_and_path_count();
    test_next_path_round_robin();
    test_next_path_skips_disabled();
    test_next_path_all_dead();
    test_compute_n_basic();
    test_compute_n_scales_when_path_dead();
    test_compute_n_invalid_k();
    test_probe_ewma_and_hysteresis();
    test_probe_out_of_bounds();
    test_get_path_state_bounds();
    test_reload_preserves_runtime();
    printf("strategy: all tests passed\n");
    return 0;
}
