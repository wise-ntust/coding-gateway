# Hardware Validation Checklist

Use this checklist before claiming OpenWrt or ZedBoard/OpenWifi validation.

## Minimum Matrix

- One native x86 localhost baseline run
- One OpenWrt build + deploy + smoke run
- One ZedBoard/OpenWifi build + deploy + smoke run
- One metrics capture per hardware platform
- One archived log bundle per hardware platform

## Required Evidence Per Run

- Date and operator
- Git commit SHA
- Hardware platform and firmware/image version
- Toolchain / SDK version
- Exact config files used
- Topology description
- Traffic generator and command lines
- Smoke-test output
- Metrics snapshot
- Limitations or deviations

## Recommended Artifact Naming

- `hardware/<date>-openwrt-<sha>/`
- `hardware/<date>-zedboard-<sha>/`

Within each directory, keep:

- `notes.md`
- `tx.conf`
- `rx.conf`
- `smoke.log`
- `metrics.txt`
- `topology.txt`

## Minimum Smoke Checks

- Binary starts successfully
- TUN interface comes up
- Routes are installed as expected
- At least one packet flow succeeds through the tunnel
- Metrics endpoint responds

## Minimum Comparative Checks

- One no-loss baseline
- One induced-loss comparison
- One blockage or failover observation

## Reporting Rules

- If a run is incomplete, record it as incomplete; do not promote it into README as a validated result.
- If toolchain or firmware constraints block a step, note the exact blocker.
- If the topology differs from Docker evaluation, describe the difference explicitly.
