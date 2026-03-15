# 4-Node Topology + IP Forwarding Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `[forward]` config section so coding-gateway auto-enables IP forwarding and installs routes on startup, then validate with a 4-container iperf3 experiment (client-tx → ap-tx →[coded UDP]→ ap-rx → client-rx).

**Architecture:** coding-gateway reads `ip_forward` and `route` keys from a new `[forward]` config section; after `tun_configure()`, it writes `/proc/sys/net/ipv4/ip_forward` and calls `ip route add` for each declared route. The Docker topology adds two client containers and two LAN networks flanking the existing mmWave networks, letting real application traffic traverse the coded tunnel with no per-experiment shell setup.

**Tech Stack:** C99 POSIX, Linux proc/sysctl, iproute2 (`ip route add`), Docker Compose, iperf3 POSIX sh eval scripts.

---

## Chunk 1: Config struct + parser + unit test

### Task 1: Add `[forward]` fields to `struct gateway_config`

**Files:**
- Modify: `include/config.h`

- [ ] **Step 1: Read current struct**

Open `include/config.h` and locate `struct gateway_config`. It currently ends with `char crypto_key[65]`.

- [ ] **Step 2: Add forward fields**

Append to the struct (before the closing `};`):

```c
    /* [forward] section — IP forwarding + static routes */
    bool   ip_forward;
    char   forward_routes[8][48]; /* CIDR strings, e.g. "10.20.0.0/24" */
    int    forward_route_count;
```

- [ ] **Step 3: Verify build still passes**

```
make clean && make
```
Expected: zero warnings, binary produced.

- [ ] **Step 4: Commit**

```bash
git add include/config.h
git commit -m "feat: add [forward] fields to gateway_config struct"
```

---

### Task 2: Parse `[forward]` section in `src/config.c`

**Files:**
- Modify: `src/config.c`

- [ ] **Step 1: Write failing test first (see Task 3 for fixture)**

Skip ahead to Task 3 to create the test fixture config, then come back here.

- [ ] **Step 2: Add forward section parsing**

In `config_load()`, inside the `key = value` dispatch block, add a new branch after the `strategy` block (around line 131):

```c
            } else if (strcmp(section, "forward") == 0) {
                if (!strcmp(key, "ip_forward"))
                    cfg->ip_forward = (strcmp(val, "true") == 0);
                else if (!strcmp(key, "route") &&
                         cfg->forward_route_count < 8) {
                    size_t rlen = strlen(val);
                    if (rlen >= 48) rlen = 47;
                    memcpy(cfg->forward_routes[cfg->forward_route_count],
                           val, rlen);
                    cfg->forward_routes[cfg->forward_route_count][rlen] = '\0';
                    cfg->forward_route_count++;
                }
```

Note: The `else if (cur_path != NULL)` branch (path key dispatch) must remain the last branch — keep it after all named sections.

- [ ] **Step 3: Verify parse defaults are zero**

`memset(cfg, 0, sizeof(*cfg))` at line 34 already zeros `ip_forward` (false) and `forward_route_count` (0). No explicit defaults needed.

- [ ] **Step 4: Build**

```
make clean && make
```

- [ ] **Step 5: Commit**

```bash
git add src/config.c
git commit -m "feat: parse [forward] section in config_load()"
```

---

### Task 3: Test fixture config + unit test for forward section

**Files:**
- Create: `config/4node-forward-test.conf`
- Modify: `src/test_config.c`

- [ ] **Step 1: Create fixture config**

```ini
[general]
mode = both
tun_name = tun0
tun_addr = 10.0.0.1/30
listen_port = 7001

[coding]
k = 2
redundancy_ratio = 2.0
block_timeout_ms = 10

[strategy]
type = fixed

[forward]
ip_forward = true
route = 10.20.0.0/24
route = 192.168.99.0/24

[path.p1]
interface = eth0
remote_ip = 172.20.0.3
remote_port = 7000
enabled = true
```

Save as `config/4node-forward-test.conf`.

- [ ] **Step 2: Add test function in `src/test_config.c`**

Add after `test_missing_file()`:

