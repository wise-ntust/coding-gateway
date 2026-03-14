# Four Improvements Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add no-FEC baseline experiment, strategy+transport unit tests, IPv6 support, and split main.c into focused modules.

**Architecture:** Four independent tasks executed sequentially. Task 1 (baseline experiment) is a pure eval script with no code changes. Task 2 (unit tests) adds test files only. Task 3 (IPv6) modifies the packet-length extraction in the RX decode path. Task 4 (main.c split) is a refactor that extracts TX/RX logic into separate modules while main.c retains only init + event loop.

**Tech Stack:** C99/POSIX, Docker/tc netem, shell scripts, Makefile

---

## File Structure

### New files
- `scripts/eval/e0_baseline_no_fec.sh` — no-FEC baseline experiment (30 reps)
- `src/test_strategy.c` — strategy module unit tests
- `src/test_transport.c` — transport wire protocol unit tests
- `src/tx.c` — TX block assembly + flush logic (extracted from main.c)
- `src/rx.c` — RX window + decode logic (extracted from main.c)
- `include/tx.h` — TX module public API
- `include/rx.h` — RX module public API

### Modified files
- `src/main.c` — shrink to init + event loop (delegate to tx.c/rx.c)
- `Makefile` — add test_strategy and test_transport build rules
- `README.md` — add baseline results, update test count

---

## Chunk 1: Tasks 1–2 (Baseline Experiment + Unit Tests)

### Task 1: No-FEC Baseline Experiment

**Files:**
- Create: `scripts/eval/e0_baseline_no_fec.sh`

The experiment runs the tunnel with `redundancy_ratio=1.0` (n=k, no redundancy shards) to establish a baseline. Same loss sweep as E1 (0–70%), 30 reps per data point. This proves FEC's value by comparing against a tunnel with identical overhead but zero coding gain.

- [ ] **Step 1: Create the baseline experiment script**

```sh
#!/bin/sh
# E0: No-FEC baseline — redundancy_ratio=1.0 (n=k, no redundant shards)
# Patches docker-tx.conf and docker-rx.conf to set ratio=1.0 at startup,
# then runs E1-style loss sweep with 30 reps.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e0_baseline.csv"
SUMMARY="$RESULTS_DIR/e0_baseline_summary.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

echo "loss_pct,rep,success_rate,config" > "$CSV"

# Build and start with default config
docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 5

# Patch redundancy_ratio to 1.0 (no FEC) inside both containers
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    sed -i 's/redundancy_ratio = .*/redundancy_ratio = 1.0/' /app/config/docker-tx.conf
docker compose -f "$COMPOSE_FILE" exec -T rx-node \
    sed -i 's/redundancy_ratio = .*/redundancy_ratio = 1.0/' /app/config/docker-rx.conf

# SIGHUP both gateways to pick up new ratio
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
docker compose -f "$COMPOSE_FILE" exec -T rx-node \
    sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
sleep 3

# Verify baseline
if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E0] FAIL: baseline connectivity"
    exit 1
fi
echo "  baseline OK (ratio=1.0) — ${REPS} reps per loss level"

for loss in 0 10 20 30 40 50 60 70; do
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc del dev eth0 root 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc add dev eth0 root netem loss "${loss}%"
    sleep 1

    rep=1
    while [ "$rep" -le "$REPS" ]; do
        PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 20 -i 0.2 -W 2 10.0.0.2 2>&1 || true)
        LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
            for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
        }')
        [ -z "$LOSS_PCT" ] && LOSS_PCT=100
        RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
        echo "$loss,$rep,$RATE,no_fec" >> "$CSV"
        rep=$((rep + 1))
    done
    echo "  loss=${loss}%: ${REPS} reps done"
done

# Summary
echo "loss_pct,mean,std,n,config" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1; sum[key]+=$3; sumsq[key]+=$3*$3; n[key]++
} END {
    for (k in sum) {
        m = sum[k]/n[k]; v = (sumsq[k]/n[k]) - m*m
        if (v<0) v=0; s = sqrt(v)
        printf "%s,%.2f,%.2f,%d,no_fec\n", k, m, s, n[k]
    }
}' "$CSV" | sort -t, -k1 -n >> "$SUMMARY"

echo "[E0] Done. Summary:"
cat "$SUMMARY"
```

- [ ] **Step 2: Make executable and run**

