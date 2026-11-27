"""Template filling and slugs, shared by the session, decision and site writers."""
from __future__ import annotations

import re

SLOT = re.compile(r"\{\{(\w+)\}\}")


def fill(template: str, values: dict[str, str]) -> str:
    """Replace every {{key}} in template with values[key], in one pass.

    A value is inserted as is: a value that itself contains {{other}} is not expanded again.
    A slot with no value raises KeyError, so a template and its writer cannot drift apart quietly.
    """
    def one(m: re.Match) -> str:
        key = m.group(1)
        if key not in values:
            raise KeyError(f"template slot {{{{{key}}}}} has no value")
        return values[key]

    return SLOT.sub(one, template)


def slug(text: str, max_len: int, default: str) -> str:
    """Lower-case words joined by '-', at most max_len characters, or default when empty."""
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s[:max_len].strip("-") or default
