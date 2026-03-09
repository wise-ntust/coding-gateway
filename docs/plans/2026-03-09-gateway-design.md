# coding-gateway — System Design

Date: 2026-03-09

## Context

coding-gateway is a C99 POSIX userspace application implementing RLNC-based erasure
coding over multiple UDP paths for mmWave resilience. Zero external dependencies;
targets x86, ZedBoard (ARM Cortex-A9), and OpenWrt (MIPS/ARM).

This document records design decisions made before implementation begins.

---

## Confirmed Design Decisions

| Question | Decision |
|----------|----------|
| Block formation | k packets OR T ms timeout (whichever comes first) |
| In-flight blocks | Fixed sliding window of size W |
| Loss feedback | TX sends probe packets; RX echoes them back; TX measures RTT and loss |
| Config format | INI / key=value (not TOML — avoids zero-dependency tension) |
| Concurrency model | Single-threaded, select()-based event loop |
| Path failure detection | Probe-based only; sendto() errors are logged but do not directly alter strategy |

---

## Module Structure

```
coding-gateway/
├── src/
│   ├── main.c          # argv parsing, config load, event loop
│   ├── gf256.c         # GF(2⁸) arithmetic (mul/inv lookup tables)
│   ├── codec.c         # encode_block() / decode_block()
│   ├── tun.c           # tun_open() / tun_read() / tun_write()
│   ├── transport.c     # per-path UDP sockets, send/recv shard
│   ├── config.c        # INI parser → struct gateway_config
│   ├── strategy.c      # fixed / weighted / adaptive path selection
│   └── metrics.c       # Prometheus HTTP exporter (future roadmap)
├── include/
│   ├── gf256.h
│   ├── codec.h
│   ├── tun.h
│   ├── transport.h
│   ├── config.h
│   ├── strategy.h
│   └── common.h        # shared types, constants, log macros
├── config/
│   ├── loopback-tx.conf
│   └── loopback-rx.conf
├── docs/plans/
└── Makefile
```

**Module dependency order (no cycles):**
```
main → config, tun, transport, strategy, codec
codec → gf256
strategy → transport (reads path stats)
```

---

## Wire Protocol (UDP Datagram Format)

Each UDP datagram carries one shard:

```
Offset  Size  Field
------  ----  -----
0       2B    magic = 0xC0DE
2       1B    version = 0x01
3       1B    type: 0x01=data, 0x02=probe, 0x03=probe_echo
4       4B    block_id (big-endian, monotonically increasing)
8       1B    shard_idx (0 to n-1)
9       1B    k (original packets in this block)
10      1B    n (total shards in this block)
11      1B    reserved (pad to 4-byte alignment)
12      2B    payload_len
14      kB    coefficients[k] (GF(2⁸) coefficients)
14+k    *     payload (payload_len bytes)
```

Total fixed header: 14 bytes + k bytes.

**Probe packet (type=0x02):** header as above; payload is a single `uint64_t`
timestamp in microseconds since epoch.

**Probe echo (type=0x03):** header as above; payload is the probe payload copied
verbatim. TX measures RTT as `now - timestamp`.

**Notes:**
- `block_id` is 32-bit. At 10,000 blocks/sec it wraps after ~4.9 days.
  Wrap-around is handled using signed sequence number comparison.
- `k` in the header lets the receiver determine the length of `coefficients[]`
  without prior negotiation.

---

## Config Format (INI)

```ini
[general]
mode = tx               # tx | rx | both
tun_name = tun0
tun_addr = 10.0.0.1/30  # gateway configures TUN IP via ioctl

[coding]
k = 4
redundancy_ratio = 1.5
block_timeout_ms = 5
max_payload = 1400
window_size = 8

[strategy]
type = adaptive         # fixed | weighted | adaptive
probe_interval_ms = 100
probe_loss_threshold = 0.3

[path.mmwave_direct]
interface = eth1
remote_ip = 192.168.1.2
remote_port = 7000
weight = 1.0
enabled = true

[path.mmwave_via_a]
interface = eth2
remote_ip = 192.168.1.2
remote_port = 7001
weight = 1.0
enabled = true
```

**Parser rules:**
- Lines beginning with `#` are comments.
- Section headers: `[name]` for flat sections, `[path.NAME]` for path entries.
- Up to `MAX_PATHS` (8) path sections supported.
- No multi-line values, no quoting beyond stripping leading/trailing whitespace.

**Config structs:**

