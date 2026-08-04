"""Extract the enum-to-string maps of a library's `*Name()` functions.

`symbols.py` compares names and shapes, which cannot see a difference in what a
function *returns*. `lastErrorName()` was exactly that: identical signature in
both libraries, `INVALID_ARGUMENT` in one and `InvalidArgument` in the other, so
code that logs or compares the string did not port. Nothing in either public
header shows it.

The functions in scope are the ones that translate an enum constant into a string
for the application to print or compare — a body that is a `switch` over enum
constants returning string literals. They are found by shape rather than by a
hard-coded list, so a second one added later is compared without touching this
file.

Only implementation files carry these bodies, so the EspBle side of the comparison
is a committed snapshot (`espble.values`) produced by `tools/gen_api_parity.py`.
"""

import re

_FUNCTION = re.compile(
    r"const\s+char\s*\*\s*(?P<owner>[A-Za-z_]\w*)::(?P<name>[A-Za-z_]\w*Name)\s*"
    r"\([^)]*\)\s*(?:const\s*)?\{"
)
_CASE = re.compile(r"case\s+([A-Za-z_]\w*::[A-Za-z_]\w*)\s*:\s*return\s+\"([^\"]*)\"")
_FALLBACK = re.compile(r"(?<![:\w])return\s+\"([^\"]*)\"\s*;\s*\}?\s*$")

# The key used for the value returned when no case matched. It is part of the
# contract too: an application that logs an unexpected error prints this.
DEFAULT_KEY = "<default>"


def _body(text, start):
    """Return the function body that starts at the brace index `start`."""
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:index]
    return ""


def extract(source_text):
    """Return {function name: {enum constant or <default>: string}}.

    A function whose body has no `case Enum::Constant: return "..."` at all is not
    a name map and is skipped, so an ordinary `const char *…Name()` accessor does
    not turn into a comparison target.
    """
    maps = {}
    for match in _FUNCTION.finditer(source_text):
        body = _body(source_text, match.end() - 1)
        entries = {constant: value for constant, value in _CASE.findall(body)}
        if not entries:
            continue
        for line in reversed(body.strip().splitlines()):
            fallback = _FALLBACK.search(line.strip())
            if fallback:
                entries[DEFAULT_KEY] = fallback.group(1)
                break
        maps[match.group("name")] = entries
    return maps


def flatten(maps):
    """Return {(function, key): string}, the form the parity table lists."""
    return {
        (function, key): value
        for function, entries in maps.items()
        for key, value in entries.items()
    }
