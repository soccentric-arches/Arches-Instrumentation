# Arches-Instrumentation

Evidence-capture layer of the Arches IV&V mission: observes the system under
test and the bench, records timestamped evidence keyed to IV&V runs, and makes
it retrievable through Arches-Server.

## Status: STUB

This repo is intentionally empty of code. It is registered as `status: stub`
in [`loop/fleet-deps.yaml`](../../loop/fleet-deps.yaml) — the Loop Engine drops
it from loop scope at scope-check until it becomes real.

* Requirements and proposed scope: [REQ.md](REQ.md) — **DRAFT, awaiting owner
  approval**.
* Bootstrap loop: `loop/workflow/backlog/loop-016-instrumentation-leaf.md` in
  the master repo.
* No stack, no build, no validation gate yet (preflight reports `n/a` for
  `stack: none`). When the stack is chosen (R5 in REQ.md), add
  `tools/validate/{stack.txt,templates/}` and generate the gate with
  `tools/validate/generate.sh`.

## Layout

```
REQ.md               requirements draft (source of truth for scope)
.claude/CLAUDE.md    how agents work in this repo
herdr.sh             managed file, byte-identical across the fleet
```