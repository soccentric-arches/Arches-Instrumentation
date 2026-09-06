# Arches Instrumentation — Gap Report

Audit of the implementation on `develop` against [REQ.md](REQ.md).

**Method:** full read of REQ.md; full file inventory; read of every daemon
domain header, the platform layer, the transport layer, the command tables,
system files and the Yocto recipe; a clean `cmake --build` and a full `ctest`
run in [build/](build).

**Verified state:** the build succeeds and produces all 10 daemons + 10 `_ctrl`
tools. `ctest` runs 214 tests: **207 pass, 7 fail**.

**What is genuinely done well** (not repeated below): hub-and-spoke topology
(R1/R2), real gRPC-over-UDS with per-daemon `.proto` files, the single
command-table pattern feeding both the gRPC surface and `_ctrl` (R3, §3.2),
the 32-bit alarm/fault ID codec and build-time catalog codegen (§8), config
origin tracking and merge (§4/§9), the disk spool with priority drop policy
(§7.1/§11), time reconciliation (§4), the alarm state machine with X.733
fields and escalation (§7.6), OTA state machine with anti-rollback and
maintenance windows (§7.9), backup manifest/exclusions/transactional restore
(§7.10), audit log, drift report, provisioning state machine and key rotation
(§6.2), and no `#ifdef`-per-target anywhere in daemon code (R9).

Everything below is a gap. Priority ordering: **P0** ships broken, **P1**
required by REQ and absent, **P2** partial or divergent, **P3** hygiene.

---

## P0 — Failing tests and broken behaviour

- [ ] **P0-1 — Seven tests fail.** `ctest` is red. Fix all seven before any
  other work; several are the failure paths REQ §11 says must be tested.
- [ ] **P0-2 — `_ctrl` streaming delivers 1 frame of 5.**
  `CtrlLoopback.StreamHandler` ([test/unit/test_filesystem.cpp:112](test/unit/test_filesystem.cpp:112))
  gets `got.size() == 1`, expected 5. Streaming ops (`log_ctrl tail`,
  `ota_ctrl` progress) are unusable over the ctrl socket. Bug is in
  [lib/ctrl/src/ctrl.cpp](lib/ctrl/src/ctrl.cpp) framing/read loop.
- [ ] **P0-3 — Clean-shutdown marker never reads back as clean.**
  `IstateCore::last_shutdown_clean()` returns `false` after a clean shutdown.
  Fails `IntegrationBoot.FullTopologyBootsAndSpokesStart`
  ([test/integration/harness.cpp:101](test/integration/harness.cpp:101)),
  `IntegrationUncleanShutdown.MarkerMissingReportedNextBoot`
  ([harness.cpp:242](test/integration/harness.cpp:242)) and
  `FaultShutdown.UncleanMarkerReportedOnNextBoot`
  ([test/fault-injection/fault_tests.cpp:201](test/fault-injection/fault_tests.cpp:201)).
  Every boot is reported to HQ as unclean (§11).
- [ ] **P0-4 — `/pst` corruption of both redundant copies is not detected.**
  `FaultPst.BothCopiesCorruptBootsUnprovisionedAndReports`
  ([fault_tests.cpp:64](test/fault-injection/fault_tests.cpp:64)) gets `ok()==true`
  where `Errc::Corrupt` is required. The device would boot on corrupt config
  and silently believe it valid — the exact opposite of the §11 contract.
- [ ] **P0-5 — Spool loses entries when HQ applies backpressure.**
  `FaultFlood.JitteredDrainNotStampede`
  ([fault_tests.cpp:217](test/fault-injection/fault_tests.cpp:217)) expects
  `spool.size()==100` after a paused drain, gets 90. Decide the contract
  (entries acked by the sink are legitimately consumed) and fix either
  `NorthboundSpool::drain` or the test — do not leave it red.
- [ ] **P0-6 — Log fields are not redacted; the secret-leak test fails.**
  `SecretLeak.AuditTrailRedactsDetailSecrets`
  ([test/secret-leak/secret_leak_test.cpp:98](test/secret-leak/secret_leak_test.cpp:98))
  finds `LEAKME` in a log field. §10 requires secret hygiene *enforced with a
  test*. Add key-aware redaction in [lib/common/src/log.cpp](lib/common/src/log.cpp)
  (`wg-private.key`, `*secret*`, `*password*`, `*token*`, `psk`) applied to
  both message and structured fields.

