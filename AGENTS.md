# AGENTS.md — coding-gateway

## Required Entry Point

Any AI agent working in this repository MUST treat this file as the primary repository instruction entrypoint.

Before any reply, analysis, planning, code edit, file edit, search, test run, build, git action, or other task action, the agent MUST read this file first.

The agent MUST then read the required files under `./rules/` in the order listed below before continuing.

If the agent cannot read this file or any required rule file, it MUST say so explicitly and stop making repository-specific assumptions.

## Instruction Precedence

1. Direct user instructions
2. This file, `AGENTS.md`
3. The required files in `./rules/`
4. Agent defaults, vendor defaults, and generic behavior

Repository-specific instructions in this file and `./rules/` override generic agent behavior.

## Required Rule Load Order

1. Read this file first.
2. Then read and follow these files in this exact order:
   - `./rules/language.md`
   - `./rules/project-overview.md`
   - `./rules/coding-style.md`
   - `./rules/git-workflow.md`
   - `./rules/build-and-test.md`
   - `./rules/documentation-sync.md`
   - `./rules/engineering-principles.md`
   - `./rules/roadmap.md`
3. Do not skip files because the current task looks small or unrelated.
4. When multiple files apply, the more specific rule wins.
5. Keep `README.md`, this file, and the relevant rule files in sync when requirements change.
6. Treat `README.md` as the repository's canonical human-readable summary of current features, tests, experiments, limitations, and supported workflows.
7. Do not leave new valid evaluation results or changed behavior reflected only in `scripts/eval/results/`, commit history, or code; propagate them into `README.md`.

## Compatibility Notes

- These instructions are intended for any AI coding agent, not only Claude.
- If the repository also contains `CLAUDE.md`, `GEMINI.md`, `COPILOT.md`, or similar vendor-specific instruction files, treat them as pointers back to this document unless they contain stricter repository-specific requirements.
- Do not prefer vendor-specific defaults over this repository's instruction chain.

## Execution Guard

- Do not start repository work until this file and the required `./rules/*.md` files have been read.
- Do not invent missing repository rules.
- If a required rule file is missing, unreadable, or contradictory, report that clearly before proceeding.
