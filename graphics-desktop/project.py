"""Portable .cosmic projects for the existing quantum core and graphic observer.

The JSON codec uses only Python's standard library. Loading a project validates
the whole document before constructing a new core; it never mutates an active
session, executes project code or follows external references.
"""
from copy import deepcopy
import json
import math
import os
from pathlib import Path
import struct
import tempfile
from threading import RLock

FORMAT = "cosmic-x-engine-project"
VERSION = 1
MAX_FILE_BYTES = 16 * 1024 * 1024
MAX_QUBITS = 256
MAX_GATES = 100_000
MAX_OBSERVABLES = 4096
MAX_EDITS = 100_000
MAX_OBJECTS = 1_048_576
MAX_REVISION = 9_007_199_254_740_991
GATES = {"H": 1, "S": 1, "X": 1, "Y": 1, "Z": 1, "CX": 2, "CZ": 2}


def _keys(value, expected, where):
    if not isinstance(value, dict) or set(value) != set(expected):
        raise ValueError(f"{where}: expected exactly {', '.join(expected)}")


def _integer(value, low, high, where):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{where}: integer {low}..{high} required")
    return value


def _number(value, low, high, where):
    if type(value) not in (int, float):
        raise ValueError(f"{where}: finite number required")
    try:
        valid = math.isfinite(value) and low <= value <= high
    except OverflowError:
        valid = False
    if not valid:
        raise ValueError(f"{where}: finite number {low}..{high} required")
    return float(value)


def _vector(value, where):
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError(f"{where}: three coordinates required")
    return [_number(n, -1_000_000, 1_000_000, where) for n in value]


def _float32(value):
    return struct.unpack("!f", struct.pack("!f", value))[0]


def validate_camera(camera):
    if not isinstance(camera, dict):
        raise ValueError("graphics.camera: object required")
    mode = camera.get("mode")
    if mode == "orbit":
        _keys(camera, ("mode", "overview", "yaw", "pitch", "zoom"), "graphics.camera")
        if type(camera["overview"]) is not bool:
            raise ValueError("graphics.camera.overview: boolean required")
        _number(camera["yaw"], -1_000_000, 1_000_000, "graphics.camera.yaw")
        _number(camera["pitch"], .04, 1.52, "graphics.camera.pitch")
        _number(camera["zoom"], .12, 6, "graphics.camera.zoom")
    elif mode == "lookAt":
        _keys(camera, ("mode", "eye", "target", "near", "far", "fov"), "graphics.camera")
        eye, target = _vector(camera["eye"], "camera.eye"), _vector(camera["target"], "camera.target")
        if sum((a - b) ** 2 for a, b in zip(eye, target)) < 1e-8:
            raise ValueError("graphics.camera: distinct eye and target required")
        if sum((_float32(a) - _float32(b)) ** 2 for a, b in zip(eye, target)) < 1e-8:
            raise ValueError("graphics.camera: eye and target collapse at GLES float precision")
        near = _number(camera["near"], .001, 1_000_000, "graphics.camera.near")
        far = _number(camera["far"], .001, 1_000_000, "graphics.camera.far")
        if near >= far:
            raise ValueError("graphics.camera: near must be smaller than far")
        if _float32(near) >= _float32(far):
            raise ValueError("graphics.camera: projection planes collapse at GLES float precision")
        _number(camera["fov"], 1, 170, "graphics.camera.fov")
    else:
        raise ValueError("graphics.camera.mode: orbit or lookAt required")
    return deepcopy(camera)


