"""Portable sparse-observable validation, independent of native/GPU runtimes."""
MAX_ADDRESS_DIGITS = 4096
MAX_OBSERVABLES = 4096
MAX_QUERY_TERMS = 4096
MAX_TEXT_BYTES = 16 * 1024 * 1024


def address(value):
    """Keep addresses as decimal strings, including beyond machine integers."""
    if (type(value) is not str or not 1 <= len(value) <= MAX_ADDRESS_DIGITS
            or any(c < "0" or c > "9" for c in value)):
        raise ValueError("A qubit address requires 1..4096 ASCII decimal digits as text")
    return value.lstrip("0") or "0"


def partner_address(value):
    value = address(value)
    last = ord(value[-1]) - ord("0")
    return value[:-1] + str(last - 1 if last & 1 else last + 1)


def validate_observable(value):
    """Structural validation only; the native session evaluates the operator."""
    if type(value) is not str or len(value) > MAX_TEXT_BYTES:
        raise ValueError("A bounded sparse Pauli string is required")
    if any(c not in " \t\r\n\f\v+-:IXYZ0123456789" for c in value):
        raise ValueError("Use sparse Pauli terms such as X:0 Z:1000000000")
    terms = value.split()
    if terms and terms[0] in ("+", "-"):
        terms = terms[1:]
    if len(terms) > MAX_QUERY_TERMS:
        raise ValueError("At most 4096 terms per sparse observable")
    seen = set()
    for term in terms:
        if len(term) < 3 or term[0] not in "IXYZ" or term[1] != ":":
            raise ValueError("Use sparse Pauli terms such as X:0 Z:1000000000")
        q = address(term[2:])
        if q in seen:
            raise ValueError("A sparse observable requires distinct canonical addresses")
        seen.add(q)
    return value


def validate_snapshot(value):
    if (type(value) is not str or not value.startswith(("MXI1:", "MXI2:"))
            or not 5 < len(value) <= MAX_TEXT_BYTES or (len(value) - 5) % 2
            or any(c not in "0123456789abcdefABCDEF" for c in value[5:])):
        raise ValueError("An MXI1/MXI2 native snapshot with bounded hexadecimal text is required")
    return value


def default_observables(first="0", second=None):
    first = address(first)
    second = partner_address(first) if second is None else address(second)
    if first == second:
        raise ValueError("Two distinct addresses are required")
    # Fixed roles: never replace these operators with new stabilizer generators
    # after a gate, since that would hide the effect of the changed state.
    return (f"X:{first} X:{second}", f"Y:{first} Y:{second}",
            f"Z:{first} Z:{second}", f"X:{first}", f"Z:{first}",
            f"X:{second}", f"Z:{second}")


