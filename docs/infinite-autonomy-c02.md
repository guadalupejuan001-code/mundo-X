# InfiniteCore C02 — independent XI workers and transactional authority

Base: InfiniteCore C01 PR #83, commit e4fb235e0e64289fd0a08ca00c2465143d8e0dc1.
Status: stacked review candidate, not merged and not integrated into consumer applications.

## Design

C02 introduces an opt-in AutonomousCoreC02 with 1, 2, 4 or a configurable number of CPU worker threads. Every worker creates its own InfiniteSession, restores a captured canonical snapshot, and executes the unmodified native XI interpreter. A private outcome cannot write another worker's state.

Two explicit modes prevent silent loss of changes:

* Isolated: performs work in a private state, returns XI output, and optionally returns its resulting snapshot; the central authority is untouched.
* Transactional: performs work in a private state, constructs the candidate snapshot, then acquires the central mutex. A commit occurs only if the source epoch still matches the authority. A stale candidate is discarded and re-executed against the new snapshot. Exhausting the configured retry count returns a conflict error; no stale candidate is committed.

The sole central InfiniteSession is private to C02, with no mutable owner handle exposed. Only C02-mediated work enjoys the global authority contract. C01 remains available unchanged for legacy callers. Epoch comparison is atomic with restoration because both run under the same C02 mutex. This does not constitute a general multi-version merge of conflicting changes.

Read-only transactional queries also re-evaluate on epoch mismatch, preventing stale results being mistaken for the latest authority state. A private snapshot is a full valid XI state; a 51-byte representation elsewhere does not imply 51-byte task memory.

## Queue and memory policy

Configurable defaults: 1024 pending tasks, up to 16 MiB program input, 128 MiB combined admitted program bytes (pending **plus running**), 2 workers. These are admission limits, not preallocated RAM and not a limit on the rule-defined logical address space. Native XI has additional independent limits for lines, commands, operations and state resources.

On Windows the available-host-memory probe uses GlobalMemoryStatusEx; on Linux it reads MemAvailable in /proc/meminfo. The dynamic input budget is min(128 MiB, 20% of current available memory after reserving 384 MiB). If the probe is unsupported, the configured static input cap remains effective. The probe and margin are safeguards, not an exact estimate of C++ heap, snapshots or memory required for quantum execution. No software-only guard can guarantee absence of OOM from arbitrary native execution.

The admission gate checks both pending task count and total outstanding program bytes, denying new tasks when the dynamic or hard cap would be exceeded. Existing accepted tasks are drained during shutdown.

## Scheduling and aging

Weighted round robin remains 4:2:1 (interactive:normal:background), without wasting empty priority slots. After 2 seconds a background head can use normal slots. After 5 seconds a normal/background head gets immediate selection based on oldest wait. A worker cannot preempt an already-running XI program. Thresholds are monotonic-time based, configurable and tested through a deterministic pure selection function.

## Conflict safety and scope

Transactional commits are serial, although private XI executions can be parallel. Coarse epoch matching may replay work that actually touched disjoint qubits; this is deliberate until write sets and merge safety can be proved. Under contention the single authority can be a bottleneck. No GPU/QSSD, tensor truncation, state splitting or platform-kernel replacement is introduced.

The code does not alter InfiniteCore, EtherCore, CompactCore, InfiniteSession, the XI interpreter, checkpoints or original C01. No physical quantum speedup is asserted. Clients must opt in to C02; current Android, PC and BBY Q paths are not silently migrated.

## Checks and performance

* C02 native test: admission boundaries (1024, 16 MiB, 128 MiB), dynamic pressure, weighted fairness, aging, unused slots, serial XI equivalence, private-state isolation, native rollback, forced optimistic conflicts with deterministic barriers, retry success and conflict rejection.
* Original C01/infinite/ether/native tests must continue passing.
* Native benchmark runs the same isolated XI workload at 1, 2 and 4 workers, two trials each, in Windows and Ubuntu runners. It reports elapsed ms, jobs/sec, wait and execution p50/p95, peak running workers, reserved input bytes, host-available bytes and effective input cap.
* Benchmarks are host- and workload-specific, not proof of quantum acceleration or general scalability. Real measurements are in the workflow's autonomous-c02-benchmark.json artifact, never invented in this document.
