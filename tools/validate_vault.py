"""Validate the Obsidian vault: frontmatter, wikilink targets, and callout syntax.

Run from the repository root:

    python3 tools/validate_vault.py

Exits non-zero if any wikilink points at a note that does not exist, if a note
is missing frontmatter, or if a code fence is unbalanced.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

VAULT = Path(__file__).resolve().parent.parent / "docs"

# Capture everything between [[ ]]; alias and heading are split out afterwards.
WIKILINK = re.compile(r"\[\[([^\]]+)\]\]")

FRONTMATTER_FENCE = "---"


def collect_notes() -> dict[str, Path]:
    """Map note stem -> path, so links can be resolved."""
    return {path.stem: path for path in VAULT.rglob("*.md")}


def check_frontmatter(notes: dict[str, Path]) -> list[str]:
    problems = []
    for name, path in sorted(notes.items()):
        lines = path.read_text(encoding="utf-8").splitlines()
        if not lines or lines[0].strip() != FRONTMATTER_FENCE:
            problems.append(f"{path}: missing frontmatter")
            continue
        try:
            end = lines.index(FRONTMATTER_FENCE, 1)
        except ValueError:
            problems.append(f"{path}: unterminated frontmatter")
            continue
        if end == 1:
            problems.append(f"{path}: empty frontmatter")
            continue
        keys = [line.split(":", 1)[0].strip()
                for line in lines[1:end] if ":" in line and not line.startswith(" ")]
        for required in ("tags", "status"):
            if required not in keys:
                problems.append(f"{path}: frontmatter missing '{required}'")
    return problems


def link_target(raw: str) -> str:
    """Reduce a raw [[...]] body to the note name it refers to.

    Obsidian requires the alias pipe to be escaped as ``\\|`` when the link sits
    inside a markdown table, so the split has to tolerate a trailing backslash.
    """
    target = raw.split("|")[0]
    target = target.split("#")[0]
    return target.strip().strip("\\").strip()


def check_links(notes: dict[str, Path]) -> list[str]:
    problems = []
    for name, path in sorted(notes.items()):
        text = path.read_text(encoding="utf-8")
        # Ignore links that appear inside fenced code blocks.
        fenced = set()
        in_fence = False
        for number, line in enumerate(text.splitlines(), 1):
            if line.lstrip().startswith("```"):
                in_fence = not in_fence
            elif in_fence:
                fenced.add(number)

        for number, line in enumerate(text.splitlines(), 1):
            if number in fenced:
                continue
            for raw in WIKILINK.findall(line):
                target = link_target(raw)
                if not target:
                    continue
                # Accept both "Note" and "Folder/Note" forms.
                if target.split("/")[-1] not in notes:
                    problems.append(
                        f"{path}:{number}: broken link [[{raw}]]"
                    )
    return problems


def check_code_fences(notes: dict[str, Path]) -> list[str]:
    problems = []
    for name, path in sorted(notes.items()):
        fences = sum(
            1 for line in path.read_text(encoding="utf-8").splitlines()
            if line.lstrip().startswith("```")
        )
        if fences % 2:
            problems.append(f"{path}: unbalanced code fence ({fences})")
    return problems


# `src/RedisServer.cpp:133` and `include/eviction/cache_entry.h`
CODE_REF = re.compile(r"`([A-Za-z][\w./-]*\.(?:cpp|h|py|md|bat|sh))(?::(\d+))?`")
# A source path mentioned without a line number, e.g. `src/RedisDatabase.cpp`
CODE_PATH = re.compile(r"`((?:src|include|tests|Benchmark|Redis-Server|eviction_policies)/[\w./-]*\.(?:cpp|h|py|md|bat|sh))`")


def check_code_refs(notes: dict[str, Path]) -> list[str]:
    """Verify every source path and file:line reference in the vault.

    A study guide with stale line numbers is worse than one without, because
    the reader cannot tell that it is wrong.
    """
    repo = VAULT.parent
    problems: list[str] = []
    checked = 0

    for name, path in sorted(notes.items()):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for match in CODE_REF.finditer(line):
                relative, line_no = match.group(1), match.group(2)
                target = _resolve_source(repo, relative)
                if target is None:
                    continue
                checked += 1
                if line_no is None:
                    continue
                total = len(target.read_text(encoding="utf-8", errors="replace").splitlines())
                if not 1 <= int(line_no) <= total:
                    problems.append(
                        f"{path}:{number}: {relative}:{line_no} is out of range "
                        f"(file has {total} lines)"
                    )
            for match in CODE_PATH.finditer(line):
                checked += 1
                if _resolve_source(repo, match.group(1)) is None:
                    problems.append(
                        f"{path}:{number}: no such file {match.group(1)}"
                    )
    return problems, checked


def _resolve_source(repo: Path, relative: str) -> Path | None:
    """Locate a source path, tolerating a leading module directory."""
    candidates = [repo / relative]
    for prefix in ("Redis-Server/", "Benchmark/"):
        if relative.startswith(prefix):
            candidates.append(repo / relative[len(prefix):])
        else:
            candidates.append(repo / prefix / relative)
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def main() -> int:
    if not VAULT.is_dir():
        print(f"vault not found at {VAULT}", file=sys.stderr)
        return 2

    notes = collect_notes()
    problems = check_frontmatter(notes) + check_links(notes) + check_code_fences(notes)
    ref_problems, ref_count = check_code_refs(notes)
    problems += ref_problems

    print(f"vault: {VAULT}")
    print(f"notes: {len(notes)}")
    for folder in sorted(p for p in VAULT.iterdir() if p.is_dir()):
        print(f"  {folder.name + '/':<8} {len(list(folder.glob('*.md')))} notes")
    print(f"source references checked: {ref_count}")

    if problems:
        print(f"\n{len(problems)} problem(s):")
        for problem in problems:
            print(f"  {problem}")
        return 1

    print("\nAll checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())