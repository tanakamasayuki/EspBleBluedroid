"""Public API surface extraction for the EspBle / EspBleBluedroid parity check.

The two libraries are meant to expose the same BLE API, so a difference has to be
a deliberate, classified decision rather than drift nobody noticed. This module
turns a root header into a set of symbol names, and `test_api_parity.py` fails
when the comparison against `docs/API_PARITY.tsv` finds something the table does
not explain.

Scope and deliberate limits:

- Only the root header is parsed (`EspBle.h` / `EspBleBluedroid.h`). The shared
  backend-independent headers are verbatim copies of each other, so a plain diff
  already covers them.
- `private:` / `protected:` sections, `#if*_TESTING` blocks, comments, and
  preprocessor lines are dropped: they are not public API.
- A symbol is a name plus, for anything callable, its argument count —
  `EspBleScanner::stop/0`. Parameter *types* are not compared. The goal is to
  catch a missing, renamed, or extra-argument method and a lost struct field, not
  to prove signature equality.
- Overloads collapse into one symbol per argument count, which is what a caller
  actually depends on.
- The intentional root rename is normalised away: `EspBleBluedroid` reads as
  `EspBle`, so `EspBleBluedroid::begin/1` and `EspBle::begin/1` are one symbol.
"""

import re

_TESTING_GUARD = re.compile(r"^#\s*if(?:def)?\s+.*_TESTING\b")
_ACCESS = re.compile(r"(?:^|\s)(public|protected|private)$")
_TYPE_OPEN = re.compile(r"(?:^|\s)(class|struct|union)\s+([A-Za-z_]\w*)")
_ENUM_OPEN = re.compile(r"(?:^|\s)enum\s+(?:class\s+|struct\s+)?([A-Za-z_]\w*)?")
_ALIAS = re.compile(r"(?:^|\s)using\s+([A-Za-z_]\w*)\s*=")
_CALLABLE = re.compile(r"([A-Za-z_]\w*)\s*\(")
_ENUMERATOR = re.compile(r"^([A-Za-z_]\w*)")

# Words that can precede "(" without naming a member.
_NOT_A_MEMBER = {
    "explicit",
    "friend",
    "operator",
    "return",
    "sizeof",
    "static_cast",
    "reinterpret_cast",
    "const_cast",
    "decltype",
    "noexcept",
}


def normalise(name):
    """Fold the intentional root-class rename into a single name."""
    return re.sub(r"\bEspBleBluedroid\b", "EspBle", name)


def _preprocess(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    lines = [line.split("//")[0] for line in text.splitlines()]

    kept = []
    guard_depth = 0
    for line in lines:
        stripped = line.strip()
        if guard_depth:
            if stripped.startswith("#if"):
                guard_depth += 1
            elif stripped.startswith("#endif"):
                guard_depth -= 1
            continue
        if _TESTING_GUARD.match(stripped):
            guard_depth = 1
            continue
        if stripped.startswith("#"):
            continue
        kept.append(line)
    return "\n".join(kept)


def _argument_counts(text, open_index):
    """Every arity a caller can use for the parameter list starting at '('.

    A parameter with a default value makes one more callable arity, so
    `connect(address, type, timeout = 10000)` yields both 2 and 3. Comparing the
    callable arities — rather than the declared count — keeps a default argument
    added on one side from reading as an incompatible signature.
    """
    depth = 0
    total = 0
    defaults = 0
    seen = False
    for index in range(open_index, len(text)):
        char = text[index]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
            if depth == 0:
                if not seen:
                    return (0,)
                total += 1
                return tuple(range(total - defaults, total + 1))
        elif depth == 1:
            if char == ",":
                total += 1
                seen = False
            elif char == "=":
                defaults += 1
            elif not char.isspace():
                seen = True
    return None


def _skip_initializer(text, open_index):
    """Return the index after the brace initializer starting at '{'."""
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index + 1
    return len(text)


class _Scope:
    def __init__(self, name, kind, access):
        self.name = name
        self.kind = kind  # "type", "enum", or "block"
        self.access = access


def _qualify(scopes, name):
    path = [scope.name for scope in scopes if scope.kind in ("type", "enum")]
    path.append(name)
    return "::".join(path)


def _visible(scopes):
    for scope in reversed(scopes):
        if scope.kind == "block":
            return False
        if scope.kind in ("type", "enum"):
            return scope.access == "public"
    return True


def _declaration_symbols(statement, scopes):
    """Symbols declared by one ';'-terminated declaration."""
    statement = statement.strip()
    if not statement or not _visible(scopes):
        return set()
    if scopes and scopes[-1].kind == "block":
        return set()

    alias = _ALIAS.search(statement)
    if alias:
        return {_qualify(scopes, alias.group(1))}

    if _TYPE_OPEN.search(statement) and "(" not in statement:
        # Forward declaration: the type is registered when its body opens.
        return set()

    for match in _CALLABLE.finditer(statement):
        name = match.group(1)
        if name in _NOT_A_MEMBER:
            continue
        counts = _argument_counts(statement, match.end() - 1)
        if counts is None:
            continue
        return {"%s/%d" % (_qualify(scopes, name), count) for count in counts}

    # Data member or variable: take the declarator name, ignoring array bounds
    # and any initializer.
    head = statement.split("=")[0]
    head = re.sub(r"\[[^\]]*\]", "", head)
    names = re.findall(r"([A-Za-z_]\w*)", head)
    if not names:
        return set()
    return {_qualify(scopes, names[-1])}


def _enumerator_symbols(body, scopes):
    symbols = set()
    for part in body.split(","):
        match = _ENUMERATOR.match(part.strip())
        if match:
            symbols.add(_qualify(scopes, match.group(1)))
    return symbols


def extract(header_text):
    """Return the set of public symbol names declared by a root header."""
    text = _preprocess(header_text)
    symbols = set()
    scopes = []
    buffer = ""
    index = 0

    while index < len(text):
        char = text[index]

        if char == "{":
            type_open = _TYPE_OPEN.search(buffer)
            enum_open = _ENUM_OPEN.search(buffer)
            if type_open:
                name = type_open.group(2)
                access = "public" if type_open.group(1) != "class" else "private"
                # A type nested in a hidden scope is hidden too, members and all.
                if not _visible(scopes):
                    access = "private"
                else:
                    symbols.add(_qualify(scopes, name))
                scopes.append(_Scope(name, "type", access))
            elif enum_open:
                name = enum_open.group(1) or ""
                visible = _visible(scopes)
                if name and visible:
                    symbols.add(_qualify(scopes, name))
                scopes.append(_Scope(name, "enum", "public" if visible else "private"))
            elif "=" in buffer:
                # Brace initializer inside a declaration: keep collecting.
                index = _skip_initializer(text, index)
                continue
            else:
                scopes.append(_Scope(None, "block", "public"))
            buffer = ""
            index += 1
            continue

        if char == "}":
            if scopes:
                closing = scopes.pop()
                if closing.kind == "enum" and _visible(scopes + [closing]):
                    symbols.update(_enumerator_symbols(buffer, scopes + [closing]))
            buffer = ""
            index += 1
            continue

        if char == ";":
            symbols.update(_declaration_symbols(buffer, scopes))
            buffer = ""
            index += 1
            continue

        if char == ":" and _ACCESS.search(buffer.strip()):
            if scopes:
                scopes[-1].access = _ACCESS.search(buffer.strip()).group(1)
            buffer = ""
            index += 1
            continue

        buffer += char
        index += 1

    return {normalise(symbol) for symbol in symbols}
