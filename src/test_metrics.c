#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "metrics.h"

static void test_latency_buckets_ordered(void)
{
    int i;
    for (i = 1; i < METRICS_LATENCY_BUCKETS; i++)
        assert(metrics_latency_bounds[i] > metrics_latency_bounds[i - 1]);
}

static void test_record_latency_single(void)
{
    memset(&g_metrics, 0, sizeof(g_metrics));
    metrics_record_latency(3000); /* 3ms */

    assert(g_metrics.latency_count == 1);
    assert(g_metrics.latency_sum_ms > 2.9 && g_metrics.latency_sum_ms < 3.1);
    /* 3ms falls in bucket[1] (<=5ms) and all above */
    assert(g_metrics.latency_bucket[0] == 0); /* <=1ms: no */
    assert(g_metrics.latency_bucket[1] == 1); /* <=5ms: yes */
    assert(g_metrics.latency_bucket[2] == 1); /* <=10ms: yes */
}

static void test_record_latency_cumulative(void)
{
    memset(&g_metrics, 0, sizeof(g_metrics));
    metrics_record_latency(500);   /* 0.5ms → bucket[0]+ */
    metrics_record_latency(8000);  /* 8ms → bucket[2]+ */
    metrics_record_latency(200000); /* 200ms → bucket[6]+ */

    assert(g_metrics.latency_count == 3);
    assert(g_metrics.latency_bucket[0] == 1); /* <=1ms: 0.5ms */
    assert(g_metrics.latency_bucket[1] == 1); /* <=5ms: 0.5ms */
    assert(g_metrics.latency_bucket[2] == 2); /* <=10ms: 0.5ms + 8ms */
    assert(g_metrics.latency_bucket[6] == 3); /* <=250ms: all three */
}

static void test_record_latency_above_all_buckets(void)
{
    memset(&g_metrics, 0, sizeof(g_metrics));
    metrics_record_latency(1000000); /* 1000ms — above all buckets */

    assert(g_metrics.latency_count == 1);
    /* No bucket should contain it (all bounds < 1000ms) */
    int i;
    for (i = 0; i < METRICS_LATENCY_BUCKETS; i++)
        assert(g_metrics.latency_bucket[i] == 0);
}

static void test_metrics_zero_init(void)
{
    memset(&g_metrics, 0, sizeof(g_metrics));
    assert(g_metrics.decode_success == 0);
    assert(g_metrics.decode_failure == 0);
    assert(g_metrics.blocks_encoded == 0);
    assert(g_metrics.latency_count == 0);

    int i;
    for (i = 0; i < MAX_PATHS; i++) {
        assert(g_metrics.shards_sent[i] == 0);
        assert(g_metrics.shards_received[i] == 0);
    }
}

static void test_counter_increment(void)
{
    memset(&g_metrics, 0, sizeof(g_metrics));
    g_metrics.decode_success++;
    g_metrics.decode_success++;
    g_metrics.shards_sent[0] += 10;
    g_metrics.shards_sent[1] += 5;

    assert(g_metrics.decode_success == 2);
    assert(g_metrics.shards_sent[0] == 10);
    assert(g_metrics.shards_sent[1] == 5);
}

int main(void)
{
    test_latency_buckets_ordered();
    test_record_latency_single();
    test_record_latency_cumulative();
    test_record_latency_above_all_buckets();
    test_metrics_zero_init();
    test_counter_increment();
    printf("metrics: all tests passed (6 cases)\n");
    return 0;
}