```c
static void test_forward_section(void)
{
    struct gateway_config cfg;
    int ret = config_load("config/4node-forward-test.conf", &cfg);
    assert(ret == 0);
    assert(cfg.ip_forward == true);
    assert(cfg.forward_route_count == 2);
    assert(strcmp(cfg.forward_routes[0], "10.20.0.0/24") == 0);
    assert(strcmp(cfg.forward_routes[1], "192.168.99.0/24") == 0);
}
```

Add `test_forward_section();` call in `main()` before the printf.

- [ ] **Step 3: Run test to verify it fails (before parser change)**

```
make test 2>&1 | grep -A3 test_config
```
Expected: assertion failure or compilation error until Task 2 is done.

- [ ] **Step 4: Run test to verify it passes (after Task 2)**

```
make test 2>&1 | grep -A3 test_config
```
Expected: `config: all tests passed`

- [ ] **Step 5: Commit**

```bash
git add config/4node-forward-test.conf src/test_config.c
git commit -m "test: add [forward] section parsing test and fixture config"
```

---

## Chunk 2: `tun_apply_forward()` + main.c integration

### Task 4: Add `tun_apply_forward()` declaration to `include/tun.h`

**Files:**
- Modify: `include/tun.h`

- [ ] **Step 1: Add declaration**

After the existing `tun_write` declaration, add:

```c
/*
 * Apply IP forwarding and static routes declared in the config.
 * Writes /proc/sys/net/ipv4/ip_forward and runs `ip route add` for each
 * route in cfg->forward_routes[].  No-op if cfg->ip_forward is false.
 * Returns 0 on success, -1 if any step fails (non-fatal: logs warning).
 */
int tun_apply_forward(const char *tun_name, const struct gateway_config *cfg);
```

Add `#include "config.h"` at the top of `tun.h` (after the existing includes).

- [ ] **Step 2: Build**

```
make clean && make
```

- [ ] **Step 3: Commit**

```bash
git add include/tun.h
git commit -m "feat: declare tun_apply_forward() in tun.h"
```

---

### Task 5: Implement `tun_apply_forward()` in `src/tun.c`

**Files:**
- Modify: `src/tun.c`

- [ ] **Step 1: Understand existing tun_configure() pattern**

`tun_configure()` uses ioctl for address setup. `tun_apply_forward()` uses proc filesystem and shell commands — simpler, no ioctl needed.

- [ ] **Step 2: Add implementation after `tun_write()`**

```c
int tun_apply_forward(const char *tun_name, const struct gateway_config *cfg)
{
#ifdef __linux__
    FILE *f;
    char  cmd[128];
    int   i, rc = 0;

    if (!cfg->ip_forward)
        return 0;

    /* Enable kernel IP forwarding */
    f = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    if (!f) {
        LOG_WARN("cannot open /proc/sys/net/ipv4/ip_forward");
        rc = -1;
    } else {
        fputs("1\n", f);
        fclose(f);
        LOG_INFO("ip_forward enabled");
    }

    /* Allow forwarded traffic through TUN */
    snprintf(cmd, sizeof(cmd),
             "iptables -C FORWARD -i %s -j ACCEPT 2>/dev/null || "
             "iptables -I FORWARD -i %s -j ACCEPT", tun_name, tun_name);
    if (system(cmd) != 0)
        LOG_WARN("iptables FORWARD -i %s: non-fatal", tun_name);

    snprintf(cmd, sizeof(cmd),
             "iptables -C FORWARD -o %s -j ACCEPT 2>/dev/null || "
             "iptables -I FORWARD -o %s -j ACCEPT", tun_name, tun_name);
    if (system(cmd) != 0)
        LOG_WARN("iptables FORWARD -o %s: non-fatal", tun_name);

    /* Add declared routes via TUN interface */
    for (i = 0; i < cfg->forward_route_count; i++) {
        snprintf(cmd, sizeof(cmd), "ip route replace %s dev %s",
                 cfg->forward_routes[i], tun_name);
        if (system(cmd) != 0) {
            LOG_WARN("ip route replace %s dev %s failed",
                     cfg->forward_routes[i], tun_name);
            rc = -1;
        } else {
            LOG_INFO("route: %s dev %s", cfg->forward_routes[i], tun_name);
        }
    }
    return rc;
#else
    (void)tun_name;
    (void)cfg;
    return 0;
#endif
}
```

