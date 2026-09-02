# Agent instructions — jcorp-nomad

**Who reads this:** Grok / coding agents (and humans who want the same rules).  
**How to use:** This file is *how we work*. Product README stays the firmware/UI truth.

This repo is **public**. Do not put secrets or private issue links in commits.

---

## Start here

1. **This file**
2. **[`README.md`](README.md)**
3. GitHub **`RakaMaru`**. `gh auth switch -h github.com -u RakaMaru` if needed (do not run `gh-set-user-RakaMaru.bat`; it pauses).
4. **Decision table** — [`skills/decision-table/SKILL.md`](skills/decision-table/SKILL.md).
5. **Steps table** — [`skills/steps-table/SKILL.md`](skills/steps-table/SKILL.md). Pick with **let’s do N**.

---

## What this repo is

ESP32-S3 portable offline media server (Jcorp Nomad). RakaMaru clone. Not RTTools, not Phantom Driver, not SiteCrafter.

---

## Identity

GitHub **RakaMaru** / `RakaMaru/jcorp-nomad` (**public**). Commit **RakaMaru** / `rakamaru@gmail.com`.

**GitHub MCP** is **RakaMaru**. Do **not** use it for XtremeLashes.

---

## Discuss vs implement

**Default: do not change files** until they ask. **Report ≠ fix.** Local **`dev`**. Do not run promote bats unless asked.

**`later`** = deferred. Do **not** create a label named `backlog`.

Decision tables: same four columns as RTTools.

Steps tables: `#` · **Status** · **Step** · **Where**. New journey starts at 1. Pick with **let’s do 7**. Insert a missed gate and renumber the following rows.

**“Do it”** on a lock is write + park + one-liner, not commit/promote.

Issues: https://github.com/RakaMaru/jcorp-nomad/issues

---

### Changelog

| Date | Change |
|------|--------|
| 2026-08-22 | First AGENTS.md (thin kit). |
| 2026-08-23 | Process kit (RTTools#59). |
| 2026-09-01 | Steps table skill; pick with **let’s do N**. “Do it” on a lock is write + park + one-liner. |