Run: `chmod +x scripts/eval/e0_baseline_no_fec.sh && sh scripts/eval/e0_baseline_no_fec.sh`
Expected: CSV with 240 rows (8 loss × 30 reps), summary with mean±std per loss level. No-FEC success rate should closely track `100 - loss_pct` (no coding gain).

- [ ] **Step 3: Update README.md Evaluation section**

Add E0 baseline row to the E1 comparison table. Add a "no_fec" column.

- [ ] **Step 4: Commit**

```bash
git add scripts/eval/e0_baseline_no_fec.sh README.md
git commit -m "eval: add E0 no-FEC baseline experiment (N=30)"
git push
```

---

### Task 2: Strategy Unit Tests

**Files:**
- Create: `src/test_strategy.c`
- Modify: `Makefile` (add build rule)

Tests cover: init, round-robin path selection, compute_n with all/some/no paths alive, EWMA probe update, hysteresis dead/alive flip, reload preserving runtime state, edge cases (out-of-bounds path_idx, k < 1).

- [ ] **Step 1: Write test_strategy.c**

```c
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
    assert(strategy_next_path(ctx) == 0); /* wraps */
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
    /* n = ceil(4 * 1.5) = 6 */
    assert(strategy_compute_n(ctx, 4) == 6);
    /* n = ceil(2 * 1.5) = 3 */
    assert(strategy_compute_n(ctx, 2) == 3);
    strategy_free(ctx);
}

static void test_compute_n_scales_when_path_dead(void)
{
    struct gateway_config cfg = make_cfg(2, 1.5f, 0.3f);
    struct strategy_ctx *ctx = strategy_init(&cfg);
    /* Kill path 0 via probe losses */
    int i;
    for (i = 0; i < 20; i++)
        strategy_update_probe(ctx, 0, 0, false);
    /* With 1/2 paths alive, ratio doubles: 1.5 * 2 = 3.0 */
    /* n = ceil(2 * 3.0) = 6 */
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

    /* Send many lost probes to push loss_rate above threshold */
    int i;
    for (i = 0; i < 20; i++)
        strategy_update_probe(ctx, 0, 0, false);
    assert(ps->alive == false);

    /* Send many successful probes to push loss below 0.5*threshold */
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
    /* Should not crash */
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

    /* Accumulate some runtime state */
    strategy_update_probe(ctx, 0, 5000, true);
    strategy_update_probe(ctx, 1, 10000, true);
    struct path_state *ps0 = strategy_get_path_state(ctx, 0);
    float rtt_before = ps0->rtt_ms;

    /* Reload with new ratio */
    struct gateway_config cfg2 = make_cfg(2, 2.5f, 0.5f);
    strategy_reload(ctx, &cfg2);

    /* Runtime state preserved */
    assert(ps0->rtt_ms == rtt_before);
    /* Config updated */
    assert(strategy_compute_n(ctx, 2) >= 5); /* ceil(2 * 2.5) = 5 */
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
```

- [ ] **Step 2: Add Makefile build rule**

Add after the `test_config` rule:

```makefile
build/test_strategy: src/test_strategy.c build/strategy.o build/config.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

- [ ] **Step 3: Run tests**

Run: `make test`
Expected: All existing tests pass + `strategy: all tests passed`

- [ ] **Step 4: Write test_transport.c**

Tests wire protocol serialization: build a data shard buffer manually, call transport_recv-equivalent parsing logic. Since transport_recv requires real sockets, test the wire format serialization/deserialization logic by building buffers and verifying header fields.

```c
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "transport.h"
#include "common.h"

static void test_wire_header_size(void)
{
    assert(WIRE_HDR_SIZE == 14);
}

static void test_wire_header_packing(void)
{
    /* Build a header and verify byte layout matches spec */
    uint8_t buf[WIRE_HDR_SIZE];
    struct wire_header *h = (struct wire_header *)buf;
    memset(buf, 0, sizeof(buf));

    h->magic       = htons(WIRE_MAGIC);
    h->version     = WIRE_VERSION;
    h->type        = TYPE_DATA;
    h->block_id    = htonl(42);
    h->shard_idx   = 3;
    h->k           = 4;
    h->n           = 6;
    h->reserved    = 0;
    h->payload_len = htons(100);

    /* Verify wire bytes at known offsets */
    assert(buf[0] == 0xC0 && buf[1] == 0xDE);        /* magic */
    assert(buf[2] == 0x01);                            /* version */
    assert(buf[3] == TYPE_DATA);                       /* type */
    assert(buf[4] == 0 && buf[5] == 0 && buf[6] == 0 && buf[7] == 42); /* block_id */
    assert(buf[8] == 3);                               /* shard_idx */
    assert(buf[9] == 4);                               /* k */
    assert(buf[10] == 6);                              /* n */
    assert(buf[12] == 0 && buf[13] == 100);            /* payload_len */
}

