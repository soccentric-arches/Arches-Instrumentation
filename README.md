# Arches Instrumentation

Complete C++17 on-device software management and observability suite for a
fleet of embedded Linux devices across nine hardware targets (NVIDIA Jetson,
AMD Xilinx Zynq, NXP i.MX, TI Sitara, Intel/AMD x86, Raspberry Pi and three
further targets), plus its Yocto integration and test suite.

**Ten daemons, one external face.** `istate` is the hub: system state,
provisioning, persistent configuration, orchestration, and the only process
with an external interface (local gRPC-over-UDS for Configuration, gRPC over
a WireGuard tunnel to HQ). The nine spokes — `networkd`, `monitord`,
`hardware`, `faults`, `alarms`, `logmanager`, `telemetry`, `ota`, `backup` —
talk to istate and to nothing else (R1). `Configuration` (the local web UI)
is architecturally identical to a spoke: a gRPC-over-UDS client of istate.

```
instrumentation/
  proto/        one .proto per daemon + common.proto (R3: one wire format)
  lib/
    common/     config+origins, JSON+schema validation, logging, timestamps,
                rate limiter, disk spool, atomic writes, crypto (SHA-256,
                HMAC, X25519, ChaCha20, HKDF), command tables, tar, ctrl
                framing, daemon skeleton
    platform/   abstraction layer + complete simulated implementation +
                native sysfs/procfs implementation + declarative descriptors
    ctrl/       raw-UDS control server/client + shared _ctrl tool main
    transport/  gRPC-over-UDS plumbing shared by all ten daemons
  daemons/      istate + nine spokes (domain logic separate from transport)
  tools/        one _ctrl per daemon with full operation coverage
  schemas/      JSON Schema for every YAML format (validated at load)
  catalog/      master alarm catalog + code generator; default fault rules
  descriptors/  per-board device descriptors, resource profiles, capabilities
  system/       systemd units, sysusers.d, tmpfiles.d, udev, default YAML set
  test/         unit, contract, integration harness, fault injection,
                secret-leak
  yocto/        bitbake recipe (installs; carries no system files of its own)
  docs/         generated interface reference (docgen.py, cannot drift)
```

## Build

Requirements: C++17 toolchain, CMake ≥ 3.16, protobuf + gRPC, yaml-cpp,
GoogleTest. One version for the whole suite from `VERSION`.

```bash
cmake --preset host-debug
cmake --build --preset host-debug -j
ctest --preset host-debug --output-on-failure
```

Presets: `host-debug`, `host-release`, `asan`, `ubsan`, `tsan`, `coverage`,
`jenkins` (CI), plus cross toolchains per target under `cmake/toolchains/`
(`cross-jetson-orin-nano`, `cross-zynq-ultrascale`, …).

## Deploy

The repo ships everything the image needs; the Yocto recipe only builds and
installs:

```bash
# in your Yocto layer's bblayers.conf:
BBLAYERS += "/path/to/instrumentation/yocto"
# in your image recipe:
IMAGE_INSTALL:append = " instrumentation"
```

Units, sysusers, tmpfiles, udev rules and the default YAML set are installed
from the repo (`system/`); the device boots into `instr-istate.service`,
which orchestrates the spokes (observation daemons first, tunnel + HQ
session last, never a precondition for local operation).

## Operate

Every daemon exposes a raw-UDS control socket `/run/instrumentation/<daemon>-ctrl.sock`
(0600, root-only) driven by one command table shared with its gRPC surface —
full coverage, help and shell completion for free:

```bash
istate_ctrl status | spokes | config-get --daemon monitord | timeline | budgets | audit
alarm_ctrl list | raise id=0x1_04_1_0007 owner=app
ota_ctrl status | apply-artifact | confirm | rollback | canary
backup_ctrl create | preview --name <archive> | restore --name <archive>
network_ctrl status | ssh-open operator=alice ttl_s=600 | tunnel-status
monitor_ctrl status | sample-once | wear
hardware_ctrl list-devices | check-all | gpio-set device=gpio-1 value=true
fault_ctrl rules-list | test-line line="Out of memory: Killed process 4321 (myapp)"
log_ctrl tail | bundle-create | bundle-fetch --handle <handle>
telemetry_ctrl health-score | flush | register-metric name=app.metric unit=ms type=gauge app=myapp
```

All `_ctrl` changes are runtime-only and never persist to `/pst`; only
istate writes persistent configuration, every key carrying its origin
(default / app / local / hq).

## Failure behaviour (contract, §11)

Tunnel down → spool northbound, device fully operational, drain on
reconnect. Spool full → drop oldest telemetry first, never alarms/faults,
report the drop. Spoke hang → istate liveness check detects and restarts.
istate crash → spokes keep running, re-attach. /pst corrupt → redundant copy;
if both fail, boot unprovisioned and report. Unconfirmed update → automatic
rollback. Clock unavailable → monotonic operation, re-stamp on lock.
Firewall change kills tunnel → auto-revert within the timeout. Every one of
these is implemented and covered by the fault-injection suite.

## Tests

```bash
ctest --preset host-debug                 # unit + contract + integration + fault injection
./build-host-debug/integration_harness    # full topology on a build host
```

The integration harness runs the hub, all nine spokes and the simulated
platform — the complete topology on a laptop, no hardware. Coverage gate,
sanitizers and the secret-leak test are enforced in CI (Jenkinsfile).