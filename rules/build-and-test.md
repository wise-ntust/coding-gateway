# Build And Test

## Build

- All build logic lives in the top-level `Makefile`.
- Support three targets: `native` (default), `zedboard`, and `openwrt`.
- For code-affecting changes, `make` must succeed with zero warnings on all three targets before any commit.
- Treat a change as code-affecting if it modifies build logic, packaging, runtime behavior, tests, or executable assets. This includes changes under `src/`, `include/`, `config/`, `scripts/`, `openwrt/`, `docker/`, top-level `Makefile`, or other files that can change produced artifacts or runtime behavior.
- Non-code changes may skip the three-target build gate. This includes repository instructions, Markdown documentation, dashboards, and similar content-only updates that do not affect produced artifacts or runtime behavior.

## Testing

- Loopback tests (`config/loopback-*.conf`) must pass before marking a codec change complete.
- Use `drop_simulation_rate` to simulate shard loss and verify redundancy behavior.
- If a change is non-code and does not affect codec behavior, the codec-specific test requirements above do not apply.
- CI is split into three gate types:
  - `Docs`: documentation/rules/result-summary changes
  - `Native`: native build plus unit tests for code-affecting changes
  - `Docker Integration`: end-to-end Docker regression tests for runtime and integration changes
- Keep the workflow scope narrow with path filters so docs-only changes do not trigger runtime-heavy verification unnecessarily.
