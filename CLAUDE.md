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
- **完成一個段落（roadmap item、feature、bugfix）後自動 commit 並 push，不需要詢問**
- 目的是即時記錄每次改動，保持 remote 同步
- Commit messages: short imperative subject line (≤72 chars), blank line, then body if needed

### Build
- All build logic lives in the top-level `Makefile`
- Three targets: `native` (default), `zedboard`, `openwrt`
- `make` must succeed with zero warnings on all three targets before any commit

### Testing
- Loopback test (`config/loopback-*.conf`) must pass before marking a codec change complete
- Simulate shard loss with `drop_simulation_rate` to verify redundancy behaviour

### Documentation Sync
- **每次完成動作（feature、bugfix、實驗、移除功能）後，檢查 README.md 是否反映最新狀態**
- 實驗結果、測試清單、config 範例、路線圖都必須與程式碼同步
- 如果有新的實驗數據（尤其是 30-rep 統計結果），更新 README.md 的 Evaluation 區段

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
- [x] Prometheus metrics exporter
- [x] Grafana dashboard
- [x] OpenWrt package feed

Update the checkboxes here and in `README.md` together when items are completed.
