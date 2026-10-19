"""Text-addressed adapter for the repository-owned InfiniteSession C bridge.

Only finite requests cross this boundary. A Python handle owns one live native
session; the graphics observer borrows it and never creates a second state.
"""
import ctypes as ct
from dataclasses import dataclass
from pathlib import Path
from threading import RLock

import numpy as np

from infinite_format import (MAX_ADDRESS_DIGITS, MAX_OBSERVABLES, MAX_QUERY_TERMS,
                             MAX_TEXT_BYTES, address, partner_address, validate_observable, default_observables)


@dataclass(frozen=True)
class InfiniteObservation:
    values: np.ndarray
    revision: int
    epoch: int


class InfiniteCore:
    """Owner of one InfiniteSession, with explicit finite resource budgets."""
    backend = "infinite"
    GATES = {"H": 1, "S": 1, "X": 1, "Y": 1, "Z": 1, "CX": 2, "CZ": 2, "CNOT": 2}

    def __init__(self, background="zero"):
        if background not in ("zero", "bell"):
            raise ValueError("Background must be zero or bell")
        self._lock = RLock()
        self.handle = None
        self.lib = ct.CDLL(str(Path(__file__).parent / "native/libmx_optical.so"))
        self._configure()
        self.handle = self.lib.mxi_create(int(background == "bell"))
        if not self.handle:
            self._check(-1)

    def _configure(self):
        lib = self.lib
        lib.mxi_create.argtypes = [ct.c_int]
        lib.mxi_create.restype = ct.c_void_p
        lib.mxi_destroy.argtypes = [ct.c_void_p]
        lib.mxi_destroy.restype = None
        lib.mxi_last_error.argtypes = []
        lib.mxi_last_error.restype = ct.c_char_p
        for name in ("execute", "snapshot"):
            fn = getattr(lib, "mxi_" + name)
            fn.argtypes = [ct.c_void_p] + ([ct.c_char_p] if name == "execute" else []) + [ct.POINTER(ct.c_void_p)]
            fn.restype = ct.c_int
        lib.mxi_restore.argtypes = [ct.c_void_p, ct.c_char_p]
        lib.mxi_restore.restype = ct.c_int
        lib.mxi_reset.argtypes = [ct.c_void_p, ct.c_int]
        lib.mxi_reset.restype = ct.c_int
        lib.mxi_free_string.argtypes = [ct.c_void_p]
        lib.mxi_free_string.restype = None
        lib.mxi_query.argtypes = [ct.c_void_p, ct.POINTER(ct.c_char_p), ct.c_size_t,
                                  ct.POINTER(ct.c_double), ct.POINTER(ct.c_uint64), ct.POINTER(ct.c_uint64)]
        lib.mxi_query.restype = ct.c_int
        # Filled from the shared C header below; no JSON-number conversion of
        # addresses or 64-bit revision values occurs at this interface.
        lib.mxi_get_stats.argtypes = [ct.c_void_p, ct.POINTER(_NativeStats)]
        lib.mxi_get_stats.restype = ct.c_int

    def _check(self, status):
        if status:
            text = self.lib.mxi_last_error()
            raise ValueError(text.decode("utf-8", "replace") if text else "InfiniteSession operation failed")

    def _live(self):
        if not self.handle:
            raise RuntimeError("InfiniteCore is closed")

    @staticmethod
    def _text(value):
        if type(value) is not str or "\0" in value:
            raise ValueError("Text without embedded NUL is required")
        encoded = value.encode("ascii")
        if len(encoded) > MAX_TEXT_BYTES:
            raise ValueError("Infinite request exceeds 16 MiB")
        return encoded

    def _string_result(self, name, *args):
        with self._lock:
            self._live()
            out = ct.c_void_p()
            try:
                self._check(getattr(self.lib, "mxi_" + name)(self.handle, *args, ct.byref(out)))
                if not out.value:
                    raise RuntimeError("Native bridge returned no text")
                return ct.string_at(out).decode("ascii")
            finally:
                if out.value:
                    self.lib.mxi_free_string(out)

    def execute(self, program):
        """Execute one bounded, atomic .xi request on the current session."""
        return self._string_result("execute", self._text(program))

    def apply(self, gates):
        lines = []
        total = len("begin\nend\n")
        for gate in gates:
            if len(lines) == 65536:
                raise ValueError("At most 65536 gates per request")
            if (not isinstance(gate, (tuple, list)) or not gate or type(gate[0]) is not str
                    or gate[0] not in self.GATES or len(gate) != self.GATES[gate[0]] + 1):
                raise ValueError("Only H/S/X/Y/Z/CX/CZ gates with text addresses are supported")
            operands = [address(q) for q in gate[1:]]
            if len(operands) == 2 and operands[0] == operands[1]:
                raise ValueError("Two-qubit gates require distinct addresses")
            op = "cx" if gate[0] == "CNOT" else gate[0].lower()
            line = " ".join((op, *operands))
            total += len(line) + 1
            if total > MAX_TEXT_BYTES:
                raise ValueError("Gate request exceeds 16 MiB")
            lines.append(line)
        if lines:
            self.execute("begin\n" + "\n".join(lines) + "\nend\n")

    def snapshot(self):
        return self._string_result("snapshot")

    def restore(self, snapshot):
        encoded = self._text(snapshot)
        with self._lock:
            self._live()
            self._check(self.lib.mxi_restore(self.handle, encoded))

    def reset(self, background="zero"):
        if background not in ("zero", "bell"):
            raise ValueError("Background must be zero or bell")
        with self._lock:
            self._live()
            self._check(self.lib.mxi_reset(self.handle, int(background == "bell")))

    def query_observation(self, observables):
        encoded = []
        total = 0
        for value in observables:
            if len(encoded) == MAX_OBSERVABLES:
                raise ValueError("At most 4096 observables per request")
            data = validate_observable(value).encode("ascii")
            total += len(data)
            if total > MAX_TEXT_BYTES:
                raise ValueError("Observable program exceeds 16 MiB")
            encoded.append(data)
        args = (ct.c_char_p * len(encoded))(*encoded)
        out = np.empty(len(encoded), dtype=np.float64)
        revision, epoch = ct.c_uint64(), ct.c_uint64()
        with self._lock:
            self._live()
            self._check(self.lib.mxi_query(self.handle, args, len(encoded),
                        out.ctypes.data_as(ct.POINTER(ct.c_double)), ct.byref(revision), ct.byref(epoch)))
        return InfiniteObservation(out, revision.value, epoch.value)

    def query(self, observables):
        return self.query_observation(observables).values

    def stats(self):
        value = _NativeStats()
        with self._lock:
            self._live()
            self._check(self.lib.mxi_get_stats(self.handle, ct.byref(value)))
        result = {name: int(getattr(value, name)) for name, _ in value._fields_}
        result["background"] = "bell" if result["background"] == 1 else "zero"
        return result

    @property
    def revision(self):
        return self.stats()["revision"]

    @property
    def epoch(self):
        return self.stats()["epoch"]

    def close(self):
        with self._lock:
            if self.handle:
                self.lib.mxi_destroy(self.handle)
                self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class _NativeStats(ct.Structure):
    # Contract is checked against infinite_bridge.h by the native/desktop tests.
    _fields_ = [(name, ct.c_uint64) for name in (
        "active_qubits", "components", "largest_component", "tableau_bytes",
        "address_bytes", "revision", "epoch", "background")]