static void test_wire_constants(void)
{
    assert(WIRE_MAGIC == 0xC0DE);
    assert(WIRE_VERSION == 0x01);
    assert(TYPE_DATA == 0x01);
    assert(TYPE_PROBE == 0x02);
    assert(TYPE_PROBE_ECHO == 0x03);
}

static void test_probe_timestamp_encoding(void)
{
    /* Verify the 8-byte timestamp encoding used by probe packets */
    uint64_t ts = 0x0001020304050607ULL;
    uint32_t hi = htonl((uint32_t)(ts >> 32));
    uint32_t lo = htonl((uint32_t)(ts & 0xFFFFFFFFu));

    /* Decode back */
    uint64_t decoded = ((uint64_t)ntohl(hi) << 32) | (uint64_t)ntohl(lo);
    assert(decoded == ts);
}

static void test_shard_struct_fits_max(void)
{
    struct shard s;
    assert(sizeof(s.coeffs) >= MAX_K);
    assert(sizeof(s.data) >= MAX_PAYLOAD);
}

int main(void)
{
    test_wire_header_size();
    test_wire_header_packing();
    test_wire_constants();
    test_probe_timestamp_encoding();
    test_shard_struct_fits_max();
    printf("transport: all tests passed\n");
    return 0;
}
```

- [ ] **Step 5: Add Makefile build rule for test_transport**

```makefile
build/test_transport: src/test_transport.c | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

- [ ] **Step 6: Run all tests**

Run: `make test`
Expected: 5 test suites all pass (gf256, codec, config, strategy, transport)

- [ ] **Step 7: Commit**

```bash
git add src/test_strategy.c src/test_transport.c Makefile
git commit -m "test: add strategy and transport unit tests"
git push
```

---

## Chunk 2: Tasks 3–4 (IPv6 + main.c Split)

### Task 3: IPv6 Support in Decoded Packet Length

**Files:**
- Modify: `src/main.c:224-232` (try_decode_block IP length extraction)

Currently the code reads IPv4 total-length from bytes 2-3. IPv6 has version=6 in the upper nibble of byte 0, and payload length at bytes 4-5 (which excludes the 40-byte fixed header).

- [ ] **Step 1: Write a failing integration test idea**

No unit test possible (requires TUN + Docker). The fix is small enough to verify by code inspection + existing Docker tests. However, we verify the logic is correct:

- IPv4: version nibble = 4, total length = bytes 2-3 (includes header)
- IPv6: version nibble = 6, payload length = bytes 4-5, total = payload_length + 40

- [ ] **Step 2: Update try_decode_block in main.c**

Replace the IP length extraction block (lines 224-232) with:

```c
    for (i = 0; i < k; i++) {
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
```

- [ ] **Step 3: Build and verify zero warnings**

Run: `make clean && make`
Expected: Zero warnings

- [ ] **Step 4: Run existing tests to verify no regression**

Run: `make test`
Expected: All 5 test suites pass

- [ ] **Step 5: Commit**

```bash
git add src/main.c
git commit -m "feat: add IPv6 support in decoded packet length extraction"
git push
```

---

### Task 4: Split main.c into TX, RX, and Main Modules

**Files:**
- Create: `include/tx.h`, `include/rx.h`, `src/tx.c`, `src/rx.c`
- Modify: `src/main.c` (reduce to init + event loop)

**Decomposition:**

| Module | Responsibility | Structs/Functions |
|--------|---------------|-------------------|
| `tx.h/tx.c` | Block assembly, padding, flush | `struct tx_block`, `tx_block_init()`, `tx_block_add_pkt()`, `tx_block_flush()`, `tx_block_needs_flush()` |
| `rx.h/rx.c` | Window management, decode, advance | `struct rx_block`, `struct rx_window`, `rx_window_init()`, `rx_window_insert()`, `rx_window_try_decode()`, `rx_window_advance()` |
| `main.c` | Init, event loop, signal handling, metrics | Calls tx/rx APIs, no direct struct access |

- [ ] **Step 1: Create include/tx.h**

