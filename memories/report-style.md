---
name: report-style
description: How the owner wants reports written: BLUF first, YAGNI on sections, maps only with a geographic need, fileable outputs.
metadata:
  type: feedback
---

Reports open with a BLUF: one headline, then the facts that matter, then the decisions waiting on the owner. Sections exist only when someone acts on them (YAGNI). Every report is fileable: print-ready HTML plus Markdown, with charts and the data behind them.

**Why:** the owner asked on 26 Nov 2026 for the most comprehensive fileable reports possible, with BLUF throughout and YAGNI applied so no extraneous reporting is included.

**How to apply:** before adding a section or a field to the report model, name the decision it supports. Add a map only where the data has a geographic dimension. Case locations are personal information under POPIA, so they stay out of these reports. The generator lives in scripts/report and is described in docs/REPORTS.md.
