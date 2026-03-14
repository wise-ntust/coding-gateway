#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "metrics.h"

struct metrics g_metrics;

const double metrics_latency_bounds[METRICS_LATENCY_BUCKETS] = {
    1.0, 5.0, 10.0, 25.0, 50.0, 100.0, 250.0, 500.0
};

void metrics_record_latency(uint64_t latency_us)
{
    double ms = (double)latency_us / 1000.0;
    int i;
    for (i = 0; i < METRICS_LATENCY_BUCKETS; i++) {
        if (ms <= metrics_latency_bounds[i])
            g_metrics.latency_bucket[i]++;
    }
    g_metrics.latency_count++;
    g_metrics.latency_sum_ms += ms;
}

int metrics_listen(uint16_t port)
{
    int fd, opt = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERR("metrics: socket() failed: %s", strerror(errno));
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Non-blocking so accept() never stalls the event loop. */
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERR("metrics: bind port %u failed: %s", (unsigned)port, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        LOG_ERR("metrics: listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    LOG_INFO("metrics: listening on port %u", (unsigned)port);
    return fd;
}

/* Max response size — generous for up to MAX_PATHS paths. */
#define RESP_MAX 8192

void metrics_handle(int listener_fd,
                    const char (*path_names)[32],
                    int path_count,
                    const float *loss_rates,
                    const float *rtt_ms,
                    float redundancy_ratio)
{
    int conn;
    char body[RESP_MAX];
    char resp[RESP_MAX + 256];
    int blen = 0, rlen, i;
    uint64_t cum;

    conn = accept(listener_fd, NULL, NULL);
    if (conn < 0) return;

    /* We don't parse the HTTP request — just respond to any connection. */

    /* --- counters with path labels --- */
    for (i = 0; i < path_count; i++) {
        blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
            "coding_gateway_shards_sent_total{path=\"%s\"} %llu\n",
            path_names[i], (unsigned long long)g_metrics.shards_sent[i]);
    }
    for (i = 0; i < path_count; i++) {
        blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
            "coding_gateway_shards_received_total{path=\"%s\"} %llu\n",
            path_names[i], (unsigned long long)g_metrics.shards_received[i]);
    }

    /* --- decode counters --- */
    blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
        "coding_gateway_decode_success_total %llu\n",
        (unsigned long long)g_metrics.decode_success);
    blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
        "coding_gateway_decode_failure_total %llu\n",
        (unsigned long long)g_metrics.decode_failure);
    blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
        "coding_gateway_blocks_encoded_total %llu\n",
        (unsigned long long)g_metrics.blocks_encoded);

    /* --- per-path gauges --- */
    for (i = 0; i < path_count; i++) {
        blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
            "coding_gateway_path_loss_rate{path=\"%s\"} %.6f\n",
            path_names[i], (double)loss_rates[i]);
        blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
            "coding_gateway_path_rtt_ms{path=\"%s\"} %.3f\n",
            path_names[i], (double)rtt_ms[i]);
    }

    blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
        "coding_gateway_redundancy_ratio_current %.4f\n",
        (double)redundancy_ratio);

    /* --- latency histogram --- */
    cum = 0;
    for (i = 0; i < METRICS_LATENCY_BUCKETS; i++) {
        cum += g_metrics.latency_bucket[i];
        blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
            "coding_gateway_block_latency_ms_bucket{le=\"%.0f\"} %llu\n",
            metrics_latency_bounds[i], (unsigned long long)cum);
    }
    blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
        "coding_gateway_block_latency_ms_bucket{le=\"+Inf\"} %llu\n",
        (unsigned long long)g_metrics.latency_count);
    blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
        "coding_gateway_block_latency_ms_sum %.3f\n",
        g_metrics.latency_sum_ms);
    blen += snprintf(body + blen, (size_t)(RESP_MAX - blen),
        "coding_gateway_block_latency_ms_count %llu\n",
        (unsigned long long)g_metrics.latency_count);

    /* HTTP response */
    rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        blen, body);

    /* Best-effort write; don't retry on partial sends for a metrics endpoint. */
    (void)write(conn, resp, (size_t)rlen);
    close(conn);
}
