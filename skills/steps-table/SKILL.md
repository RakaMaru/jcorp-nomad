---
name: steps-table
description: >
  Ordered Done / Next / Later work in a four-column Steps Table. Default
  whenever the user asks for a steps table, an enumerated order of next and
  done steps, or to pick a step by number (“let’s do 7”). Do not substitute
  a prose-only list or a decision table.
  Trigger phrases: "steps table", "order of next steps", "done and next
  steps", "let’s do N".
user_invocable: true
---

# Steps Table

**Default** whenever they ask for a steps table (or an enumerated done/next/later list they can pick by number). Full rules live in [`AGENTS.md`](../../AGENTS.md) (one home).

**Do not** answer with two prose lists or a decision table.

## Columns (only these)

`#` · **Status** · **Step** · **Where**

- **`#`:** step id only — not a GitHub issue number. One running sequence for the journey so **let’s do 7** is unambiguous. **New journey → start at 1.** Keep the sequence **only** for a follow-up to this table (status flips, more rows). A new table on a **new** journey (even in the same chat) **resets to 1**. Do not renumber when a row becomes Done (7 stays 7).
- **Order:** rows are **execution order** (top to bottom, contiguous `#`). They follow the table, not jump. **First build** already in that order (Done, then Next, then Later — each block in time order). If a missed gate belongs before later numbers, **insert it and renumber the following rows** so the next line is the next `#`. Do not append that gate at the bottom as 19.
- **Status:** **Done** / **Next** / **Later** (one per row).
- **Step:** one line.
- **Where:** repo, person, or URL.
- Label the block with the GitHub issue when the journey has one.
- Park on the Issue when they like the table or a step changes status.

Pick in chat: **let’s do 7** (that row only). Not `7) a` — that is a decision table.

```markdown
**GitHub:** jcorp-nomad#1

| # | Status | Step | Where |
|---|--------|------|--------|
| 1 | Done | Process kit on disk | this repo |
| 2 | Next | Look locally, then promote | You |
| 3 | Later | Extra polish | this repo |
```