---

## P1 — Required by REQ, not implemented

### Configuration distribution to spokes (§R4, §7, §9) — the largest gap
- [ ] **P1-1 — No hub→spoke config path exists.** `MonitorHubFrame` and every
  other `*HubFrame` carry only `Command / Backpressure / RelayedData /
  TimeStamp` ([proto/monitord.proto](proto/monitord.proto)). There is no
  `ConfigPush`/`GetConfig` RPC for spokes. Add one to `common.proto` and every
  spoke frame.
- [ ] **P1-2 — No daemon reads its own `/pst/system/<daemon>/config.yaml`.**
  REQ §7 requires it of all ten. Only `hardware` loads any file at startup.
  Wire each spoke to fetch its config from istate on registration and on push.
- [ ] **P1-3 — Daemon configs are never schema-validated.** §9 requires
  validation at load with a file-and-line rejection. The validator exists
  ([lib/common/src/config.cpp](lib/common/src/config.cpp)) but no daemon calls it
  on its own config. [daemons/monitord/src/main.cpp:38](daemons/monitord/src/main.cpp:38)
  loads the resource profile with an **empty schema** and discards the result
  (`(void)profile;`).
- [ ] **P1-4 — Only 8 JSON Schemas exist, and none of them describe a daemon
  config.** [schemas/daemon-config.schema.json](schemas/daemon-config.schema.json)
  is a 12-line generic object with `additionalProperties: false` and 4 keys —
  **every** file in [system/defaults/](system/defaults) would be rejected by it
  (`paths`, `sources`, `maintenance_windows`, `ntp_servers`, …). Write one real
  schema per daemon (10) and validate the defaults against them in CI.

### Periodic work — 8 of 10 daemons have no timer at all
Only `monitord` ([daemons/monitord/src/spoke.cpp:43](daemons/monitord/src/spoke.cpp:43))
and `istate` run periodic work. The following are reachable only by an operator
typing a `_ctrl` command:
- [ ] **P1-5 — Faults never follows the kernel log.**
  [daemons/faults/src/main.cpp:55](daemons/faults/src/main.cpp:55) does the boot
  backfill once, then `run_until_stop(stop, {})` with no callback. §7.5's
  "real-time monitoring of the kernel ring buffer and journal" is absent.
- [ ] **P1-6 — Alarms never runs `escalation_sweep`.** Time-based escalation
  (§7.6) never fires on its own.
- [ ] **P1-7 — Hardware never runs periodic checks.** `periodic-interval` is
  settable but nothing schedules `check-all` (§7.4).
- [ ] **P1-8 — OTA never calls `tick()`.** The automatic rollback of an
  unconfirmed update (§7.9, §11) never fires — the deadline is set and then
  never evaluated.
- [ ] **P1-9 — Backup never runs on schedule.** `schedule_enabled` /
  `schedule_interval_s` are in the config and the command table; no scheduler
  thread reads them (§7.10).
- [ ] **P1-10 — Telemetry never flushes on its interval.** §7.8's "at a
  configured interval sends it to HQ" requires a timer; only manual `flush` exists.
- [ ] **P1-11 — LogManager never prunes/rotates on a timer,** and never
  enforces its storage budget in the background (§7.7).
- [ ] **P1-12 — networkd never sweeps expired SSH windows and never probes
  connectivity.** `RemoteAccessManager::sweep()` and the connectivity ranker
  are never called on a timer, so a time-boxed SSH window does not actually
  expire (§7.2) and failover never re-ranks.

### Liveness (§3.1, §7.1, §11)
- [ ] **P1-13 — `LivenessPing`/`LivenessPong` are defined in
  [proto/common.proto](proto/common.proto) and never sent, received or handled
  anywhere.** Hang detection relies purely on last-seen data, so an idle-but-
  healthy spoke (alarms, backup, ota) is indistinguishable from a hung one.
  Implement the ping/pong round trip istate's liveness sweep is written against.