```c
#ifndef TX_H
#define TX_H

#include <stdint.h>
#include <sys/time.h>
#include "common.h"
#include "transport.h"
#include "strategy.h"
#include "metrics.h"

struct tx_block {
    uint32_t       block_id;
    uint8_t        pkts[MAX_K][MAX_PAYLOAD];
    uint16_t       pkt_len[MAX_K];
    int            pkt_count;
    struct timeval first_pkt_time;
};

void tx_block_init(struct tx_block *blk, uint32_t block_id);

/* Add a packet to the block. Returns true if block is now full (pkt_count >= k). */
bool tx_block_add_pkt(struct tx_block *blk, const uint8_t *pkt, uint16_t len, int k);

/* Returns true if block has packets and timeout has elapsed. */
bool tx_block_needs_flush(const struct tx_block *blk, int timeout_ms);

/* Encode and send all shards. Resets block for next use. */
void tx_block_flush(struct tx_block *blk,
                    struct transport_ctx *tctx,
                    struct strategy_ctx *sctx,
                    int k);

#endif /* TX_H */
```

- [ ] **Step 2: Create src/tx.c**

Extract `flush_block()` logic + new helpers from main.c.

- [ ] **Step 3: Create include/rx.h**

```c
#ifndef RX_H
#define RX_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "codec.h"
#include "transport.h"
#include "metrics.h"

struct rx_block {
    uint32_t     block_id;
    struct shard shards[MAX_N];
    bool         received[MAX_N];
    int          recv_count;
    bool         decoded;
    uint64_t     first_recv_us;
};

struct rx_window {
    struct rx_block slots[MAX_WINDOW];
    uint32_t        base_id;
};

void rx_window_init(struct rx_window *win);
void rx_window_insert(struct rx_window *win, const struct wire_header *hdr,
                      const struct shard *s, int window_size);

/* Try to decode a block; returns true if newly decoded this call. */
bool rx_window_try_decode(struct rx_window *win, uint32_t block_id,
                          int k, int window_size, int tun_fd);

void rx_window_advance(struct rx_window *win, int window_size);

#endif /* RX_H */
```

- [ ] **Step 4: Create src/rx.c**

Extract `rx_window_find()`, `rx_window_insert()`, `try_decode_block()`, `rx_window_advance()` from main.c. The `now_us()` helper is shared — move to a static inline in `common.h` or duplicate in both modules.

- [ ] **Step 5: Update main.c**

Remove all struct definitions and extracted functions. Replace with `#include "tx.h"` and `#include "rx.h"`. The event loop calls the new APIs:

```c
/* TX: read from TUN */
if (tx_block_add_pkt(&tx, pkt_buf, pkt_len, cfg.k))
    tx_block_flush(&tx, tctx, sctx, cfg.k);

/* TX: timeout flush */
if (tx_block_needs_flush(&tx, cfg.block_timeout_ms))
    tx_block_flush(&tx, tctx, sctx, cfg.k);

/* RX: insert shard and try decode */
rx_window_insert(&rx_win, &hdr, &shard_in, cfg.window_size);
if (rx_window_try_decode(&rx_win, hdr.block_id, hdr.k, cfg.window_size, tun_fd))
    g_metrics.decode_success++;
rx_window_advance(&rx_win, cfg.window_size);
```

- [ ] **Step 6: Add `now_us()` to common.h as static inline**

```c
#include <sys/time.h>
static inline uint64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}
```

- [ ] **Step 7: Build and verify zero warnings**

Run: `make clean && make`
Expected: Zero warnings. The wildcard `$(wildcard src/*.c)` picks up tx.c and rx.c automatically.

- [ ] **Step 8: Run all tests**

Run: `make test`
Expected: All 5 test suites pass (tx.c/rx.c don't have their own tests — existing integration tests cover them)

- [ ] **Step 9: Run Docker integration test T01**

Run: `sh scripts/test_01_basic.sh`
Expected: PASS — verifies the refactor didn't break the tunnel

- [ ] **Step 10: Verify final line counts**

Run: `wc -l src/main.c src/tx.c src/rx.c`
Expected: main.c ~200 lines, tx.c ~80 lines, rx.c ~150 lines

- [ ] **Step 11: Update README.md**

Add IPv6 support note. Update architecture description if needed.

- [ ] **Step 12: Commit**

```bash
git add include/tx.h include/rx.h src/tx.c src/rx.c src/main.c include/common.h README.md
git commit -m "refactor: split main.c into tx.c, rx.c modules; add IPv6 support"
git push
```