```c
#define MAX_PATHS 8

struct path_config {
    char     name[32];
    char     interface[16];
    char     remote_ip[40];
    uint16_t remote_port;
    float    weight;
    bool     enabled;
};

struct gateway_config {
    char   mode[8];
    char   tun_name[16];
    char   tun_addr[20];         // CIDR notation
    int    k;
    float  redundancy_ratio;
    int    block_timeout_ms;
    int    max_payload;
    int    window_size;
    char   strategy_type[16];
    int    probe_interval_ms;
    float  probe_loss_threshold;
    struct path_config paths[MAX_PATHS];
    int    path_count;
};
```

---

## Main Event Loop

```
select(fds=[tun_fd, udp_fd[0..P-1]], timeout=block_timeout_ms)

tun_fd readable (TX mode):
    read packet → append to current block
    if pkt_count == k → encode_block() → send all shards via strategy

select timeout fires (TX mode):
    if pkt_count > 0 → pad to k, encode_block() → send shards

udp_fd[i] readable:
    recv datagram → parse header
    type=data       → rx_window_insert() → try decode_block() → write TUN
    type=probe_echo → update path[i].rtt_ms and loss_rate via EWMA

probe_timer (every probe_interval_ms, TX mode):
    for each enabled path → send probe packet with current timestamp
```

### TX Block State

```c
struct tx_block {
    uint32_t block_id;
    uint8_t  pkts[MAX_K][MAX_PAYLOAD];
    uint16_t pkt_len[MAX_K];
    int      pkt_count;
    struct timeval first_pkt_time;
};
```

### RX Sliding Window

```c
struct rx_block {
    uint32_t block_id;
    uint8_t  shards[MAX_N][MAX_PAYLOAD + MAX_K];
    uint16_t shard_len[MAX_N];
    bool     received[MAX_N];
    int      recv_count;
    bool     decoded;
};

struct rx_window {
    struct rx_block slots[MAX_WINDOW];
    uint32_t base_id;   // oldest undecoded block_id
};
```

Shard arrival:
1. If `block_id < base_id` → discard (too old).
2. If `block_id >= base_id + W` → advance `base_id` to make room (window overflow;
   skipped blocks will never decode — upper-layer TCP retransmits).
3. Store shard; if `recv_count >= k` → Gaussian elimination → write to TUN →
   mark decoded.

---

## Adaptive Strategy

### Per-Path Runtime State

```c
struct path_state {
    struct path_config cfg;
    float    loss_rate;       // EWMA probe loss, 0.0–1.0
    float    rtt_ms;          // EWMA round-trip time
    float    weight_current;  // effective weight (adaptive adjusts this)
    bool     alive;
    uint64_t probes_sent;
    uint64_t probes_recv;
    uint64_t shards_sent;
};
```

### Probe Loss EWMA

```
On probe_echo received for path i:
    rtt = now_us - timestamp_in_payload
    path[i].rtt_ms   = 0.2 * rtt_ms + 0.8 * path[i].rtt_ms
    loss_rate        = 1.0 - (probes_recv / probes_sent)  (over sliding count)
    path[i].loss_rate = 0.2 * loss_rate + 0.8 * path[i].loss_rate

    if path[i].loss_rate > probe_loss_threshold:
        path[i].alive = false
    if path[i].loss_rate < probe_loss_threshold * 0.5:   // hysteresis
        path[i].alive = true
```

Hysteresis prevents oscillation when a path is near the threshold — important for
mmWave environments where brief blockage/recovery cycles are common.

### Dynamic Redundancy Adjustment

Before encoding each block:

```
n_alive = count of paths where alive == true
if n_alive == 0:
    effective_ratio = max_redundancy
elif n_alive == total_enabled_paths:
    effective_ratio = config.redundancy_ratio
else:
    effective_ratio = min(config.redundancy_ratio * total / n_alive, max_redundancy)

n = ceil(k * effective_ratio)
```

### Strategy Modes

- **fixed:** round-robin across enabled paths.
- **weighted:** probabilistic selection proportional to `weight`.
- **adaptive:** weighted selection with `weight_current` driven by probe feedback;
  paths where `alive == false` receive weight 0.

---

## Future Extensions (Not In Scope Now)

- **Multi-threaded (TX thread + RX thread):** useful when targeting ZedBoard
  dual-core; requires mutex around sliding window.
- **Process isolation (fork TX/RX):** for fault isolation; requires IPC for shared
  config and TUN fd.
- **Runtime config reload (SIGHUP):** reload strategy params and path weights
  without restart.
- **Prometheus metrics exporter:** HTTP endpoint on `metrics_port`.
- **OpenWrt package feed:** `.ipk` packaging for `opkg install`.