### networkd (§7.2, R8)
- [ ] **P1-14 — No wireless support at all.** No SSID/credential management,
  no scanning, no client mode, no AP mode, no wpa_supplicant/hostapd. The
  daemon nevertheless advertises `"wireless"` in its capability list
  ([daemons/networkd/src/daemon.cpp:196](daemons/networkd/src/daemon.cpp:196)) —
  istate therefore reports dishonestly, which §5.1 explicitly forbids.
- [ ] **P1-15 — No DNS configuration** (resolv.conf / systemd-resolved) —
  required by §7.2.
- [ ] **P1-16 — No routing configuration.** `NetworkPlan` has no routes field.
- [ ] **P1-17 — Nothing is ever applied to the system.**
  `NativePlatform::apply_network_plan` is an unconditional no-op —
  [lib/platform/src/native.cpp:532-540](lib/platform/src/native.cpp:532) discards
  `plan_json` even when `INSTR_APPLY_NETWORK=1`. No `ip`, `nft`/`iptables`,
  `wg`, `sshd`, `systemd-networkd` writer exists. networkd computes correct
  plans and throws them away — this makes the tunnel-loss guard, firewall,
  port forwarding, VLAN/bond and WireGuard bring-up non-functional on a device.

### OTA (§7.9)
- [ ] **P1-18 — SWUpdate is never invoked.** `OtaEngine::apply()`
  ([daemons/ota/src/domain.cpp:151](daemons/ota/src/domain.cpp:151)) only calls
  `set_boot_slot(..., trial=true)`. The staged artifact is never written to the
  inactive slot. The recipe RDEPENDS on `swupdate` but no code path calls it.
- [ ] **P1-19 — `set_boot_slot`'s `trial` flag is ignored** and no bootloader
  environment is touched ([native.cpp:339](lib/platform/src/native.cpp:339) writes
  `/mnt/boot/pending-slot`; no `fw_setenv`/efibootmgr/`nvbootctrl`). Golden boot
  is hardcoded to `"A"`. Automatic rollback (§11) cannot work.
- [ ] **P1-20 — Config migration on first boot after an update is not driven.**
  `PstStore::migrate()` exists ([daemons/istate/src/pst.cpp:143](daemons/istate/src/pst.cpp:143))
  but nothing calls it from the post-update boot path (§7.9, §6).

### Platform layer (§5)
- [ ] **P1-21 — Missing interface capabilities:** partition topology (only flat
  block devices), SMART for NVMe/SATA (only eMMC `life_time`), time-source
  availability enumeration, hardware identity sources beyond the factory file,
  and CAN send/receive primitives (`can_loopback_test`
  ([native.cpp:420](lib/platform/src/native.cpp:420)) only reads `operstate` —
  it is not a loopback test).
- [ ] **P1-22 — No way to run a daemon on the simulated platform.** All ten
  `main.cpp` files hardcode `NativePlatform`. §5 requires the simulated
  implementation be "complete enough to run the entire suite on a build host
  with no hardware" — today only unit tests can reach it. Add a
  `--platform=simulated` / `INSTR_PLATFORM` selector.
- [ ] **P1-23 — `journal_lines()` returns empty unless `INSTR_APPLY_NETWORK=1`**
  ([native.cpp:512](lib/platform/src/native.cpp:512)). A read-only operation is
  gated behind a mutation flag, so LogManager collects nothing on a real device.

### Security (§10, §3.1)
- [ ] **P1-24 — Socket ownership does not enforce the call graph.**
  [daemons/istate/src/hubs.cpp:21](daemons/istate/src/hubs.cpp:21) calls
  `::chown(path, -1, -1)` — a no-op. Sockets are 0660 `root:root`, not
  `instr:<daemon>`, so any member of the `instr` group can call any spoke's hub
  service. Also, [system/systemd/instr-istate.service](system/systemd/instr-istate.service)
  sets `CapabilityBoundingSet=` (empty), which denies istate the `CAP_CHOWN` it
  needs to do this correctly.
- [ ] **P1-25 — No measured-boot reporting.** §10 requires reporting measured
  boot state upward where supported; there is no TPM/PCR code anywhere.
