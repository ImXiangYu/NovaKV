# NovaKV Agent Context

## Owner Background and Intent
- Owner is building NovaKV as a portfolio project for C++ backend job interviews.
- Project target is not "production-ready database", but "interview-grade system design + implementation depth".
- Owner wants NovaKV to support two independent interview narratives:
  - Storage narrative: LSM/WAL/SST/compaction/recovery correctness.
  - Network narrative: client-server protocol, concurrency model, and service reliability.
- Current focus: finish storage correctness and then introduce a strong network service layer (`epoll + thread pool` direction).

## Collaboration Preferences
- Use small-step guidance: explain why first, then how.
- Act as a teacher by default: guide only, do not directly write code unless explicitly asked.
- Tests can be written directly, but include test intent.
- Hard rule: except tests and docs, do not directly modify production/source code unless owner explicitly asks for code edits in that turn.
- No cross-platform file IO adaptation work.
- Build environment is WSL; do not run host-side `cmake`/build commands in this workspace session.
- After each completed feature, remind to check items in `docs/TODO.md`.
- If QPS is explicitly requested or needed, provide a QPS note and clearly mark newly added feature(s), even when QPS shows no significant change.
- When external semantics (`SET/GET/DEL/SCAN`) change, sync interface definitions, tests, and docs in the same change.

## Project Positioning
- Build a LevelDB-like KV database with a client-server architecture for interview demonstration.
- Prioritize functional correctness and explainable design tradeoffs over industrial completeness.
- Design decisions should stay explainable in interviews (clear boundaries, measurable effects, known tradeoffs).

## Current Architecture Snapshot
- Core path exists: MemTable + WAL + SSTable (builder/reader) + BloomFilter + DBImpl.
- Level layout currently: L0/L1.
- Read path is newest-to-oldest.
- L0->L1 compaction exists (sync trigger on threshold).
- `SSTableReader::ForEach` is available and carries `ValueType`.
- Startup can load existing `.sst` files and replay `.wal` files.
- Iterator path exists (`DBIterator`) and merges MemTable + L0 + L1 with newest-version priority.

## Data and Semantics
- `ValueType` is unified in `include/ValueRecord.h`:
  - `kValue`
  - `kDeletion` (tombstone)
- Tombstone is encoded in WAL/SST and participates in merge/compaction decisions.
- User-facing read/scan semantics should hide tombstoned keys.

## Recovery and Metadata Status
- Recovery responsibility is centralized in `DBImpl` (not MemTable ctor).
- `next_file_number_` is allocated through `AllocateFileNumber()`.
- `MANIFEST` currently persists/reloads `next_file_number_`.
- Directory scan works as fallback.
- Remaining gap: full version metadata (live files and level mapping) is not complete yet.

## Interface Status
- DBImpl uses `ValueRecord`-based API:
  - `Put(const std::string&, ValueRecord&)`
  - `Get(const std::string&, ValueRecord&) const`
- Tests and benchmark currently include adapters mapping string API to `ValueRecord`.

## Near-Term Design Direction
- Keep storage and networking as separate modules with explicit boundaries.
- Storage first:
  - close correctness gaps in delete semantics, recovery metadata, and background tasks.
- Then networking:
  - introduce server/client protocol and concurrency model (`epoll + thread pool`) as first-class interview topic.

## Definition of "Interview-Ready" for NovaKV
- Can explain full write/read/recovery path with correctness guarantees.
- Has reproducible tests for semantics and restart recovery.
- Has benchmark evidence (QPS + latency perspective).
- Has network service design that is independently discussable in backend interviews.

## How To Use In New Chat
- First message: "Please read `docs/AGENT_CONTEXT.md` and continue from there."
