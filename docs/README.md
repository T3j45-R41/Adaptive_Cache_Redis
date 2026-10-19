---
tags: [moc, meta, how-to-use]
status: index
---

# About this vault

## Opening it in Obsidian

**Open this `docs/` folder as a vault**, not the repository root:

1. Obsidian → *Open folder as vault* → select `docs/`
2. Open `Home.md` — it is the map of content

Selecting the repo root instead also works, but the vault will then include the
C++ sources, which adds noise without adding links.

No plugins or themes are required. It uses only core Obsidian syntax:
wikilinks, tags, callouts, and frontmatter.

## What is in here

| Folder | Contents |
| --- | --- |
| `OS/` | 10 operating-systems concept notes + index |
| `DBMS/` | 10 database-management concept notes + index |
| `Project/` | 7 notes on this specific codebase + index |

## Conventions

**Tags** are hierarchical, e.g. `#dbms/buffer-pool`. Click any tag to see
related notes. `#project/adaptive-cache-redis` appears on every note.

**Callouts** carry the meaning:

> [!success] Implemented
> Present in the code, tested, and working.

> [!warning] Not implemented
> Discussed for comparison or as an extension. **No code exists for this.**

> [!abstract] Partial
> A simplified form exists; the note says exactly how it differs.

**Frontmatter** on every note:
```yaml
tags: [dbms, buffer-pool, project/adaptive-cache-redis]
status: implemented
```

`status` is one of `implemented`, `partial`, `not-implemented`,
`documented-only`, `reference`, `index`.

> [!important] Why the status distinction is enforced
> This is an academic project, so "we discussed it" and "we built it" must not
> be confused. Every claim in this vault is either traced to working code or
> explicitly marked as absent. If something here says *not implemented*, that is
> a finding, not a gap in the writing.

---

## Validating the vault

```bash
python3 tools/validate_vault.py
```

Checks that every note has frontmatter, every wikilink resolves, code fences
are balanced, and — most importantly — that all **261 source references** still
point at files and line numbers that exist.

> [!tip] Run this after editing source files
> A study guide whose `file:line` references have drifted is worse than one
> without them, because the reader has no way to tell it is wrong. The validator
> catches existence and range; a spot-check of content is still worth doing
> after large refactors.

---

## Related

- [[Home]] — the main map of content
- [[OS Index]] · [[DBMS Index]] · [[Project Index]]
- [[Project/Concept-to-Code Map]] — every concept in one table
- [[Project/Gaps and Future Work]] — what is deliberately absent