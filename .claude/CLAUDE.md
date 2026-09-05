# Arches-Instrumentation

Evidence-capture leaf of the Arches IV&V fleet. **Currently a stub**: no code,
no build, no validation gate. [REQ.md](REQ.md) holds the proposed scope and is
awaiting owner approval; loop-016 in the master repo's
`loop/workflow/backlog/` is the bootstrap loop.

## How you work here

* This repo is `status: stub` in `loop/fleet-deps.yaml` — the engine drops it
  from loop scope at scope-check. The only loop allowed to touch it is the one
  that makes it real (loop-016), after the owner approves REQ.md.
* There is no `.claude/validate.sh` and no `Makefile` — there is nothing to
  build. Preflight reports `n/a (no gate)` for `stack: none` (same as
  Arches-Hardware). Do not treat a missing gate as a pass; treat the repo as
  not-yet-real.

## Do

* Keep REQ.md the single source of truth for scope. Changes to scope go there
  first, as edits the owner reviews.
* When the stack is chosen (REQ.md R5): create `tools/validate/stack.txt`, add
  the matching template under `tools/validate/templates/`, register the stack
  in `loop/fleet-deps.yaml`, and generate the gate with
  `tools/validate/generate.sh` — in that order, in one commit.

## Don't

* **Don't write application code before REQ.md is approved.** A stub exists
  precisely so scope is settled before implementation starts.
* **Don't add a `.claude/validate.sh` by hand.** Gates are generated from
  `tools/validate/templates/<stack>.sh`; a hand-written gate drifts from the
  fleet contract. When a gate exists, it is human-owned: run it, never edit it.
* **Don't absorb the `Apps/` bench tools silently.** Their disposition is REQ.md
  R6 — an explicit, documented decision, not a creeping move.

*Loop-derived lessons for this repo live in the fence below, pending curation
into the lists above.*

<!-- BEGIN loop-engine -->
<!-- END loop-engine -->