Note: `ip route replace` is used instead of `add` to be idempotent on SIGHUP restarts.

- [ ] **Step 3: Build**

```
make clean && make
```

Expected: zero warnings.

- [ ] **Step 4: Commit**

```bash
git add src/tun.c
git commit -m "feat: implement tun_apply_forward() — ip_forward + route setup"
```

---

### Task 6: Call `tun_apply_forward()` from `main.c`

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Add call after `tun_configure()`**

Find this block (around line 93):

```c
    if (tun_configure(cfg.tun_name, cfg.tun_addr) != 0) {
        LOG_ERR("tun_configure failed");
        close(tun_fd);
        return 1;
    }
```

Add immediately after:

```c
    if (tun_apply_forward(cfg.tun_name, &cfg) != 0)
        LOG_WARN("tun_apply_forward: some routes may be missing");
```

(Non-fatal: gateway continues even if a route command fails — warn only.)

- [ ] **Step 2: Build**

```
make clean && make
```

- [ ] **Step 3: Run all tests**

```
make test
```

Expected: all tests passed.

- [ ] **Step 4: Commit**

```bash
git add src/main.c
git commit -m "feat: call tun_apply_forward() after tun setup in main.c"
```

---

## Chunk 3: 4-container Docker topology

### Task 7: `docker/Dockerfile.client` — lightweight client image

**Files:**
- Create: `docker/Dockerfile.client`

- [ ] **Step 1: Create Dockerfile**

```dockerfile
FROM alpine:3.19
RUN apk add --no-cache iproute2 iputils iperf3
CMD ["sh"]
```

No build step — clients are pure network endpoints.

- [ ] **Step 2: Commit**

```bash
git add docker/Dockerfile.client
git commit -m "feat: add Dockerfile.client (Alpine + iperf3, no gateway build)"
```

---

### Task 8: AP configs with `[forward]` section

**Files:**
- Create: `config/4node-ap-tx.conf`
- Create: `config/4node-ap-rx.conf`

- [ ] **Step 1: Create `config/4node-ap-tx.conf`**

```ini
[general]
mode = both
tun_name = tun0
tun_addr = 10.0.0.1/30
listen_port = 7001

[coding]
k = 2
redundancy_ratio = 2.0
block_timeout_ms = 10
max_payload = 1400
window_size = 8

[strategy]
type = fixed
probe_interval_ms = 100
probe_loss_threshold = 0.3

[forward]
ip_forward = true
route = 10.20.0.0/24

[path.path1]
interface = eth1
remote_ip = 172.20.0.3
remote_port = 7000
weight = 1.0
enabled = true

[path.path2]
interface = eth2
remote_ip = 172.21.0.3
remote_port = 7000
weight = 1.0
enabled = true
```

Note: `eth0` is the LAN (client-facing), `eth1`/`eth2` are the mmWave paths. The listen_port is 7001 here; ap-rx uses 7000 (the default paths bind to).

- [ ] **Step 2: Create `config/4node-ap-rx.conf`**

```ini
[general]
mode = both
tun_name = tun0
tun_addr = 10.0.0.2/30
listen_port = 7000

[coding]
k = 2
redundancy_ratio = 2.0
block_timeout_ms = 10
max_payload = 1400
window_size = 8

[strategy]
type = fixed
probe_interval_ms = 100
probe_loss_threshold = 0.3

[forward]
ip_forward = true
route = 10.10.0.0/24

[path.path1]
interface = eth0
remote_ip = 172.20.0.2
remote_port = 7001
weight = 1.0
enabled = true

[path.path2]
interface = eth1
remote_ip = 172.21.0.2
remote_port = 7001
weight = 1.0
enabled = true
```

Note: ap-rx's TUN is 10.0.0.2/30 (peer of ap-tx's 10.0.0.1/30). Forward route is the client-tx subnet 10.10.0.0/24.

- [ ] **Step 3: Commit**

```bash
git add config/4node-ap-tx.conf config/4node-ap-rx.conf
git commit -m "feat: add 4-node AP configs with [forward] section"
```

