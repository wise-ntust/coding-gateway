# Git Workflow

## Commit Discipline

- 完成一個段落（roadmap item、feature、bugfix）後自動 commit 並 push，不需要詢問。
- 目的是即時記錄每次改動，保持 remote 同步。
- 非程式碼變更也照常 commit 並 push，但可依 `./rules/build-and-test.md` 的例外略過三平台 build gate。
- Commit message format:
  - Short imperative subject line, 72 characters or fewer
  - Blank line
  - Body when needed
