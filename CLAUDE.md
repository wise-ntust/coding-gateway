# CLAUDE.md — coding-gateway

## Language

Always respond in Traditional Chinese (繁體中文) unless the user writes in English.

## Project Overview

`coding-gateway` is a C99 userspace application implementing RLNC-based erasure coding over multiple UDP paths for mmWave resilience. Zero external dependencies; targets x86, ZedBoard (ARM), and OpenWrt (MIPS/ARM).

## Rules

### Code Style
- C99, POSIX only — no libc extensions, no platform-specific headers beyond POSIX
- Indent with 4 spaces, no tabs
- Keep functions short and focused; one clear responsibility per function
- Prefer explicit over clever — readability matters on embedded targets

### Commit Discipline
- **After completing any roadmap item, feature, or bugfix — always ask the user whether to commit and push before doing so**
- Never auto-commit; always confirm first
- Commit messages: short imperative subject line (≤72 chars), blank line, then body if needed

### Build
- All build logic lives in the top-level `Makefile`
- Three targets: `native` (default), `zedboard`, `openwrt`
- `make` must succeed with zero warnings on all three targets before any commit

### Testing
- Loopback test (`config/loopback-*.conf`) must pass before marking a codec change complete
- Simulate shard loss with `drop_simulation_rate` to verify redundancy behaviour

### No Over-Engineering
- Do not add abstractions until there are at least two concrete use cases
- Do not add error handling for states the code cannot reach
- Configuration lives in TOML files, not compiled-in constants

## Roadmap Reference

Current status (track against `README.md`):

- [x] GF(2⁸) arithmetic core
- [x] Systematic encode / decode
- [x] TUN/TAP interface
- [x] UDP multi-path transport
- [x] Fixed and weighted strategies
- [x] Adaptive strategy with loss feedback
- [x] Probe-based RTT and loss measurement (basic EWMA; full per-path demux is a future refinement)
- [x] Runtime config reload (SIGHUP)
- [ ] Prometheus metrics exporter
- [ ] Grafana dashboard
- [ ] OpenWrt package feed

Update the checkboxes here and in `README.md` together when items are completed.
