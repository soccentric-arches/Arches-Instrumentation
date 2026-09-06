# Gap report — Arches Instrumentation vs REQ.md

Audit date: 2026-09-05. Baseline: clean build, 278/278 tests passing.
Method: requirement-by-requirement sweep of REQ.md §§1–15 against the tree,
plus a search for code that is implemented but has no production caller —
which is where every finding below came from.

## G1 — LogManager never collects anything (§7.7)

`LogStore::append()` is called from **tests only**. The daemon configures
sources, rotates, prunes, tails and bundles a store that nothing ever
writes to. `SourceSpec::kind` is parsed and reported but never dispatched
on, and `IPlatform::journal_lines()` — implemented in both the native and
simulated platforms — has no caller anywhere in `daemons/`.

§7.7 opens with "Collects from the journal and from application log sinks".
That is the daemon's primary job and it is absent.

**Fix:** a `LogCollector` (same shape as the existing `FaultFollower`) that
polls each configured source — `journal` via `journal_lines()`, `file`/`app`
by tailing `path` from a retained offset — and appends into the store,
honouring the per-source runtime level. Wired into `main.cpp` and
reconfigured on config delivery.

## G2 — Colocated applications have no interface (R5, §7.6, §7.8, §6.2)

REQ is emphatic that applications are the *only* entities that may raise or
clear an alarm (R5, §7.6), that Telemetry offers an "application metric
registration API" (§7.8), and that after provisioning "applications may then
override values as they see fit, with origin recorded" (§6.2).

No such interface exists. Alarm raise/clear and metric registration are
reachable only over the per-daemon `_ctrl` sockets, which §3.2 fixes at mode
**0600, root only** — an unprivileged colocated application cannot open one.
`ConfigurationApi` is the only app-shaped gRPC surface and it has no alarm or
metric RPC. So R5 is, today, unsatisfiable by any application on the box.

**Fix:** an `ApplicationApi` gRPC service on istate at
`/run/instrumentation/application.sock`, mode 0660, owned
`instr-istate:instr-apps` — architecturally identical to the Configuration
peer of §3.3, so R1/R2/R4 are preserved (apps reach spokes only by istate
relaying). RPCs: `GetSystemInfo`, `RaiseAlarm`, `ClearAlarm`,
`RegisterMetric`, `ReportMetric`, `GetConfig`, `SetAppConfig` (origin forced
to `app`). Plus an `instr-apps` group in sysusers.d and an application log
sink in the logmanager defaults.

## G3 — CAN devices cannot be driven (§7.4)

`DeviceManager::control()` branches on `gpio`, `led`, `usb` and `display`.
There is no `can` branch, and no `devices-*.yaml` gives a CAN device a
`controls:` key — so a CAN device can only be loopback-checked, never
driven. `IPlatform::can_send()` / `can_recv()` are implemented on both
platforms and unreachable from any daemon.

§7.4 names CAN in the managed device set and requires Hardware to drive
devices, "not read-only".

**Fix:** a `can` control branch (`can_send`, `can_recv`) and `controls:` on
the CAN devices in the descriptors.

## G4 — Crash forensics never triggers (§7.7)

`ForensicsCapture::capture()` works and is covered by a unit test, but its
only caller is the manual `crash-report` ctrl op. Nothing detects an
abnormal daemon exit, and `IPlatform::coredump_dir()` has no caller — so the
capture cannot find a coredump on its own.

§7.7 requires capture "on abnormal daemon exit". A forensic path that only
fires when an operator asks for it has missed the event it exists for.

**Fix:** istate's orchestrator already tracks spoke state; have an abnormal
transition relay a crash-report to logmanager, which resolves the coredump
from `coredump_dir()`.

## G5 — CtrlServer accumulates a thread handle per connection (defect)

`CtrlServer::serve_accept()` prunes finished connection threads with
`if (!it->joinable())`, which is never true: a `std::thread` stays joinable
after its function returns, right up until it is joined or detached. The
erase branch is dead, so `conn_threads_` grows by one entry — and one
unjoined OS thread — for every ctrl connection ever accepted, for the
lifetime of the daemon. On a device polled periodically by a `_ctrl` tool
this is an unbounded leak in all ten daemons.

**Fix:** track completion explicitly and join finished threads on accept.
