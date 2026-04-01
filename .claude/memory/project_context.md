---
name: Project Context
description: Blob_war AI game project structure, key files, and active tasks
type: project
---

C++17 game AI project using TBB for parallelism.

**Key packages:**
- `src/ai/NegamaxParInc/` — main AI (YBW + Zobrist TT + LMR + PMR + incremental state)
- `src/ai/NegamaxVariants/` — archived variants (reference only)
- `optimizer/` — auto parameter tuning via coordinate descent

**Active task:** Task 1 — add iterative deepening + killer heuristic to 3 models.
**Why:** Time-based search beats fixed depth; killer heuristic improves move ordering.
**How to apply:** YBWsearchDYN (new), NegamaxParIncAIStud (modify), NegamaxParIncAIDYN (new).

Task 2: rewrite optimizer to use NegamaxParIncAIDynStud vs YBWDyn(250ms) with exponential backoff parameter tuning.
