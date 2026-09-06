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
  docker/       arm64 (Jetson/aarch64) build container + its Makefile
  docs/         generated interface reference (docgen.py, cannot drift)
```

## Build

Requirements for the default workflow: nothing but Docker. The default
build is aarch64 — the Jetson fleet — inside the arm64 container in
`docker/` (Ubuntu 22.04 arm64, the JetPack 6 userland generation, so the
binaries match the device glibc; on an x86_64 host the container runs
under qemu via binfmt, installed automatically). There is no native x86
build target.

```bash
make               # aarch64 build for the fleet -> build-aarch64/
make test          # the test suite, inside the arm64 container
make check-format  # clang-format gate, inside the arm64 container
make tidy          # clang-tidy, inside the arm64 container (slow under qemu)
make shell         # interactive shell in the arm64 container
```

Manual CMake builds still work where a native toolchain exists:

```bash
cmake --preset host-debug
cmake --build --preset host-debug -j
ctest --preset host-debug --output-on-failure
```

Presets: `host-debug`, `host-release`, `ninja-host-debug`,
`ninja-host-release`, `asan`, `ubsan`, `tsan`, `coverage`,
`jenkins` (CI), plus cross toolchains per target under `cmake/toolchains/`
(`cross-jetson-orin-nano`, `cross-zynq-ultrascale`, …).

Formatting is enforced in CI (`.clang-format`, Google-based, 100 columns):

```bash
make check-format                                        # verify (CI gate)
make format                                              # rewrite in place
cmake --build --preset host-debug --target check-format  # manual equivalent
```

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

## Colocated applications

The `_ctrl` sockets above are 0600 root-only (§3.2), so they are not the
surface an application uses. Applications get their own face on istate —
architecturally identical to the Configuration peer of §3.3 — at
`/run/instrumentation/application.sock`, mode 0660, owned
`instr-istate:instr-apps`. Add your application's user to the `instr-apps`
group and it can call `ApplicationApi` (see `proto/istate.proto`):

| RPC | Why |
|---|---|
| `RaiseAlarm` / `ClearAlarm` | R5: an alarm is declared by an application and by nothing else. `owner` is required and is recorded as the alarm's owner. A CRITICAL alarm comes back with `needs_snapshot` set (§7.6). |
| `RegisterMetric` / `ReportMetrics` | §7.8 application metric registration and reporting. |
| `GetConfig` / `SetAppConfig` | §6.2: applications override values as they see fit, with origin recorded. Writes always land with origin `app` and never overwrite an operator's `local` edit. |
| `GetSystemInfo` | serial, hardware revision, firmware and schema versions for the app's own records. |

R1/R2/R4 are unaffected: an application talks to istate and to nothing else,
istate relays to the alarms and telemetry spokes, and every `/pst` write is
still istate's alone.

For logs, append to `/var/log/instrumentation/app.log` (group `instr-apps`,
setgid). LogManager collects it alongside the journal, subject to the same
rotation, retention, level control and storage budget as every other source.

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

`integration_application` covers the colocated-application path end to end:
a real gRPC client on a real socket against a live hub with the alarms and
telemetry spokes attached.