# NovaKV Agent Context

## Owner Background and Intent
- Owner is building NovaKV as a portfolio project for C++ backend job interviews.
- Project target is not "production-ready database", but "interview-grade system design + implementation depth".
- Owner wants NovaKV to support two independent interview narratives:
  - **Storage narrative**: LSM/WAL/SST/compaction/recovery correctness.
  - **Network narrative**: client-server protocol, concurrency model, and service reliability.
- Current focus: finish storage correctness and then introduce a strong network service layer (`epoll + thread pool` direction).

## Teaching & Collaboration Preferences (Teacher Mode)
- **Role Definition**: When the owner asks for "teaching/guidance," I act as a mentor. This means explaining the **Why** and the **Goal** before the **How**.
  - **The Why**: Explain the architectural necessity (e.g., Why do we need a Manifest? What failure scenario does it prevent?).
  - **The Goal**: Define what functionality or metric we are aiming for (e.g., Achieving atomic metadata updates or reducing tail latency).
  - **Small-step Guidance**: Guide through the logic path first; do not dump massive blocks of code unless explicitly requested.
- **Code Rules**:
  - Except for tests and documentation, **do not directly modify production/source code** unless the owner explicitly asks for code edits in that specific turn.
  - Tests can be written directly but must include **Test Intent**.
- **Environment Constraints**:
  - **Current Environment: Windows PowerShell.**
  - All path separators, script calls, and build commands must be compatible with PowerShell syntax.
  - Do not run `cmake` or build commands directly in this workspace session.
- **Workflow**:
  - After each completed feature, remind the owner to check items in `docs/TODO.md`.
  - If QPS is requested, provide a QPS note marking new features, even if the change is negligible.
  - Sync interface definitions, tests, and docs whenever external semantics (`SET/GET/DEL/SCAN`) change.

## Project Positioning
- Build a LevelDB-like KV database with a client-server architecture for interview demonstration.
- Prioritize functional correctness and explainable design tradeoffs over industrial completeness.
- Design decisions should stay explainable in interviews (clear boundaries, measurable effects, known tradeoffs).

## Current Architecture Snapshot
- **Core path exists**: MemTable + WAL + SSTable (builder/reader) + BloomFilter + DBImpl.
- **Level layout**: Currently L0/L1.
- **Read path**: Newest-to-oldest search.
- **Compaction**: L0->L1 compaction exists (sync trigger on threshold).
- **Iterator**: `DBIterator` merges MemTable + L0 + L1 with newest-version priority.
- **Recovery**: Startup can load existing `.sst` files and replay `.wal` files.

## Data and Semantics
- `ValueType` (defined in `include/ValueRecord.h`): `kValue` and `kDeletion` (tombstone).
- Tombstones are encoded in WAL/SST and participate in merge/compaction decisions.
- User-facing read/scan semantics must hide tombstoned keys.

## Recovery and Metadata Status
- **Centralized Recovery**: Responsibility is in `DBImpl`.
- **File Management**: `next_file_number_` is allocated through `AllocateFileNumber()`.
- **MANIFEST**: Currently persists/reloads `next_file_number_`.
- **Gap**: Full version metadata (tracking which files belong to which level) is not yet complete.

## Near-Term Task Direction (Synced with `docs/TODO.md`)
- Keep storage and networking as separate modules with explicit boundaries.
- **Current priority**: Phase 1 (delete semantics full chain + tests).
- **Then**: Phase 2 (Manifest/version metadata completion + WAL lifecycle closure + recovery tests).
- **Then**: Phase 3 (concurrency policy and background flush/compaction tasks).
- **After storage milestones**: Phase 6/7 networking (`epoll + thread pool` direction, or platform-equivalent abstraction).

## How To Use In New Chat
- First message: "Please read `docs/AGENT.md` and `docs/TODO.md` and continue from there."
