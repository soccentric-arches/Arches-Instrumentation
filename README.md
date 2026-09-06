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

## Topology

Everything below the `device` line runs on-box; HQ lives off-device behind
the tunnel. istate is the hub and the only process with an external face
(R2); the nine spokes, Configuration and colocated applications are all
gRPC-over-UDS clients of istate and of nothing else (R1).

```
                                    ┌─────────────────────────────────────────────────────────────┐
                                    │                    HQ — cloud fleet plane                   │
                                    │fleet management · configuration · monitoring                │
                                    │verifies enrollment vs manufacturing record                  │
                                    │returns tunnel configuration (WireGuard)                     │
                                    │signs config pushes · uploads OTA artifacts                  │
                                    │ingests telemetry · faults · alarms · log bundles            │
                                    │staged canary rollout · auto-halt on regression              │
                                    │backpressure to devices · config-drift compare               │
                                    └──────────────────────────────┬──────────────────────────────┘
        gRPC · HqPeer.Session — ONE bidirectional stream           │
        over the WireGuard tunnel — networkd brings it up,         │
        istate owns the session · identity = WG keypair            │
        generated on first boot · private key never leaves         │
                                                                   │ ▲ north DeviceFrame:
                                                                   │ │  telemetry · events · faults
                                                                   │ │  log refs · canary · pong
                                                                   │ ▼ south HqFrame:
                                                                   │ │  signed config · OTA cmds ·
                                                                   │ │  relay cmds · tunnel · bpress
 ════════ device ══════════════════════════════════════════════════════════════════════════════════════════
                                                                   │
┌─────────────────────────────┐   ┌────────────────────────────────┴────────────────────────────────┐
│ Configuration — local web UI│   │   istate · the hub — R1 hub-and-spoke · R2 sole external face   │
│Rust · on-box · stateless    │   │gRPC-over-UDS server — one socket per peer:                      │
│NO device state of its own:  │──►│  /run/instrumentation/<daemon>.sock · 0660 · instr:<d>          │
│every read/write is an istate│   │  handshake: name · build ver · schema ver · capabilities        │
│call — never touches systemd,│   │  schema mismatch → refuse + alarm (never degrade)               │
│network stack, update system │   │  streams: samples · fault events · telemetry batches ·          │
│or /pst directly (§3.3)      │   │           OTA progress · log tail → fan-out HQ + Config         │
│pairing + enrollment live in │   │gRPC client → HQ: HqPeer.Session over the WG tunnel              │
│istate; Config renders them  │   │northbound spool: disk-backed · bounded · drains on              │
│ConfigurationApi (gRPC/UDS): │   │  reconnect · jittered backoff · honours HQ backpressure         │
│GetSystemInfo · GetConfig ·  │   │  drop: oldest telemetry first — NEVER alarms/faults             │
│SetConfig (origin-tracked)   │   │config service: per-key origin default/app/local/hq              │
│ListSpokes · SpokeControl    │   │  HQ pushes signed + verified · resets consult origin            │
│Reset · StreamEvents/Logs    │   │  → operator 'local' edits survive · config w/o OTA              │
│GetHealthScore · Budgets     │   │orchestrator: liveness-checks spokes, restarts hung;             │
│GetEnrollmentStatus ·        │   │  spokes survive istate crash, re-attach                         │
│RotateKey · RelayCommand     │   │timeline/event fan-out · budgets · audit trail                   │
└─────────────────────────────┘   │startup: faults/monitord/hw → alarms/logmgr/telemetry            │
                                  │  → networkd → ota/backup → tunnel + HQ session LAST             │
┌─────────────────────────────┐   │  (never a precondition for local operation, R10)                │
│    colocated applications   │   │/pst: SOLE writer (R4) · atomic writes · redundancy              │
│ApplicationApi (gRPC/UDS) on │──►│  corrupt → boot unprovisioned + report                          │
│/run/.../application.sock    │   └───┬─────────────────────────────────────────────────────────────┘
│0660 · group instr-apps      │       │
│RaiseAlarm / ClearAlarm —    │ ┐─────┘
│R5: only apps declare alarms;│ ├────────────────────┬─────────────────────────────────────┬
│CRITICAL → needs_snapshot    │ │                    │                                     │
│Register / ReportMetrics     │ │   ┌────────────────┴───────────────┐    ┌────────────────┴───────────────┐
│(§7.8)                       │ │   │         networkd — R8          │    │            monitord            │
│GetConfig / SetAppConfig —   │ │   │owns ALL network-adjacent:      │    │periodic resource sampling:     │
│lands origin=app, never      │ │   │interfaces, wireless, routing,  │    │cpu, memory, disk, thermal,     │
│overwrites operator 'local'  │ │   │DNS, firewall, SSH, port fwd    │    │power sources                   │
│GetSystemInfo                │ │   │WireGuard tunnel bring-up;      │    │storage wear: eMMC lifetime,    │
│logs → app.log → logmanager  │ │   │tunnel-loss guard: change that  │    │SD health, SMART                │
└─────────────────────────────┘ │   │kills tunnel → auto-revert if   │    │samples stream to istate at a   │
                                │   │HQ unreachable in the timeout   │    │configured interval → batches   │
┌─────────────────────────────┐ │   │time: chrony·ptp4l·phc2sys·gpsd │    │northbound via istate           │
│ _ctrl tools — one per daemon│ │   └────────────────────────────────┘    └────────────────────────────────┘
│istate_ctrl · alarm_ctrl ·   │ │
│ota_ctrl · backup_ctrl ·     │ ├────────────────────┬─────────────────────────────────────┬
│network_ctrl · monitor_ctrl ·│ │                    │                                     │
│hardware_ctrl · fault_ctrl · │ │   ┌────────────────┴───────────────┐    ┌────────────────┴───────────────┐
│log_ctrl · telemetry_ctrl    │ │   │         hardware — R7          │    │          faults — R6           │
│raw-UDS (not gRPC) · 0600,   │ │   │ONLY hardware writer (R7):      │    │kernel log observer, READ-ONLY  │
│root-only, runtime-only —    │ │   │full control of board devices   │    │never configures hardware,      │
│NEVER persists to /pst (§3.2)│ │   │GPIO, LED, USB, CAN, display    │    │never raises alarms (R6)        │
│one shared command table:    │ │   │hardware watchdog · boot slot   │    │fault IDs share the 32-bit      │
│gRPC + ctrl both dispatch →  │ │   │control, rollback, golden boot  │    │alarm scheme → HQ correlates    │
│full coverage · --help ·     │ │   │thermal zones · power sources   │    │kernel fault ↔ app alarm        │
│--json · completion          │ │   │driven by declarative per-      │    │rate-limited: token bucket,     │
└─────────────────────────────┘ │   │board descriptors — no #ifdefs  │    │'suppressed N events' summary   │
                                │   └────────────────────────────────┘    └────────────────────────────────┘
                                │
                                ├────────────────────┬─────────────────────────────────────┬
                                │                    │                                     │
                                │   ┌────────────────┴───────────────┐    ┌────────────────┴───────────────┐
                                │   │          alarms — R5           │    │           logmanager           │
                                │   │alarm state machine · declared  │    │collects journal + app.log      │
                                │   │by apps ONLY, never inferred    │    │(instr-apps group, setgid)      │
                                │   │(R5) · X.733 fields: probable   │    │rotation · retention · level    │
                                │   │cause, specific problem,        │    │control · storage budgets       │
                                │   │correlated notifications —      │    │bundles: tar on demand,         │
                                │   │HQ groups across the fleet      │    │fetched via handle              │
                                │   │escalation = config (HQ or      │    │live tail: filtered, rate-      │
                                │   │local) · auto-clear policy      │    │limited, auto-stop on timeout   │
                                │   └────────────────────────────────┘    └────────────────────────────────┘
                                │
                                ├────────────────────┬─────────────────────────────────────┬
                                │                    │                                     │
                                │   ┌────────────────┴───────────────┐    ┌────────────────┴───────────────┐
                                │   │           telemetry            │    │              ota               │
                                │   │metric registry: gauge/counter  │    │apply-artifact · confirm ·      │
                                │   │app-registered (§7.8)           │    │rollback — unconfirmed update   │
                                │   │health score                    │    │auto-rolls back                 │
                                │   │collects per YAML config at a   │    │artifact streamed down via      │
                                │   │configured interval → batches   │    │istate · lands on disk before   │
                                │   │→ istate → spool → HQ + Config  │    │apply                           │
                                │   │spool-full: drop oldest         │    │canary: fault rate, health,     │
                                │   │telemetry first, report drop    │    │version → HQ staged rollout     │
                                │   └────────────────────────────────┘    └────────────────────────────────┘
                                │
                                ├────────────────────┬─────────────────────────────────────┬
                                                     │                                     │
                                    ┌────────────────┴───────────────┐    ┌────────────────┴───────────────┐
                                    │             backup             │    │    /pst — persistent store     │
                                    │tar snapshot of /pst;           │    │istate is SOLE writer (R4);     │
                                    │restore = untar under istate    │    │all others via istate's API     │
                                    │direction (R4)                  │    │atomic: temp → fsync → rename   │
                                    │excludes factory/, WG keys —    │    │checksum · redundant copy       │
                                    │identity is NOT portable        │    │both copies corrupt → boot      │
                                    │encrypted: local key derived    │    │unprovisioned + report alarm    │
                                    │from device identity; to HQ     │    │config lives here → HQ can      │
                                    │encrypted to HQ's public key    │    │reconfigure without an OTA      │
                                    └────────────────────────────────┘    └────────────────────────────────┘


        nine spokes — gRPC-over-UDS to istate and to nothing else (R1) · no daemon-to-daemon links
        startup: istate → faults, monitord, hardware → alarms, logmanager, telemetry →
        networkd → ota, backup → tunnel + HQ session LAST — never a precondition (R10)
```