def validate_project(value):
    """Return an independent validated document, without native or GL work."""
    _keys(value, ("format", "version", "name", "core", "graphics"), "project")
    if value["format"] != FORMAT or type(value["version"]) is not int or value["version"] != VERSION:
        raise ValueError("Unsupported .cosmic format/version")
    name = value["name"]
    if not isinstance(name, str) or not 1 <= len(name) <= 128 or any(ord(c) < 32 or 0xD800 <= ord(c) <= 0xDFFF for c in name):
        raise ValueError("project.name: 1..128 characters without control characters required")
    core = value["core"]
    _keys(core, ("backend", "initialState", "qubits", "revision", "gates"), "core")
    if core["backend"] != "compact-clifford" or core["initialState"] != "zero":
        raise ValueError("This project version supports compact-clifford replay from zero")
    n = _integer(core["qubits"], 1, MAX_QUBITS, "core.qubits")
    _integer(core["revision"], 0, MAX_REVISION, "core.revision")
    gates = core["gates"]
    if not isinstance(gates, list) or len(gates) > MAX_GATES:
        raise ValueError(f"core.gates: at most {MAX_GATES} gates required")
    for gate in gates:
        if not isinstance(gate, list) or not gate or type(gate[0]) is not str or gate[0] not in GATES:
            raise ValueError("core.gates: H/S/X/Y/Z/CX/CZ required")
        if len(gate) != GATES[gate[0]] + 1:
            raise ValueError("core.gates: incorrect gate arity")
        for q in gate[1:]:
            _integer(q, 0, n - 1, "core.gates operand")
        if len(gate) == 3 and gate[1] == gate[2]:
            raise ValueError("core.gates: two distinct operands required")
    graphics = value["graphics"]
    _keys(graphics, ("program", "objects", "seed", "coupling", "observables", "edits", "camera", "time", "selectedId", "resolutionLimit"), "graphics")
    if graphics["program"] != "opaque-grid-v1":
        raise ValueError("Unsupported graphic program")
    count = _integer(graphics["objects"], 1, MAX_OBJECTS, "graphics.objects")
    _integer(graphics["seed"], 0, 4_294_967_295, "graphics.seed")
    _number(graphics["coupling"], -.25, .25, "graphics.coupling")
    observables = graphics["observables"]
    if not isinstance(observables, list) or not 1 <= len(observables) <= MAX_OBSERVABLES:
        raise ValueError(f"graphics.observables: 1..{MAX_OBSERVABLES} observables required")
    for pauli in observables:
        if not isinstance(pauli, str):
            raise ValueError("graphics.observables: Pauli strings required")
        body = pauli[1:] if pauli[:1] in ("+", "-") else pauli
        if len(body) != n or any(c not in "IXYZ" for c in body):
            raise ValueError("graphics.observables: signed IXYZ strings in q0-first order required")
    edits = graphics["edits"]
    if not isinstance(edits, list) or len(edits) > MAX_EDITS:
        raise ValueError(f"graphics.edits: at most {MAX_EDITS} edits required")
    previous = -1
    for edit in edits:
        if not isinstance(edit, list) or len(edit) != 2:
            raise ValueError("graphics.edits: [id, state] pairs required")
        object_id = _integer(edit[0], 0, MAX_OBJECTS - 1, "graphics.edits id")
        _integer(edit[1], 1, 7, "graphics.edits state")
        if object_id <= previous:
            raise ValueError("graphics.edits: IDs must be unique and sorted")
        previous = object_id
    validate_camera(graphics["camera"])
    _number(graphics["time"], -1_000_000, 1_000_000, "graphics.time")
    _integer(graphics["selectedId"], -1, count - 1, "graphics.selectedId")
    _integer(graphics["resolutionLimit"], 64, 4096, "graphics.resolutionLimit")
    return deepcopy(value)


def _unique_object(pairs):
    value = {}
    for key, entry in pairs:
        if key in value:
            raise ValueError(f"Duplicate JSON key: {key}")
        value[key] = entry
    return value


def _not_finite(value):
    raise ValueError(f"Nonfinite JSON value: {value}")


def decode_project(data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    if not isinstance(data, bytes) or len(data) > MAX_FILE_BYTES:
        raise ValueError("Project exceeds the 16 MiB limit")
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_unique_object, parse_constant=_not_finite)
        return validate_project(value)
    except (UnicodeError, RecursionError, OverflowError, json.JSONDecodeError) as error:
        raise ValueError(f"Invalid UTF-8 project JSON: {error}") from error