---

### Task 9: `docker-compose.4node.multipath.yml`

**Files:**
- Create: `docker-compose.4node.multipath.yml`

**Network topology:**
```
client-tx (10.10.0.2)
    │ lan_tx (10.10.0.0/24)
ap-tx (10.10.0.3, 172.20.0.2, 172.21.0.2)  TUN: 10.0.0.1/30
    │ mmwave1 + mmwave2
ap-rx (172.20.0.3, 172.21.0.3, 10.20.0.3)  TUN: 10.0.0.2/30
    │ lan_rx (10.20.0.0/24)
client-rx (10.20.0.2)
```

- [ ] **Step 1: Create compose file**

```yaml
services:
  client-tx:
    build:
      context: .
      dockerfile: docker/Dockerfile.client
    cap_add:
      - NET_ADMIN
    networks:
      lan_tx:
        ipv4_address: 10.10.0.2
    command: >
      sh -c "ip route add 10.20.0.0/24 via 10.10.0.3 &&
             sleep infinity"

  ap-tx:
    build:
      context: .
      dockerfile: docker/Dockerfile.test
    cap_add:
      - NET_ADMIN
    devices:
      - /dev/net/tun
    networks:
      lan_tx:
        ipv4_address: 10.10.0.3
      mmwave1:
        ipv4_address: 172.20.0.2
      mmwave2:
        ipv4_address: 172.21.0.2
    command: ["/app/coding-gateway", "--config", "/app/config/4node-ap-tx.conf"]

  ap-rx:
    build:
      context: .
      dockerfile: docker/Dockerfile.test
    cap_add:
      - NET_ADMIN
    devices:
      - /dev/net/tun
    networks:
      mmwave1:
        ipv4_address: 172.20.0.3
      mmwave2:
        ipv4_address: 172.21.0.3
      lan_rx:
        ipv4_address: 10.20.0.3
    command: ["/app/coding-gateway", "--config", "/app/config/4node-ap-rx.conf"]

  client-rx:
    build:
      context: .
      dockerfile: docker/Dockerfile.client
    cap_add:
      - NET_ADMIN
    networks:
      lan_rx:
        ipv4_address: 10.20.0.2
    command: >
      sh -c "ip route add 10.10.0.0/24 via 10.20.0.3 &&
             iperf3 -s -D &&
             sleep infinity"

networks:
  lan_tx:
    driver: bridge
    ipam:
      config:
        - subnet: 10.10.0.0/24
  mmwave1:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.0.0/24
  mmwave2:
    driver: bridge
    ipam:
      config:
        - subnet: 172.21.0.0/24
  lan_rx:
    driver: bridge
    ipam:
      config:
        - subnet: 10.20.0.0/24
```

- [ ] **Step 2: Smoke-test the topology**

```bash
docker compose -f docker-compose.4node.multipath.yml up -d --build
sleep 8
# Verify end-to-end ping: client-tx → client-rx
docker compose -f docker-compose.4node.multipath.yml exec -T client-tx \
    ping -c 5 -W 2 10.20.0.2
docker compose -f docker-compose.4node.multipath.yml down
```

Expected: 5/5 packets received.

- [ ] **Step 3: Commit**

```bash
git add docker-compose.4node.multipath.yml
git commit -m "feat: add 4-node multipath compose file for iperf3 experiments"
```

---

## Chunk 4: E17 iperf3 experiment script + README

### Task 10: `scripts/eval/e17_iperf_4node.sh`

**Files:**
- Create: `scripts/eval/e17_iperf_4node.sh`

**What it measures:** iperf3 UDP throughput and loss from client-tx to client-rx through the coded tunnel, across a sweep of path loss levels (0, 10, 20, 30, 40%) and two modes (ratio=1.0 vs ratio=2.0).

- [ ] **Step 1: Create script**

