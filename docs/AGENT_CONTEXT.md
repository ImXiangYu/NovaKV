# NovaKV Agent Context

## Collaboration Preferences
- Use small-step guidance: explain why first, then how.
- Act as a teacher by default: guide only, do not directly write code unless explicitly asked.
- Tests can be written directly, but include test intent.
- No cross-platform file IO adaptation work.
- Build environment is WSL; do not run host-side `cmake`/build commands in this workspace session.
- After each completed feature, remind to check items in `docs/TODO.md`.
- After each feature, provide a QPS note sentence for logging.

## Project Goal
- Build a LevelDB-like KV database for interview-level completeness.
- Prioritize functional correctness over industrial-grade perfection.

## Current Architecture
- MemTable + WAL + SSTable (builder/reader) + BloomFilter + DBImpl.
- Level layout: L0/L1.
- Read path uses newest-to-oldest order.
- L0->L1 compaction exists (sync trigger on threshold).
- `SSTableReader::ForEach` exists and carries `ValueType`.
- Startup can load existing `.sst` files.

## Data Semantics
- `ValueType` is unified in `include/ValueRecord.h`:
  - `kValue`
  - `kDeletion` (tombstone)
- Tombstone is encoded in SST and participates in compaction.

## Recovery and File Numbering
- Recovery responsibility is centralized in `DBImpl` (not MemTable ctor).
- `next_file_number_` is allocated through `AllocateFileNumber()`.
- `MANIFEST` is used to persist/reload `next_file_number_`.
- Directory scan is fallback logic when needed.

## Interface Status
- DBImpl currently uses `ValueRecord`-based API:
  - `Put(const std::string&, ValueRecord&)`
  - `Get(const std::string&, ValueRecord&) const`
- Tests/bench use local adapters to map string values to `ValueRecord`.

## Suggested Next Feature
- Iterator / range scan:
  - `Seek(start_key)`, `Next()`, `Valid()`, `key()`, `value()`
  - Merge MemTable + L0 + L1
  - Newest version wins
  - Tombstone entries are hidden from user-facing iteration

## How To Use In New Chat
- First message: "Please read `docs/AGENT_CONTEXT.md` and continue from there."
