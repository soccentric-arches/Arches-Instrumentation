# Arches-Instrumentation — Requirements (DRAFT)

*Authored 2026-09-05 from fleet context at the owner's request. **Not yet
human-approved.** Do not promote loop-016 (or write code in this repo) until
the owner approves or edits this document. This repo is registered as a
`stub` in `loop/fleet-deps.yaml` until then.*

## Problem

Arches is an IV&V system: Arches-Client executes verification runs on a target
and streams verdicts to Arches-Server. Nothing in the fleet owns the *physical
evidence* behind those verdicts — the serial console transcript, periphery
state, bus activity, and bench logs that corroborate or contradict what the
Client reports. What exists today is scattered and has no home in the fleet
map:

* `loop-005` (bench-log-collector) and `loop-006` (usb-stress) are parked as
  standalone `Apps/` projects with no leaf that owns them.
* `loop-007` (live run monitoring in the Dashboard) has no dedicated data
  source to build on.
* Run forensics — "show me what the bench actually did during run X" — is
  done by hand, per incident.

An operator who receives a failing verdict cannot currently pull the raw,
timestamped bench evidence for that run from any fleet component.

## Proposed role

**Arches-Instrumentation is the evidence-capture layer of the IV&V mission.**
It observes the system under test and the bench, records timestamped evidence
keyed to IV&V runs, and makes that evidence retrievable through Arches-Server.

| Need | Owner |
|---|---|
| Test execution, verdicts, result streaming | Arches-Client (unchanged) |
| Result ingestion, run control, SQLite | Arches-Server (unchanged in v1) |
| Hardware abstraction API, proto contract | Arches-HAL (consumed, unchanged) |
| **Bench + device evidence capture** | **Arches-Instrumentation** |
| One-off bench scripts (current `Apps/` prototypes) | Decided under R6 |

## Requirements

- **R1** — A capture agent records timestamped serial console output from a
  target device for the full duration of an IV&V run, without manual steps.
- **R2** — Every capture is keyed to a run identity that Arches-Server already
  recognizes, so evidence is retrievable per run, not per file.
- **R3** — The agent can capture periphery observations (GPIO state reads and
  similar probes) through the existing HAL gRPC contract during a run, without
  regenerating protos and without new HAL code.
- **R4** — An operator can retrieve the console capture and any periphery
  observations for a given run through Arches-Server (v1 may reuse the
  existing ingestion path; a dedicated endpoint is a later loop).
- **R5** — The leaf is a real fleet member: its stack is chosen and registered
  (`fleet-deps.yaml` + `tools/validate/stack.txt`), its `.claude/validate.sh`
  gate is installed from that stack's template, and it is listed in the root
  `Makefile` `SUFFIXES`.
- **R6** — The relationship between this leaf and the standalone bench tools
  (`Apps/App/python/bench-log-collector`, `Apps/App/cpp-linux/usb-stress-app`)
  is decided and documented: absorbed here, integrated, or deliberately left
  standalone — so the fleet map stays truthful.

## Non-goals (v1)

* Flashing, power control, wedged-target recovery — owned by `loop/tools/`.
* Telemetry, alarms, fault, logs, OTA, backup, system-manager daemons — the
  Apps/ daemon family, a different product line.
* Any change to HAL protos or the Client/Server wire contracts.

## Open decisions (owner)

1. **Stack** — proposal: Python 3.11 + uv for the bench-side agent (matches
   Arches-PySDK and the bench-log-collector prototype). Alternatives: C++ on
   the bench host, or a split agent.
2. **R6 disposition** — absorb the `Apps/` bench tools into this leaf, keep
   them standalone, or leave undecided until the agent proves the pattern.
3. **Evidence path** — reuse Arches-Server's existing ingestion for v1
   (proposal) vs. a dedicated endpoint now.

## Exit from stub state

This repo leaves `status: stub` when: this REQ is human-approved, loop-016
(`loop/workflow/backlog/loop-016-instrumentation-leaf.md`) is promoted and
approved, and R5 is true on `develop`. Until then the engine drops this repo
from loop scope at scope-check, per the `fleet-deps.yaml` stub contract.