```sh
#!/bin/sh
# E17: iperf3 4-node throughput under path loss — FEC vs no-FEC
# Usage: e17_iperf_4node.sh [RESULTS_DIR] [REPS]
#
# Topology: client-tx → ap-tx →[mmWave]→ ap-rx → client-rx
# Measures iperf3 UDP throughput (Mbit/s) and loss (%) end-to-end.
# Two modes: ratio=1.0 (no FEC) and ratio=2.0 (FEC 2x).
# Symmetric loss applied on ap-tx eth1+eth2 (the mmWave paths).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-10}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e17_iperf_4node.csv"
SUMMARY="$RESULTS_DIR/e17_iperf_4node_summary.csv"

REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"
COMPOSE="$REPO/docker-compose.4node.multipath.yml"

CURRENT_COMPOSE=""
cleanup() {
    [ -n "$CURRENT_COMPOSE" ] && docker compose -f "$CURRENT_COMPOSE" down 2>/dev/null || true
}
trap cleanup EXIT
CURRENT_COMPOSE="$COMPOSE"

echo "mode,loss_pct,rep,throughput_mbps,loss_pct_iperf" > "$CSV"

for ratio in 1.0 2.0; do
    if [ "$ratio" = "1.0" ]; then MODE="no_fec"; else MODE="fec_2x"; fi

    echo "=== E17: ${MODE} (ratio=${ratio}) ==="

    docker compose -f "$COMPOSE" down 2>/dev/null || true
    docker compose -f "$COMPOSE" up -d --build 2>/dev/null
    sleep 8

    # Hot-reload redundancy_ratio on both AP nodes
    docker compose -f "$COMPOSE" exec -T ap-tx \
        sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" \
        /app/config/4node-ap-tx.conf
    docker compose -f "$COMPOSE" exec -T ap-rx \
        sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" \
        /app/config/4node-ap-rx.conf
    docker compose -f "$COMPOSE" exec -T ap-tx \
        sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
    docker compose -f "$COMPOSE" exec -T ap-rx \
        sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
    sleep 2

    # Verify baseline
    if ! docker compose -f "$COMPOSE" exec -T client-tx \
            ping -c 3 -W 2 10.20.0.2 >/dev/null 2>&1; then
        echo "  [FAIL] ${MODE}: baseline ping failed"
        docker compose -f "$COMPOSE" down 2>/dev/null || true
        continue
    fi

    for loss in 0 10 20 30 40; do
        # Apply symmetric loss on ap-tx mmWave interfaces
        for iface in eth1 eth2; do
            docker compose -f "$COMPOSE" exec -T ap-tx \
                tc qdisc del dev "$iface" root 2>/dev/null || true
            docker compose -f "$COMPOSE" exec -T ap-tx \
                tc qdisc add dev "$iface" root netem loss "${loss}%"
        done
        sleep 1

        rep=1
        while [ "$rep" -le "$REPS" ]; do
            # iperf3 UDP 10 Mbit/s for 5 seconds, JSON output
            RESULT=$(docker compose -f "$COMPOSE" exec -T client-tx \
                iperf3 -c 10.20.0.2 -u -b 10M -t 5 -J 2>/dev/null || echo "")

            # Extract throughput (bits_per_second → Mbit/s) and loss_percent
            THROUGHPUT=$(echo "$RESULT" | awk '
                /"bits_per_second"/{bps=$NF+0; gsub(",","",bps)}
                END{printf "%.2f", bps/1e6}')
            LOSS_I=$(echo "$RESULT" | awk '
                /"lost_percent"/{lp=$NF+0; gsub(",","",lp)}
                END{printf "%.1f", lp}')
            [ -z "$THROUGHPUT" ] && THROUGHPUT=0
            [ -z "$LOSS_I" ]     && LOSS_I=100

            echo "${MODE},${loss},${rep},${THROUGHPUT},${LOSS_I}" >> "$CSV"
            rep=$((rep + 1))
        done
        echo "  [${MODE}] loss=${loss}%: ${REPS} reps done"
    done

    docker compose -f "$COMPOSE" down 2>/dev/null || true
done

echo "mode,loss_pct,mean_mbps,std_mbps,mean_loss,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1","$2
    sum[key]+=$4; sumsq[key]+=$4*$4
    lsum[key]+=$5; n[key]++
} END {
    for (k in sum) {
        m=sum[k]/n[k]; v=(sumsq[k]/n[k])-m*m
        if(v<0)v=0; s=sqrt(v)
        ml=lsum[k]/n[k]
        printf "%s,%.2f,%.2f,%.1f,%d\n", k, m, s, ml, n[k]
    }
}' "$CSV" | sort -t, -k1 -k2 -n >> "$SUMMARY"

echo "[E17] Done. Summary:"
cat "$SUMMARY"
```

