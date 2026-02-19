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
  - `rg` is available; prefer `rg` / `rg --files` for search and file discovery, **do not use Get-Content!**.
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