def read_project(path):
    with Path(path).open("rb") as stream:
        data = stream.read(MAX_FILE_BYTES + 1)
    return decode_project(data)


def encode_project(value):
    value = validate_project(value)
    data = (json.dumps(value, ensure_ascii=False, allow_nan=False, indent=2) + "\n").encode("utf-8")
    if len(data) > MAX_FILE_BYTES:
        raise ValueError("Project exceeds the 16 MiB limit")
    return data


def write_project(path, value):
    """Atomic replacement: malformed/oversized projects cannot damage a save."""
    data = encode_project(value)
    destination = Path(path)
    directory = destination.parent
    descriptor, temporary = tempfile.mkstemp(prefix=".cosmic-", suffix=".tmp", dir=directory)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return destination


class ProjectSession:
    """One owning quantum core and one observer borrowing exactly that core.

    Project-facing mutations preserve the v1 limits before changing state.
    Callers using the general ``core``/``scene`` directly retain responsibility
    for synchronization and the broader native interface; save() revalidates
    their result before touching an existing file.
    """
    def __init__(self, document):
        document = validate_project(document)
        self._mutation_lock = RLock()
        from core_runtime import Core
        from engine import QuantumScene, build_native
        build_native()
        config, graphics = document["core"], document["graphics"]
        self.core = Core(config["qubits"])
        try:
            self.core.restore(config["gates"], config["revision"])
            self.scene = QuantumScene.from_core(self.core, count=graphics["objects"], seed=graphics["seed"],
                                                coupling=graphics["coupling"], observables=graphics["observables"])
            self.scene.restore_edits(graphics["edits"])
        except Exception:
            self.core.close()
            raise
        self.name = document["name"]
        self.camera = deepcopy(graphics["camera"])
        self.time = graphics["time"]
        self.selected_id = graphics["selectedId"]
        self.resolution_limit = graphics["resolutionLimit"]

    @classmethod
    def open(cls, path):
        return cls(read_project(path))

    @classmethod
    def from_scene(cls, scene, *, name="Mundo X", camera=None, time=0., selected_id=-1, resolution_limit=640):
        """Borrow an existing scene/core; exporting does not clone/reset it."""
        instance = cls.__new__(cls)
        instance._mutation_lock = RLock()
        instance.core, instance.scene = scene._core, scene
        instance.name = name
        instance.camera = deepcopy(camera if camera is not None else
                                   {"mode": "orbit", "overview": True, "yaw": .67, "pitch": .65, "zoom": 1.})
        instance.time, instance.selected_id, instance.resolution_limit = time, selected_id, resolution_limit
        instance._borrowed = True
        instance.to_dict()  # Validate the complete description without mutation.
        return instance

    @classmethod
    def create(cls, *, name="Mundo X", objects=1_000_000, qubits=20, seed=2026, coupling=.25):
        _integer(objects, 1, MAX_OBJECTS, "graphics.objects")
        _integer(qubits, 1, MAX_QUBITS, "core.qubits")
        _integer(seed, 0, 4_294_967_295, "graphics.seed")
        _number(coupling, -.25, .25, "graphics.coupling")
        from engine import QuantumScene, build_native
        build_native()
        scene = QuantumScene(objects, qubits, seed, coupling)
        try:
            instance = cls.from_scene(scene, name=name)
        except Exception:
            scene.close()
            raise
        instance._borrowed = False
        return instance

    @property
    def time(self):
        return self._time

    @time.setter
    def time(self, value):
        _number(value, -1_000_000, 1_000_000, "graphics.time")
        with self._mutation_lock:
            self._time = value

    def set_time(self, value):
        """Set a portable animation time without changing the core."""
        self.time = value

    def apply(self, gates):
        """Validate a whole gate batch and v1 history limits before mutation.

        The standalone Core remains unrestricted. CNOT retains the existing
        CLI alias and is stored canonically as CX in a portable project.
        """
        try:
            iterator = iter(gates)
        except TypeError as error:
            raise ValueError("core.gates: a gate sequence is required") from error
        batch = []
        for gate in iterator:
            if len(batch) == MAX_GATES:
                raise ValueError(f"core.gates: at most {MAX_GATES} gates required")
            if not isinstance(gate, (list, tuple)) or not gate or type(gate[0]) is not str:
                raise ValueError("core.gates: H/S/X/Y/Z/CX/CZ required")
            operation = "CX" if gate[0] == "CNOT" else gate[0]
            if operation not in GATES or len(gate) != GATES[operation] + 1:
                raise ValueError("core.gates: unsupported gate or incorrect arity")
            for operand in gate[1:]:
                _integer(operand, 0, self.core.n - 1, "core.gates operand")
            if len(gate) == 3 and gate[1] == gate[2]:
                raise ValueError("core.gates: two distinct operands required")
            batch.append((operation, *gate[1:]))
        with self._mutation_lock:
            if not batch:
                return
            current = self.core.snapshot()
            if current["revision"] >= MAX_REVISION:
                raise ValueError("core.revision: maximum portable revision reached")
            if len(current["gates"]) + len(batch) > MAX_GATES:
                raise ValueError(f"core.gates: the project would exceed {MAX_GATES} gates")
            # Shared native validation remains the final transactional gate.
            self.core.apply(batch)

    def edit(self, object_id, value):
        self.edit_many(((object_id, value),))

    def edit_many(self, updates):
        """Apply an edit batch through the scene's atomic 100k-edit guard."""
        with self._mutation_lock:
            self.scene.edit_many(updates)

    def to_dict(self):
        with self._mutation_lock:
            if self.core.backend != "compact":
                raise ValueError(".cosmic v1 requires the compact Clifford backend")
            state = self.core.snapshot()
            document = {
                "format": FORMAT, "version": VERSION, "name": self.name,
                "core": {"backend": "compact-clifford", "initialState": "zero", "qubits": self.core.n,
                         "revision": state["revision"], "gates": [list(g) for g in state["gates"]]},
                "graphics": {"program": "opaque-grid-v1", "objects": self.scene.count, "seed": self.scene.seed,
                             "coupling": self.scene.coupling, "observables": list(self.scene.observables),
                             "edits": [list(edit) for edit in self.scene.edits], "camera": deepcopy(self.camera),
                             "time": self.time, "selectedId": self.selected_id, "resolutionLimit": self.resolution_limit}}
            return validate_project(document)

    def save(self, path):
        return write_project(path, self.to_dict())

    def camera_for(self, width, height):
        _integer(width, 1, 4096, "camera width")
        _integer(height, 1, 4096, "camera height")
        from renderer import camera
        config = validate_camera(self.camera)
        if config["mode"] == "lookAt":
            return camera(config["eye"], config["target"], width, height,
                          near=config["near"], far=config["far"], fov=config["fov"])
        # Same orbit law as the Android observer; aspect changes the framing.
        aspect = width / height
        extent = math.ceil(math.sqrt(self.scene.count)) * 2.2
        target = [0., 0., 0.] if config["overview"] else [0., 1., 0.]
        distance = (extent * max(1.65, 1.4 / aspect) if config["overview"] else 25.) * config["zoom"]
        yaw, pitch = config["yaw"], config["pitch"]
        eye = [distance * math.cos(pitch) * math.sin(yaw), target[1] + distance * math.sin(pitch),
               distance * math.cos(pitch) * math.cos(yaw)]
        near = max(.05, extent * .01) if config["overview"] else .1
        far = max(10000., distance + extent * 4) if config["overview"] else 5000.
        return camera(eye, target, width, height, near=near, far=far, fov=48.)

    def close(self):
        with self._mutation_lock:
            if not getattr(self, "_borrowed", False):
                self.scene.close()
                self.core.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
