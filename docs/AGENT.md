# NovaKV Agent Context

## 1. Project Identity & Philosophy
- **Core Goal**: A LevelDB-like client-server KV database portfolio project for C++ backend interviews.
- **Design Philosophy**: Focus on "interview-grade system design" and "explainable tradeoffs" (MVP mindset). Prioritizes functional correctness over production-ready completeness or extreme micro-optimizations.
- **Dual Narrative**: 
  - *Storage*: LSM/WAL/SST/compaction/recovery correctness (Current Focus).
  - *Network*: Client-server protocol, epoll + thread pool concurrency, and service reliability.

## 2. Hard Constraints & Environment
- **Test Execution Ban**: **NEVER run any tests (unit, integration, benchmark, QPS) or build commands (e.g., cmake) in this workspace.** Only provide test code, commands, and checklists; the Owner runs everything manually.
- **Code Modification**: Do not directly modify production/source code unless explicitly requested for that specific turn. Tests (which must include "Test Intent") and documentation are exceptions.
- **Environment**: Windows PowerShell.
  - **MUST** use `rg` / `rg --files` for file discovery and search.
  - **STRICTLY BAN** the use of `Get-Content`.
  - All path separators, script calls, and commands must be PowerShell compatible.

## 3. Teacher Mode & Collaboration
- **Heuristic Guidance**: Always explain the **Why** (architectural necessity) and the **Goal** before providing the **How**. Guide in small steps; no massive code dumps unless explicitly requested.
- **Source Dependency**: Read the existing codebase directly to gather context. Never assume or guess.

## 4. Workflow Maintenance
- **Feature Closure**: Remind the Owner to check `docs/TODO.md` after completing a feature.
- **Phase Completion**: Remind the Owner to run QPS manually and assist in writing QPS notes.
- **Consistency**: Sync interface definitions, tests, and docs whenever external semantics (`SET/GET/DEL/SCAN`) change.