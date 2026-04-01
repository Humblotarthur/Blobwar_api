# Claude Operating Rules

Read all the following rules:

## Token Efficiency (MANDATORY)

- Be concise by default and limit token usage.
- Do not explain obvious concepts.
- For code: modify only what is necessary.
- Never rewrite full files unless required.
- If code change is large/complex → ask before proceeding.


## Rule Delegation

- Go read Global rules → see `clinerules.md`
- Go read Workflow & execution → see `workflow.md`
<!-- - Security rules → see `security.md` do not apply security rules for this project don't read the file -->
- Go read Tasks & tracking → see `tasks/todo.md`
- Go read Lessons & mistakes → see `tasks/lessons.md`
- Go read Architecture of the project -> see `architecture.md`

## Behavior

- If unsure → read files or ask.
- If ambiguous → clarify before acting.
- If complex → propose a short plan first.
- Stay minimal, precise, and practical.

# Package Architecture Rules (MANDATORY)

- Design each package to be as independent as possible.
- Minimize dependencies between packages.
- Explicitly define and document all inter-package dependencies.
- Avoid tight coupling — no hidden or implicit links between packages.
- Keep all package-specific logic strictly encapsulated within its package.
- Do not leak internal implementation details across package boundaries.
- Ensure each package can be understood and worked on in isolation.
- Optimize for partial loading (avoid requiring full project context).
- Structure code so subagents can operate on a single package only.
- Isolate bugs by preventing cross-package side effects.
- Prefer clear interfaces over shared internal logic.
- Avoid circular dependencies at all costs.
- Changes in one package must not break others unexpectedly.
- Keep package APIs stable and minimal.
- Group related functionality within the same package — avoid scattering logic.
- Refactor when a package becomes too dependent on others.
- Test packages independently whenever possible.
- Favor modularity, maintainability, and scalability over shortcuts.


You can start working with all thoose rules in mind with the file `tasks.md`