istate is the server on every spoke socket
(`/run/instrumentation/<daemon>.sock`, 0660, owned `instr:<daemon>`). A
spoke dials in on start and registers with its name, build version, proto
schema version and capability list; on a schema mismatch istate refuses the
registration and raises an alarm — it never silently degrades a mismatched
peer. Streaming where the data is a stream (monitord samples, fault events,
telemetry batches, OTA progress, log tail), unary otherwise.

| Spoke | Owns |
|---|---|
| `networkd` | interfaces, wireless, routing, DNS, firewall, SSH, the WireGuard tunnel, time daemons (R8) |
| `monitord` | resource sampling, storage wear |
| `hardware` | the only hardware writer — GPIO, LED, USB, CAN, display (R7) |
| `faults` | kernel log observation, read-only (R6) |
| `alarms` | alarm state, escalation, X.733 fields; declared by applications, never inferred (R5) |
| `logmanager` | journal and `app.log` collection, bundles, live tail |
| `telemetry` | metrics, health score, northbound batches |
| `ota` | update apply / confirm / rollback, canary |
| `backup` | encrypted snapshot/restore of /pst, device identity excluded |

### istate ↔ Configuration

Configuration — the local Rust web UI running on the same box — is a
gRPC-over-UDS client of istate, architecturally identical to a spoke (§3.3):

