---
name: decision-table
description: >
  Lock recommendations in a four-column Decision Table. Default whenever the
  user asks for a decision table, to lock recs, or to pick among options.
  Do not substitute a prose-only lock or a different table shape.
  Trigger phrases: "decision table", "lock recs", "lock recommendations",
  "vote sheet", "what do you recommend we do".
user_invocable: true
---

# Decision Table

**Default** whenever they ask for a decision table or to lock recs. Full rules live in [`AGENTS.md`](../../AGENTS.md) (one home). Same four columns as Hi Mom / XDTools.

**Do not** answer with a prose-only lock or a different table shape.

## Columns (only these)

`#` · **Decision** · **Options** · **Rec**

- **`#`:** vote id only — not a GitHub issue number. **New topic → start at 1.** Keep a running sequence **only** for a **follow-up** to the table you just locked (1–6, then 7–12). A new table on a new topic (even in the same chat) resets to 1.
- **Options:** **A / B / C / D**. **One option per line** in the cell (`<br>`).
- **Rec:** Unlocked: `B` (one line). Locked: `B<br>Locked` (two lines). No parentheses.
- No separate Lock column.
- Label the block with the GitHub issue.
- Park on the Issue when they like the recs.

Vote in chat: `1) a` · `8) b` · or **all recommended** with overrides.

```markdown
**GitHub:** RTTools#26

| # | Decision | Options | Rec |
|---|----------|---------|-----|
| 1 | Vote ids when the topic changes | A — New topic starts at 1<br>B — Keep running in the thread<br>C — skip | A |
```
