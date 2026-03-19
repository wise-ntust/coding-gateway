# ZedBoard / OpenWifi Validation Template

## Run Metadata

- Date:
- Operator:
- Commit SHA:
- Board revision:
- OpenWifi / image version:
- Cross-compiler version:

## Software Inputs

- Binary build command:
- TX config path:
- RX config path:
- FPGA / image details:
- Any local patches:

## Platform Capability Check

- `uname -a`:
- `ls -l /dev/net/tun`:
- `ip link`:
- `ip route`:
- `sysctl net.ipv4.ip_forward`:
- `zcat /proc/config.gz | grep CONFIG_TUN`:
- UDP userspace socket test:
- Notes on missing tools or kernel options:

## Topology

- Radio / path mapping:
- Tunnel addressing:
- Forwarded LAN routes:
- Peer device:

## Smoke Validation

- Binary launch result:
- TUN setup result:
- Route setup result:
- End-to-end packet test:
- Metrics endpoint result:

## Comparative Validation

- Baseline case:
- Loss-injection case:
- Blockage / failover case:

## Captured Artifacts

- `smoke.log`:
- `metrics.txt`:
- `topology.txt`:
- Additional notes:

## Limitations / Deviations

- Missing features:
- Environment caveats:
- Unexpected behavior:

## Verdict

- Pass / Partial / Fail:
- Reason:
