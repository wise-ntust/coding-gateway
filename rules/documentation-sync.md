# Documentation Sync

- `README.md` 是這個 repo 的最新人工總覽與 canonical summary。
- 每次完成動作（feature、bugfix、實驗、移除功能、規則調整）後，必須檢查並更新 `README.md`，讓它反映最新狀態。
- 實驗結果、測試清單、config 範例、操作方式、限制、路線圖都必須與程式碼同步。
- `README.md` 可以包含少量 generated fragment，但生成後的內容仍屬於 canonical summary，不能把重要狀態藏到其他檔案而不回寫 README。
- 如果 `scripts/eval/results/` 出現新的有效結果，尤其是新的 summary CSV、重複實驗統計、或可支持結論的單次量測，必須同步整理進 `README.md` 的 Evaluation 區段。
- 若使用 `scripts/eval/render_readme_eval.py` 這類生成工具，commit 前必須重新生成並通過對應的 sync check。
- 有方法學限制或異常值的結果也要在 `README.md` 記錄，但必須明確標示限制，不能寫成定論。
- 不要讓 `README.md` 落後於 `scripts/eval/results/`、`rules/roadmap.md` 或目前可執行的測試入口。
