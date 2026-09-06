# Arches Instrumentation — Gap Report (closed)

Audit of the implementation against [REQ.md](REQ.md), and the record of
closing it. Every item below was found by the audit and has been fixed;
this file is kept as the account of what was wrong and why it mattered.

**Verified state:** clean from-scratch build, no compiler warnings,
**278 tests pass** (was 207 of 214, with 7 failures and two tests passing
vacuously). The full topology — hub plus spokes over real gRPC on real Unix
sockets — runs on a build host with no hardware.

---

## P0 — Failing tests and broken behaviour

- [x] **P0-1 — Seven tests failed.** All green, and stable across repeated runs.
- [x] **P0-2 — `_ctrl` streaming delivered 1 frame of 5.** `CtrlClient` built a
  fresh `FrameDecoder` per `recv_frame`, discarding every frame after the
  first in a read. The decoder is now per-connection, and the server drains
  all pipelined requests rather than only the first.
- [x] **P0-3 — Clean-shutdown marker could never survive a reboot.** It lived in
  `/run` — tmpfs — so every boot reported unclean. Moved to `/pst/state`.
- [x] **P0-4 — `/pst` corruption of both copies was undetected.** The tests
  injected at a literal `/pst/...` while the fixture rooted elsewhere, so the
  redundant-copy path was never exercised: one test failed, its sibling
  passed vacuously. Both now inject against the fixture's real root; the
  production code was correct.
- [x] **P0-5 — Spool backpressure.** The implementation was right (only acked
  entries are popped); the test asserted the wrong number. Also removed dead
  counters overwritten from spool stats immediately after being set.
- [x] **P0-6 — Log redaction missed secrets in compound keys.** `redact_text`
  could not see a secret name inside a longer key such as `wg-private.key`,
  so the §10 secret-hygiene test failed. It now runs to the end of the key
  token before looking for the separator.
- [x] **Also: a flaky backoff test.** It summed jittered delays against a hard
  floor inside a ±20% envelope. Now asserts the envelope and pins an exact
  schedule through an injected RNG.

## P1 — Required by REQ, was not implemented

### Configuration distribution (§R4, §7, §9)
- [x] **P1-1 — No hub→spoke config path existed.** Added `ConfigDelivery`,
  `ConfigRequest` and `ConfigAck` to `common.proto` and every spoke frame.
- [x] **P1-2 — No daemon read its own config.** Spokes request configuration on
  registration and are pushed a new one on change; none opens `/pst` itself.
- [x] **P1-3 — Configs were never schema-validated.** `ConfigSink` validates
  every delivery, rolls back to the previous document on rejection, and
  reports the reason to istate.
- [x] **P1-4 — Ten per-daemon JSON Schemas written.** The old generic
  `daemon-config.schema.json` would have rejected every shipped default; it
  is gone. A test validates every default against its schema. The JSON
  validator was extended to honour schema-valued `additionalProperties`,
  which it previously passed unchecked.

### Work that was never scheduled
- [x] **P1-5 — Faults now follows the kernel log** instead of reading it once at
  startup. Follow semantics were broken in both platforms (native re-opened
  and seeked to end, reading nothing; simulated re-returned the same tail).
- [x] **P1-6…P1-12 — Timers.** hardware, backup, telemetry and networkd had no
  timer at all; alarms, ota and logmanager had hardcoded intervals in their
  main loops. All are now config-driven `PeriodicWorker`s.
  *(Correction to the original audit: alarms, ota and logmanager did have
  loops — I recorded them as absent. Four daemons lacked timers, not eight.)*
- [x] **P1-13 — Liveness ping/pong was defined and never sent.** istate now
  pings each spoke and marks hung only on missing pongs, so an idle-but-
  healthy daemon is no longer indistinguishable from a wedged one.

### networkd (§7.2, R8)
- [x] **P1-14 — Wireless implemented** (client and AP mode, rendered
  supplicant/hostapd config, PSK referenced not carried).
- [x] **P1-15/P1-16 — DNS and routes** added to the plan and applied.
- [x] **P1-17 — Plans were computed and discarded.**
  `NativePlatform::apply_network_plan` was an unconditional no-op that threw
  `plan_json` away. It now applies interfaces, routes, DNS, firewall, NAT,
  wireless, WireGuard and sshd, gated behind `INSTR_APPLY_NETWORK`.

### OTA (§7.9)
- [x] **P1-18 — SWUpdate is now invoked.** `apply()` only flipped a boot slot;
  staging runs the installer through an injectable seam.
- [x] **P1-19 — Boot slots go through the bootloader environment** (U-Boot
  `fw_setenv`, flag-file fallback), and the trial flag is honoured, so
  automatic rollback of an unconfirmed update can work.
- [x] **P1-20 — Config migration is driven from the boot path.** `migrate()`
  existed and nothing called it.

### Platform (§5)
- [x] **P1-21 — Added** partition topology, SMART/NVMe health, time-source
  enumeration, CAN send/receive and measured-boot reporting.
- [x] **P1-22 — Daemons can select the simulated platform** (`--platform`,
  `INSTR_PLATFORM`), so the full set runs on a build host.
- [x] **P1-23 — `journal_lines()` no longer gated behind a mutation flag.**

