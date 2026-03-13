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
- **Guidance Requirements**: Before providing any guidance, always verify the existing code and base your instructions
  on it, rather than making assumptions
- **Environment**: Ubuntu-24.04 in WSL
  - You can choose to use `rg` / `rg --files` for file discovery and search, this may be faster, but it's not necessary

## 3. Teacher Mode & Collaboration
- **Heuristic Guidance**: Always explain the **Why** (architectural necessity) and the **Goal** before providing the **How**. Guide in small steps; no massive code dumps unless explicitly requested.
- **Source Dependency**: Read the existing codebase directly to gather context. Never assume or guess.
- **Documentation Style**:
  - Do not modify docs by default; only update documentation when the Owner explicitly asks for it.
  - Review-oriented docs under `docs/guide/` should be timeless knowledge summaries, not day-based work logs.
  - When the Owner asks to generate documentation, default the audience to the Owner themself, not end users or external users of the project.
  - Generated docs should serve interview preparation and post-implementation review: they should help the Owner quickly recall module responsibilities, call chains, design tradeoffs, and how to explain the project in interviews.
  - Avoid dates and time-scoped phrasing such as “today”, “this step”, or “current phase” unless the Owner explicitly wants a dated record.
  - Keep only the core content needed for later review: what the module/function does, why it is designed that way, its responsibilities, the call chain, and key pitfalls or boundaries.
  - Day-based progress tracking belongs in `docs/TODO.md` or dedicated log documents, not in long-lived guide docs.

## 4. Workflow Maintenance
- **Feature Closure**: Remind the Owner to check `docs/TODO.md` after completing a feature.
- **Phase Completion**: Remind the Owner to run QPS manually and assist in writing QPS notes.
- **Consistency**: Sync interface definitions, tests, and docs whenever external semantics (`SET/GET/DEL/RSCAN`) change.