- It holds no device state. Every read and write is an istate call; it never
  touches systemd, the network stack, the update system, or /pst directly.
- Pairing and enrollment live in istate; Configuration renders the flow.
- Its surface is `ConfigurationApi` (`proto/istate.proto`): config read/write
  with origin tracking, spoke status and control, reset, event and log
  streaming, health score, enrollment status, key rotation, relayed commands
  and storage budgets.

### istate ↔ HQ

gRPC over the WireGuard tunnel: networkd brings the tunnel up, istate owns
the gRPC session over it (§3.4) — one bidirectional stream, `HqPeer.Session`:

- **Identity.** The WireGuard keypair is generated on the device at first
  boot; the public key is signed with the factory identity and enrolled with
  HQ. The private key never leaves the device.
- **Northbound** (`DeviceFrame`): telemetry batches, timeline events, fault
  events, log bundle references, canary state (fault rate, health score,
  version) and liveness pongs.
- **Southbound** (`HqFrame`): signed config pushes, OTA commands
  (apply-artifact / confirm / rollback), generic relayed commands, tunnel
  configuration and backpressure signals.
- **Spool.** Telemetry, faults and log references are spooled to disk first
  and drained when the tunnel comes up — nothing northbound depends on the
  tunnel being available at the moment it is produced. Spool full drops
  oldest telemetry first, never alarms or faults.