- [ ] **Step 2: Make executable**

```bash
chmod +x scripts/eval/e17_iperf_4node.sh
```

- [ ] **Step 3: Commit**

```bash
git add scripts/eval/e17_iperf_4node.sh
git commit -m "feat: add E17 iperf3 4-node throughput experiment script"
```

---

### Task 11: Update README.md

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add `[forward]` section docs**

In the **Configuration** section (or create one if absent), add a subsection:

```markdown
### IP Forwarding (`[forward]`)

When acting as an access-point relay, coding-gateway can enable kernel IP
forwarding and install routes automatically on startup:

```ini
[forward]
ip_forward = true
route = 10.20.0.0/24   # one or more CIDR routes, each on its own line
```

On startup, the gateway writes `1` to `/proc/sys/net/ipv4/ip_forward`, inserts
`iptables FORWARD ACCEPT` rules for the TUN interface, and runs
`ip route replace <CIDR> dev <tun_name>` for each declared route.
```

- [ ] **Step 2: Document 4-node topology**

In the **Evaluation** or **Topologies** section, add:

```markdown
### 4-Node AP Topology (E17)

```
client-tx (10.10.0.2)
    │  lan_tx (10.10.0.0/24)
ap-tx (10.10.0.3)  ─── TUN 10.0.0.1/30 ──→ (RLNC encoded)
    │  mmwave1 + mmwave2
ap-rx (10.20.0.3)  ←── TUN 10.0.0.2/30 ─── (decoded)
    │  lan_rx (10.20.0.0/24)
client-rx (10.20.0.2)
```

`docker-compose.4node.multipath.yml` launches all four containers.  AP nodes
self-configure routing via `[forward]` in their config.  Client nodes only need
a single `ip route add` pointing to their local AP.

Run E17: `scripts/eval/e17_iperf_4node.sh`
```

- [ ] **Step 3: Add E17 to the experiments table**

In the experiments summary table, add:

| ID  | Description | Script |
|-----|-------------|--------|
| E17 | iperf3 4-node: end-to-end throughput, FEC vs no-FEC, 0–40% loss | `e17_iperf_4node.sh` |

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: document [forward] config and 4-node topology, add E17"
```

---

## Verification checklist

Before declaring done:

- [ ] `make clean && make` — zero warnings
- [ ] `make test` — all tests pass including `test_forward_section`
- [ ] `docker compose -f docker-compose.4node.multipath.yml up -d --build && sleep 8 && docker compose -f docker-compose.4node.multipath.yml exec -T client-tx ping -c 5 10.20.0.2` — 5/5 pings
- [ ] `scripts/eval/e17_iperf_4node.sh` runs and produces `results/e17_iperf_4node_summary.csv`
- [ ] README reflects all changes

---

## Implementation notes

**Why `ip route replace` instead of `ip route add`:** The gateway can receive SIGHUP and reload. `replace` is idempotent — it won't fail if the route already exists.

**Why `system()` for routes:** POSIX C has no standard interface for route table manipulation. Alternatives (rtnetlink NLMSG_ROUTE) require kernel headers and are significantly more complex. `system()` with `ip route` is simpler, readable, and sufficient for this use case.

**Interface naming in `docker-compose.4node.multipath.yml`:** Docker assigns interfaces in the order networks are listed in the compose file. `ap-tx` is on `lan_tx` (eth0), `mmwave1` (eth1), `mmwave2` (eth2). The AP config uses `eth1`/`eth2` for paths accordingly.

**iperf3 JSON parsing with awk:** The iperf3 `-J` flag outputs structured JSON. The awk parser looks for `"bits_per_second"` and `"lost_percent"` keys — these appear in the `sum` section of the receiver report. This is fragile if iperf3 changes its JSON schema but works for Alpine's iperf3 3.x.
