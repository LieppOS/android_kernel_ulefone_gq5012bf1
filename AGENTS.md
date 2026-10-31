# Kernel development workflow

Applies to this kernel repository. Follow the canonical workspace `AGENTS.md`
and its linked engineering, publication, ownership and device-safety policies.

## Owner-directed main-branch workflow (2026-10-31)

- `main` is the normal development and GitHub default branch. The owner has
  explicitly assigned it for routine kernel work, including reviewed, validated
  commits and ordinary non-forced pushes to the existing LieppOS `origin`.
- Use the canonical workspace checkout at
  `android/kernel/ulefone/gq5012bf1/`. Do not create per-task branches or extra
  worktrees unless the owner requests an exception.
- Keep one writer at a time. Confirm the previous agent is finished or paused
  before writing; concurrent agents must not edit, stage, commit or switch the
  same checkout. Preserve any unrelated staged, unstaged or untracked work.
- Existing integration/investigation branches and worktrees are preserved for
  history and recovery. They are not the default destination for new work.
- Inspect the full change and unpublished history, run relevant checks, and
  complete publication review before committing/pushing. Never bypass branch
  protection, force-push, or publish held evidence, keys or calibration data.
- A merge or successful offline test does not establish hardware functionality.
  Keep experimental diagnostics explicitly labelled and disabled by default.
  Kernel integration never authorizes phone access, flashing or power changes.