- [ ] **P1-26 — Backup is not encrypted to HQ's public key.** §7.10 requires
  local archives keyed from device identity (done —
  [daemons/backup/include/instr/backup/domain.h:87](daemons/backup/include/instr/backup/domain.h:87))
  **and** HQ uploads encrypted to HQ's public key (absent — no HQ pubkey exists
  in the codebase). The X25519 primitive is already available in
  [lib/common/include/instr/common/crypto.h](lib/common/include/instr/common/crypto.h).

### Testing (§12)
- [ ] **P1-27 — The integration harness is not the full topology.**
  [test/integration/harness.cpp](test/integration/harness.cpp) instantiates
  `IstateCore` in-process and injects synthetic `GenericSpokeMsg` structs. It
  never starts the nine spoke daemons, never opens a UDS, and never exercises
  the gRPC registration handshake, streaming, or reconnect. §12 requires "hub,
  all nine spokes, simulated platform layer, running on a build host".
- [ ] **P1-28 — No simulated-device container.** §12 requires "the full daemon
  set in a container, so HQ and Configuration can be developed without
  hardware". There is no Dockerfile or compose file in the repo.
- [ ] **P1-29 — [test/fixtures/](test/fixtures) is empty** although §2 lists it
  and the Jenkins pipeline assumes a populated suite.

---

## P2 — Partial or divergent from REQ