### Security (§10, §3.1)
- [x] **P1-24 — Socket ownership now enforces the call graph.** `chown(-1,-1)`
  was a no-op and ran before the sockets existed. Sockets are handed to
  `instr-<daemon>:instr` after the server starts, failure is loud, and
  istate's unit grants the `CAP_CHOWN` its empty bounding set had denied.
- [x] **P1-25 — Measured boot** (TPM PCRs, SecureBoot) reported northbound.
- [x] **P1-26 — HQ backups sealed to HQ's public key** with a per-archive
  ephemeral X25519 key. Without an HQ key an upload is refused, never sent
  in the clear.

### Testing (§12)
- [x] **P1-27 — Real integration harness.** `test/integration/topology.cpp`
  runs hub and spokes over real gRPC on real Unix sockets against the
  simulated platform: registration, schema refusal, config delivery and
  rejection, streaming, liveness, reconnect.
- [x] **P1-28 — Simulated device container** in `test/sim-device/`.
- [x] **P1-29 — Fixtures populated** with real artefacts.

## P2 — Partial or divergent

- [x] **P2-1/P2-2 — Signature verification is now asymmetric.** Config pushes
  and OTA artifacts verify Ed25519 through libcrypto; the device holds only
  public keys. The legacy shared-secret form is accepted only where no key is
  provisioned, and its use is reported. Hand-rolled crypto no longer covers
  the signature path.
- [x] **P2-3 — Key rotation holds several keys at once,** so rotation needs no
  flag day.
- [x] **P2-4 — Descriptors install under the names daemons open,** selected per
  `MACHINE`. Previously the recipe shipped `devices-<board>.yaml` while the
  daemon opened `devices.yaml` and found nothing.
- [x] **P2-5 — Per-board fault rule sets supported** by the recipe.
- [x] **P2-6 — `/pst` defaults complete** (`catalog.yaml`, `fault-rules.yaml`,
  `devices.yaml`, `wg-peer.yaml`).
- [x] **P2-7 — Budget breach raises an alarm,** edge-triggered, not just a log
  line nobody reads on a fielded device.
- [x] **P2-8 — Coverage gate raised to 80 and made to fail closed;** a missing
  `gcovr` is now a configure error rather than a silently skipped stage.
- [x] **P2-9 — `_ctrl` surface tested:** help, per-op help, completion and
  argument declarations across all ten tools.
- [x] **P2-10 — Bulk transfer is genuinely out of band.** `bundle-fetch`
  base64'd whole archives into the control response; it now returns a handle
  and metadata, with the payload streamed over a dedicated socket.
- [x] **P2-11 — Age-based log retention implemented** (the filesystem
  abstraction gained mtime; rename now preserves it, as a real one does).
- [x] **P2-12 — Display backends come from the descriptor** (fb, backlight,
  DSI, eDP) plus brightness control.

## P3 — Hygiene

- [x] **P3-1 — `${sysusers.d}`** (an undefined variable) corrected.
- [x] **P3-2 — Recipe points at the real remote;** version read from `VERSION`.
- [x] **P3-3 — Duplicate `DEPENDS` entries removed;** OpenSSL and the runtime
  tools networkd/ota actually invoke are now declared.
- [x] **P3-4 — Duplicate `[Service]` sections removed** from nine units.
- [x] **P3-5 — `/run/instrumentation` declared once,** not three times.
- [x] **P3-6 — istate tests moved** into `daemons/istate/test/`.
- [x] **P3-7 — REQ.md deduplicated** (it contained the prompt twice).
- [x] **P3-8 — `run-clang-tidy` target added** with a `.clang-tidy` config; CI
  no longer builds a target that does not exist behind `|| true`.
- [x] **P3-9 — Build trees confirmed ignored.**

---

## Defects found while fixing, beyond the original audit

These were not in the first report; the work surfaced them.

- **`set_relay_fn` was never called**, so `IstateCore` held a null relay:
  config delivery, liveness pings and monitord→telemetry relays all silently
  did nothing on a real device.
- **Registration metadata never reached the orchestrator.** `register_spoke`
  was never called from the hub, so build version, proto version and
  capabilities were never recorded — and §11's "proto mismatch raises an
  alarm" never fired, because that alarm lived inside the uncalled function.
- **`PstStore::migrate` dropped config origins**, relabelling every operator
  edit as `default` after an update — which would then let the next HQ push
  overwrite it, defeating §9's origin protection.
- **`SpokeChannel::stop()` deadlocked on a quiet channel**, so a daemon would
  ignore SIGTERM until systemd escalated to SIGKILL. monitord additionally
  slept a full sampling interval before noticing a stop.
- **The proto-mismatch alarm id was hand-written with transposed nibbles**
  (`0x1_01_0_3001` for `0x1_01_3_0001`), so HQ would have received an id that
  decodes to nothing in the catalog.
- **OTA skipped signature verification entirely when no secret was set** —
  fail-open on exactly the unenrolled device most worth attacking.
- **The capability manifest was hardcoded**, claiming GNSS, CAN and Wi-Fi on
  every board — the dishonesty §5.1 exists to prevent. It now comes from the
  board's descriptor.
- **The shipped fault rules missed four of six classes they claim to cover**
  (thermal, ECC, device timeout, watchdog lockup) against a real kernel log,
  because they matched case-sensitively and expected wordings drivers do not
  use. Found by testing the rules against a genuine boot log fixture.
- **`SIGPIPE` on bulk and ctrl writes** meant a client disconnecting
  mid-transfer could kill the daemon.
