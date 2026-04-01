---
name: Workflow Rules
description: How to work on this project per claude.md instructions
type: feedback
---

After each task: create `.claude/tasks/<task-name>.txt` with implementation summary.
After each task: update `.claude/architecture.md`.
Use one subagent per package/model for isolated work.
Never rewrite full files unless required — modify only what's needed.

**Why:** Keeps context clean, enables parallel work, documents progress for future sessions.
**How to apply:** Always create task summary txt + update architecture.md at end of each task.