- **Flood control.** Jittered exponential backoff on reconnect and on drain,
  honouring HQ backpressure, so a site of hundreds of devices powering up
  together does not stampede HQ.
- **Config from HQ.** Pushes are signed and verified on device, then land in
  /pst with origin `hq`; origin decides what a push or reset may overwrite,
  so an operator's `local` edits survive (R4, §6). No OTA is needed to
  reconfigure any daemon.
- **Degrade, don't block (R10).** Tunnel or HQ loss leaves the device fully
  operational; a firewall change that kills the tunnel auto-reverts if HQ is
  unreachable within the configured timeout.

**Startup order:** istate → Faults, monitord, Hardware → Alarms, LogManager,
Telemetry → networkd → OTA, Backup → tunnel and HQ session. The HQ session
is last and is never a precondition for local operation (R10); observation
daemons start early so boot-window anomalies are captured.

## Build

There are two build targets and they have different jobs. Confusing them is
what made this section contradict itself for a while, so the split is stated
first:

| | Host-native (x86_64) | Container (aarch64) |
|---|---|---|
| Used for | **the gate** — every edit, every loop | release artifacts and CI |
| Speed | seconds | slow: qemu-emulated on an x86_64 host |
| Needs | a C++17 toolchain, CMake, protoc/grpc | Docker + binfmt |
| Proves | the code is correct | the code is correct *for the device* |

**Host-native is the default developer workflow and what `.claude/validate.sh`
runs.** It is fast enough to run on every change, which is the only property
that matters for a gate.

```bash
cmake --preset host-debug
cmake --build --preset host-debug -j
ctest --preset host-debug --output-on-failure
```

Presets: `host-debug`, `host-release`, `ninja-host-debug`,
`ninja-host-release`, `asan`, `ubsan`, `tsan`, `coverage`,
`jenkins` (CI), plus cross toolchains per target under `cmake/toolchains/`
(`cross-jetson-orin-nano`, `cross-zynq-ultrascale`, …).

**The container build is aarch64** — the Jetson fleet — in `docker/`
(Ubuntu 22.04 arm64, the JetPack 6 userland generation, so the binaries match
the device glibc; on an x86_64 host it runs under qemu via binfmt, installed
automatically). Use it to produce what ships, and before a release:

```bash
make               # aarch64 build for the fleet -> build-aarch64/
make test          # the test suite, inside the arm64 container
make check-format  # clang-format gate, inside the arm64 container
make tidy          # clang-tidy, inside the arm64 container (slow under qemu)
make shell         # interactive shell in the arm64 container
```

Both must pass before a release. Only the host-native one has to pass before
every commit.

Formatting is enforced in CI (`.clang-format`, Microsoft-based, effectively
unwrapped — see the rationale in that file):

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
./build-host-debug/test/integration_harness   # full topology on a build host
```

The integration harness runs the hub, all nine spokes and the simulated
platform — the complete topology on a laptop, no hardware. Coverage gate,
sanitizers and the secret-leak test are enforced in CI (Jenkinsfile).

`integration_application` covers the colocated-application path end to end:
a real gRPC client on a real socket against a live hub with the alarms and
telemetry spokes attached.