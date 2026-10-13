---
name: document
description: Write down everything a new session needs to pick up the work where it stands (PLAN status, branches, what is on the consoles, decisions, issues, docs, memory), then commit it to the release branch and push. Use when the owner says /document, "documenta", or is about to close the conversation.
---

# Document the session for the next one

A new session starts from `CLAUDE.md` and `docs/PLAN.md` only (plus the auto-memory). Anything
that lives only in this conversation is lost. Go through every step; skip none because "it was
already done": check it.

Chat with the owner in Spanish; everything written to the repository in English, in the style of
the files around it.

## 1. Collect what happened

Before writing, list for yourself, from this conversation and from git:

- `git log --oneline origin/release/vX.Y.Z..HEAD` on every local branch, `git branch`,
  `git worktree list`, `git status` in each worktree: work not merged, not pushed or not committed.
- Bugs found or fixed, with their issue numbers (`gh issue list --repo tinaut1986/zelda-tmc-3ds`).
- Decisions the owner took or confirmed, and why (their words when it matters).
- Tasks finished, confirmed by the owner, or added; priorities changed.
- What is installed on each console (build, settings that matter, save states that belong to the
  owner, anything Claude left there), and the consoles' IPs.
- Tools, options, file formats or workflows added or changed.
- Lessons that cost time and are not obvious from the code (a tool that misleads, a step that
  crashes the console, something the owner asked not to do).

## 2. Put each fact in its one place

The table in `CLAUDE.md` ("Each fact lives in one place") decides where. In short:

| Fact | Where |
|---|---|
| task finished / confirmed by the owner | tick it in `docs/PLAN.md`; the date in its *Done when* |
| task added, changed, split, dropped | its spec in PLAN (IDs never reused) |
| why something was done this way | PLAN decisions log, dated, at the end |
| a bug: what, which commit, what to check | its GitHub issue (comment or new issue), never a copy in PLAN |
| branch opened, handed for testing, confirmed, merged | PLAN Status branch table (merged rows get `_(merged into ... date)_`) |
| priority / what to do next | PLAN Status: **Priority** line and **Start here** paragraph (date it) |
| what runs on each console | PLAN Status: **On the owner's consoles now** |
| tool / option / workflow | its doc: `docs/debug-tools.md`, `docs/save-states.md`, `platform/3ds/README.md`, `CLAUDE.md` Build / console sections |
| how Claude should work (owner's feedback), lessons about the environment | auto-memory (`~/.claude/projects/.../memory/`, one fact per file, indexed in `MEMORY.md`) |

PLAN's Status holds only what no other place records: open branches, what waits for the owner's
check, what is on the consoles, the priority order and where to start. Do not paste issue or spec
content into it; replace stale paragraphs instead of adding new ones next to them.

**Start here** must let a new session act without reading the chat: the next tasks in order, each
with the issue or spec that explains it, and anything half done (an uncommitted change, a branch
waiting for the owner, a test interrupted) with where it is.

## 3. Check it reads right

- Re-read PLAN's Status top to bottom as a stranger: every branch in the table exists (or says it
  was merged), every "still to check" is still true, dates are absolute.
- `grep -n "^<<<<<<<\|^>>>>>>>" docs/PLAN.md` returns nothing.
- Links between places hold: the issue names the commit, PLAN names the issue.

## 4. Commit, merge, push

Following `CLAUDE.md` (never commit directly on `release/*` or `main`):

1. A topic branch cut from the active release branch, e.g. `docs/plan-<what>`.
2. Commit (English message, the attribution trailer from the session's instructions).
3. `git merge --no-ff` into the release branch, `git push origin release/vX.Y.Z`.
4. Commits that belong to unmerged topic branches stay on those branches; say so in PLAN's
   branch table rather than merging them to get them documented.

Ask the owner before merging code branches the owner has not confirmed; documentation-only
branches made by this skill can be merged and pushed directly.

## 5. Report

Tell the owner in a few lines (Spanish): what was written where, what is still open, and anything
that could not be documented (and why).