- [ ] **P2-1 — Signature verification is symmetric (HMAC), not public-key.**
  Both signed config pushes ([proto/common.proto](proto/common.proto)
  `SignedConfig.hmac`) and OTA artifact signatures
  ([daemons/ota/include/instr/ota/domain.h](daemons/ota/include/instr/ota/domain.h)
  `ArtifactManifest.hmac`) use HMAC-SHA256 over the enrollment secret. §9's
  intent ("tunnel compromise alone does not let an attacker reconfigure a
  fleet") is better served by Ed25519 with a device-held public key. Confirm
  the decision or switch; either way document it.
- [ ] **P2-2 — Hand-rolled crypto.** SHA-256, HMAC, X25519, ChaCha20 and HKDF
  are all implemented from scratch in
  [lib/common/src/crypto.cpp](lib/common/src/crypto.cpp). Test vectors are
  present, but this is an audit liability on a fleet product; prefer
  libsodium/OpenSSL, or get an external review.
- [ ] **P2-3 — Key rotation has one key slot.**
  [daemons/istate/src/config_service.cpp:167](daemons/istate/src/config_service.cpp:167)
  ignores `key_id` (`// single device secret for now`), so a rotation cannot
  overlap old and new keys — the very case §6.2 rotation-without-a-site-visit needs.
- [ ] **P2-4 — Descriptor filenames do not match what daemons look for.**
  `hardware` reads `/etc/instrumentation/devices.yaml`
  ([daemons/hardware/src/main.cpp:34](daemons/hardware/src/main.cpp:34)) and
  `monitord` reads `.../resource-profile.yaml`, but the recipe installs
  `devices-<board>.yaml` and `capabilities-<board>.yaml` and only the generic
  `resource-profile.yaml`. On a real image the hardware daemon finds no
  descriptor. Add MACHINE-driven selection (symlink or `${MACHINE}` install).
- [ ] **P2-5 — No per-board fault rule sets.** §5.1 lists the fault rule set as
  a per-board descriptor; only the single [catalog/fault-rules.yaml](catalog/fault-rules.yaml)
  exists, and it is installed to `${sysconfdir}/instrumentation/` while
  `catalog/alarms.yaml` goes to `.../catalog/` — inconsistent.
- [ ] **P2-6 — `/pst` defaults are incomplete.** §6 requires `system/networkd/wg-peer.yaml`,
  `system/hardware/devices.yaml`, `system/faults/fault-rules.yaml` and
  `system/alarms/catalog.yaml` to be seeded. [system/defaults/](system/defaults)
  ships only `config.yaml` per daemon.
- [ ] **P2-7 — Storage-budget breach logs a warning, never raises an alarm.**
  [daemons/istate/src/main.cpp:94](daemons/istate/src/main.cpp:94) only calls
  `log.warning`. §6 requires an alarm before the limit. (Note the R5 tension —
  istate acting as the declaring application is the right reading; make it explicit.)
- [ ] **P2-8 — Coverage gate is set to 70%,** in
  [Jenkinsfile](Jenkinsfile) and enforced by
  [tools/ci/coverage_gate.py](tools/ci/coverage_gate.py). §12 says "full
  coverage … a hard requirement". Agree a real number and raise it.
- [ ] **P2-9 — `_ctrl` shell completion is claimed but unverified.** §3.2
  requires `--help`, subcommand help, `--json`, `--socket` and completion
  generation from every tool. No test exercises the completion path.
- [ ] **P2-10 — Bulk log transfer by handle is half-built.** Handles are minted
  ([daemons/logmanager/src/domain.cpp:223](daemons/logmanager/src/domain.cpp:223))
  but there is no out-of-band payload channel; §7.7 requires the payload never
  traverse istate's control path.
- [ ] **P2-11 — Age-based log retention is not implemented.**
  [daemons/logmanager/src/domain.cpp:100](daemons/logmanager/src/domain.cpp:100)
  handles retention by count only; the age branch is a comment. §7.7 requires both.
- [ ] **P2-12 — `display_control` supports only fb0 blank/unblank**
  ([native.cpp:437](lib/platform/src/native.cpp:437)); DSI/eDP backends named in
  the descriptors have no implementation, and `hardware`'s control dispatch
  returns `NotSupported` for unhandled kinds
  ([daemons/hardware/src/domain.cpp:88](daemons/hardware/src/domain.cpp:88)).

---

## P3 — Hygiene and packaging

- [ ] **P3-1 — Yocto recipe uses an undefined variable.**
  `${sysusers.d}` in [yocto/recipes-instrumentation/instrumentation/instrumentation.bb:57](yocto/recipes-instrumentation/instrumentation/instrumentation.bb:57)
  expands to nothing — the sysusers file installs to the wrong path. Use
  `${D}${sysconfdir}/sysusers.d/`.
- [ ] **P3-2 — Recipe `SRC_URI` is a placeholder** (`git://git.arches.example/...`)
  and should point at `git@github.com:soccentric-arches/Arches-Instrumentation.git`.
  `PV = "1.0.0"` also contradicts [VERSION](VERSION).
- [ ] **P3-3 — `DEPENDS` lists `grpc protobuf` twice** in the recipe.
- [ ] **P3-4 — Every non-istate systemd unit has a duplicate `[Service]`
  header** (9 files). systemd merges them, but it is a lint failure and hides
  edits — see [system/systemd/instr-networkd.service](system/systemd/instr-networkd.service).
- [ ] **P3-5 — [system/tmpfiles.d/instrumentation.conf](system/tmpfiles.d/instrumentation.conf)
  declares `/run/instrumentation` three times**, mixing `D` (wipe on boot) with
  two `d` entries. Keep one line.
- [ ] **P3-6 — `test/istate/` sits outside the `daemons/istate/test/` shape**
  every other daemon follows (§2: "Each daemon directory follows the same shape:
  `src/`, `include/`, `test/`").
- [ ] **P3-7 — REQ.md contains the entire prompt twice** (lines 1–445 and
  446–891). Deduplicate so future diffs are readable.
- [ ] **P3-8 — Jenkins references targets that may not exist:**
  `run-clang-tidy` is not defined in any CMakeLists, and the `coverage` target
  is guarded on `gcovr` being installed
  ([test/CMakeLists.txt:79](test/CMakeLists.txt:79)) — the gate silently
  disappears when it is not.
- [ ] **P3-9 — Two stale build trees** (`build/`, `build-host-debug/`) are in
  the working directory; confirm [.gitignore](.gitignore) covers both.

---

## Suggested order of work

1. **P0-1…P0-6** — get `ctest` green; these are shipped-behaviour bugs.
2. **P1-1…P1-4** — the config path. Nothing else in §9 is real until spokes
   can receive and validate their configuration.
3. **P1-5…P1-13** — periodic work and liveness. Cheap to add, and they turn
   eight daemons from operator-driven into autonomous.
4. **P1-17, P1-18, P1-19** — the two places where correct plans are computed
   and then discarded (network apply, SWUpdate invoke). Highest field risk.
5. **P1-24, P1-26, P2-1** — security surface.
6. **P1-27, P1-28** — a real integration harness, which will surface more of
   the above on its own.
7. P2 / P3 as cleanup.
