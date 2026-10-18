#!/usr/bin/env python3
"""Compare the real Java renderer against its git baseline on host EGL/GLES3.

Python standard library + system EGL + a JDK (or ECJ and Java) + Android SDK jar.
No package is added to the Android application's runtime. See README.md.
"""
import argparse
import ctypes as C
import ctypes.util
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / "app/src/main/java/com/mundox/lab"
P = C.c_void_p
I = C.c_int
U = C.c_uint
F = C.c_float


def run(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, **kwargs)


def compile_exports(work, args):
    legacy = run(["git", "show", args.baseline_ref + ":app/src/main/java/com/mundox/lab/ObjectSceneRenderer.java"],
                 cwd=ROOT, capture_output=True, text=True).stdout
    reference = work / "ReferenceObjectSceneRenderer.java"
    reference.write_text(legacy.replace("ObjectSceneRenderer", "ReferenceObjectSceneRenderer"))
    classes = work / "classes"
    classes.mkdir()
    sources = [JAVA / (name + ".java") for name in
               ("ObjectSceneModel", "ObjectDrawPlan", "ObjectSceneRenderer")]
    sources += [reference, Path(__file__).with_name("HostGraphicsExport.java")]
    if args.ecj:
        compiler = [args.java, "-jar", args.ecj, "-8", "-proc:none", "-warn:none"]
    else:
        compiler = [args.javac, "-source", "8", "-target", "8", "-proc:none"]
    run(compiler + ["-cp", args.android_jar, "-d", classes] + sources)
    command = [args.java, "-cp", str(classes) + os.pathsep + args.android_jar,
               "com.mundox.lab.HostGraphicsExport"]
    run(command + ["sources", work])
    return command


class GL:
    def __init__(self):
        library = ctypes.util.find_library("EGL")
        if not library:
            raise RuntimeError("System EGL library not found")
        self.egl = C.CDLL(library)
        self.functions = {}
        self.proc = self.efunc("eglGetProcAddress", P, C.c_char_p)
        address = self.proc(b"eglGetPlatformDisplayEXT")
        if not address:
            raise RuntimeError("EGL_EXT_platform_base required for headless host validation")
        platform = C.CFUNCTYPE(P, U, P, C.POINTER(I))(address)
        self.display = platform(0x31DD, None, None)  # EGL_PLATFORM_SURFACELESS_MESA
        major, minor = I(), I()
        assert self.efunc("eglInitialize", U, P, C.POINTER(I), C.POINTER(I))(
            self.display, C.byref(major), C.byref(minor))
        assert self.efunc("eglBindAPI", U, U)(0x30A0)
        attrs = (I * 13)(0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8,
                         0x3033, 1, 0x3040, 0x40, 0x3038)
        config, count = P(), I()
        assert self.efunc("eglChooseConfig", U, P, C.POINTER(I), C.POINTER(P), I, C.POINTER(I))(
            self.display, attrs, C.byref(config), 1, C.byref(count)) and count.value
        self.surface = self.efunc("eglCreatePbufferSurface", P, P, P, C.POINTER(I))(
            self.display, config, (I * 5)(0x3057, 1, 0x3056, 1, 0x3038))
        self.context = self.efunc("eglCreateContext", P, P, P, P, C.POINTER(I))(
            self.display, config, None, (I * 3)(0x3098, 3, 0x3038))
        assert self.surface and self.context
        assert self.efunc("eglMakeCurrent", U, P, P, P, P)(
            self.display, self.surface, self.surface, self.context)

    def efunc(self, name, result, *types):
        fn = getattr(self.egl, name)
        fn.restype, fn.argtypes = result, list(types)
        return fn

    def fn(self, name, result, *types):
        if name not in self.functions:
            address = self.proc(name.encode())
            if not address:
                raise RuntimeError("Missing GL function: " + name)
            self.functions[name] = C.CFUNCTYPE(result, *types)(address)
        return self.functions[name]

    def check(self):
        error = self.fn("glGetError", U)()
        if error:
            raise RuntimeError("GL error 0x%04x" % error)

    def shader(self, kind, text):
        obj = self.fn("glCreateShader", U, U)(kind)
        data = C.c_char_p(text.encode())
        self.fn("glShaderSource", None, U, I, C.POINTER(C.c_char_p), C.POINTER(I))(
            obj, 1, C.byref(data), None)
        self.fn("glCompileShader", None, U)(obj)
        okay = I()
        self.fn("glGetShaderiv", None, U, U, C.POINTER(I))(obj, 0x8B81, C.byref(okay))
        if not okay.value:
            log = C.create_string_buffer(16384)
            self.fn("glGetShaderInfoLog", None, U, I, C.POINTER(I), C.c_char_p)(obj, len(log), None, log)
            raise RuntimeError(log.value.decode())
        return obj

    def program(self, vertex, fragment):
        obj = self.fn("glCreateProgram", U)()
        for kind, source in ((0x8B31, vertex), (0x8B30, fragment)):
            self.fn("glAttachShader", None, U, U)(obj, self.shader(kind, source))
        varyings = (C.c_char_p * 2)(b"tfTransform", b"tfId")
        self.fn("glTransformFeedbackVaryings", None, U, I, C.POINTER(C.c_char_p), U)(
            obj, 2, varyings, 0x8C8C)
        self.fn("glLinkProgram", None, U)(obj)
        okay = I()
        self.fn("glGetProgramiv", None, U, U, C.POINTER(I))(obj, 0x8B82, C.byref(okay))
        if not okay.value:
            log = C.create_string_buffer(16384)
            self.fn("glGetProgramInfoLog", None, U, I, C.POINTER(I), C.c_char_p)(obj, len(log), None, log)
            raise RuntimeError(log.value.decode())
        return obj

    def new(self, noun):
        value = U()
        self.fn("glGen" + noun, None, I, C.POINTER(U))(1, C.byref(value))
        return value.value

    def buffer(self, target, data):
        value = self.new("Buffers")
        self.fn("glBindBuffer", None, U, U)(target, value)
        content = C.create_string_buffer(data)
        self.fn("glBufferData", None, U, C.c_ssize_t, P, U)(target, len(data), content, 0x88E4)
        return value


def normalize(v):
    length = math.sqrt(sum(x * x for x in v))
    return [x / length for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def camera(side, close=False):
    distance = max(12, side * (0.35 if close else 3.1))
    pitch, yaw = 0.8, 0.65
    eye = [math.sin(yaw) * math.cos(pitch) * distance,
           math.sin(pitch) * distance + 0.5, math.cos(yaw) * math.cos(pitch) * distance]
    forward = normalize([-eye[0], 0.5 - eye[1], -eye[2]])
    right = normalize(cross(forward, [0, 1, 0]))
    up = cross(right, forward)
    view = [right + [-dot(right, eye)], up + [-dot(up, eye)],
            [-x for x in forward] + [dot(forward, eye)], [0, 0, 0, 1]]
    near, far = max(0.1, distance * 0.01), max(100, distance + side * 4 + 20)
    cot = 1 / math.tan(math.radians(25))
    projection = [[cot, 0, 0, 0], [0, cot, 0, 0],
                  [0, 0, (far + near) / (near - far), 2 * far * near / (near - far)], [0, 0, -1, 0]]
    vp = [sum(projection[row][k] * view[k][col] for k in range(4))
          for col in range(4) for row in range(4)]
    vp = list(struct.unpack("<16f", struct.pack("<16f", *vp)))
    return vp, eye, distance + side * 2.2


class Harness:
    SIZE = 128

    def __init__(self, work):
        self.gl = GL()
        g = self.gl.fn
        self.programs, self.vaos = {}, {}
        self.uniforms = {}
        self.explicit = self.gl.new("Buffers")
        self.edits = self.gl.new("Textures")
        for prefix in ("legacy", "current"):
            self.programs[prefix] = self.gl.program(
                (work / (prefix + ".vert")).read_text(), (work / (prefix + ".frag")).read_text())
            self.vaos[prefix] = self.gl.new("VertexArrays")
            g("glBindVertexArray", None, U)(self.vaos[prefix])
            self.gl.buffer(0x8892, (work / (prefix + ".mesh")).read_bytes())
            for attribute, offset in ((0, 0), (1, 12)):
                g("glEnableVertexAttribArray", None, U)(attribute)
                g("glVertexAttribPointer", None, U, I, U, C.c_ubyte, I, P)(attribute, 3, 0x1406, 0, 24, P(offset))
            g("glVertexAttribDivisor", None, U, U)(2, 1)
            if prefix == "current":
                self.gl.buffer(0x8893, (work / "current.indices").read_bytes())
        # Derive, and compare byte for byte, the actual Java methods' triangle streams.
        old = (work / "legacy.mesh").read_bytes()
        new = (work / "current.mesh").read_bytes()
        indices = struct.unpack("<36H", (work / "current.indices").read_bytes())
        expanded = b"".join(new[i * 24:(i + 1) * 24] for i in indices)
        assert old == expanded and len(new) == 24 * 24 and len(old) == 36 * 24
        self.mesh = {"legacy_vertices": 36, "indexed_vertices": 24, "indices": 36,
                     "triangle_stream_byte_identical": True}
        self.feedback = self.gl.new("TransformFeedbacks")
        self.feedback_buffer = self.gl.new("Buffers")
        self.fbo = self.gl.new("Framebuffers")
        g("glBindFramebuffer", None, U, U)(0x8D40, self.fbo)
        color = self.gl.new("Textures")
        g("glBindTexture", None, U, U)(0x0DE1, color)
        for param in (0x2801, 0x2800):
            g("glTexParameteri", None, U, U, I)(0x0DE1, param, 0x2600)
        g("glTexImage2D", None, U, I, I, I, I, I, U, U, P)(0x0DE1, 0, 0x8058,
            self.SIZE, self.SIZE, 0, 0x1908, 0x1401, None)
        g("glFramebufferTexture2D", None, U, U, U, U, I)(0x8D40, 0x8CE0, 0x0DE1, color, 0)
        depth = self.gl.new("Renderbuffers")
        g("glBindRenderbuffer", None, U, U)(0x8D41, depth)
        g("glRenderbufferStorage", None, U, U, I, I)(0x8D41, 0x81A6, self.SIZE, self.SIZE)
        g("glFramebufferRenderbuffer", None, U, U, U, U)(0x8D40, 0x8D00, 0x8D41, depth)
        assert g("glCheckFramebufferStatus", U, U)(0x8D40) == 0x8CD5
        g("glViewport", None, I, I, I, I)(0, 0, self.SIZE, self.SIZE)
        for cap in (0x0B71, 0x0B44):
            g("glEnable", None, U)(cap)
        for cap in (0x0BE2, 0x0C11, 0x0BD0):
            g("glDisable", None, U)(cap)
        g("glDepthFunc", None, U)(0x0201)
        g("glCullFace", None, U)(0x0405)
        self.gl.check()

    def uniform(self, name):
        key = (self.program, name)
        if key not in self.uniforms:
            self.uniforms[key] = self.gl.fn("glGetUniformLocation", I, U, C.c_char_p)(self.program, name.encode())
        return self.uniforms[key]

    def i(self, name, value):
        self.gl.fn("glUniform1i", None, I, I)(self.uniform(name), value)

    def configure(self, prefix, case, vp, eye, fog, explicit):
        g = self.gl.fn
        self.program = self.programs[prefix]
        g("glUseProgram", None, U)(self.program)
        g("glBindVertexArray", None, U)(self.vaos[prefix])
        g("glUniformMatrix4fv", None, I, I, C.c_ubyte, C.POINTER(F))(
            self.uniform("uVp"), 1, 0, (F * 16)(*vp))
        g("glUniform3f", None, I, F, F, F)(self.uniform("uEye"), *eye)
        for name, value in (("uTime", case["seconds"]), ("uFogDistance", fog)):
            g("glUniform1f", None, I, F)(self.uniform(name), value)
        for name, value in (("uSide", math.ceil(math.sqrt(case["count"]))),
                            ("uProcedural", int(not explicit)), ("uSelected", case["selected"]),
                            ("uEditCount", len(case["edits"])), ("uEdits", 0), ("uBaseId", 0)):
            self.i(name, value)
        seed = case["seed"]
        g("glUniform4ui", None, I, U, U, U, U)(self.uniform("uSeedBytes"),
            seed & 255, (seed >> 8) & 255, (seed >> 16) & 255, (seed >> 24) & 255)
        g("glActiveTexture", None, U)(0x84C0)
        g("glBindTexture", None, U, U)(0x0DE1, self.edits)
        for param, value in ((0x2801, 0x2600), (0x2800, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F)):
            g("glTexParameteri", None, U, U, I)(0x0DE1, param, value)
        edits = case["edits"] or [[-1, 0]]
        values = (I * (len(edits) * 2))(*(v for item in sorted(edits) for v in item))
        g("glTexImage2D", None, U, I, I, I, I, I, U, U, P)(
            0x0DE1, 0, 0x823B, len(edits), 1, 0, 0x8228, 0x1404, values)
        g("glEnableVertexAttribArray" if explicit else "glDisableVertexAttribArray", None, U)(2)

    def draws(self, batches, indexed, explicit, points=False):
        g = self.gl.fn
        for base, count in batches:
            self.i("uBaseId", base)
            if explicit:
                g("glBindBuffer", None, U, U)(0x8892, self.explicit)
                g("glVertexAttribPointer", None, U, I, U, C.c_ubyte, I, P)(2, 4, 0x1406, 0, 16, P(base * 16))
            if points:
                g("glDrawArraysInstanced", None, U, I, I, I)(0, 0, 1, count)
            elif indexed:
                g("glDrawElementsInstanced", None, U, I, U, P, I)(4, 36, 0x1403, None, count)
            else:
                g("glDrawArraysInstanced", None, U, I, I, I)(4, 0, 36, count)

    def image(self, case, vp, eye, fog, route, plan):
        g = self.gl.fn
        prefix = "legacy" if route == "legacy" else "current"
        explicit = route == "explicit_culled"
        self.configure(prefix, case, vp, eye, fog, explicit)
        # One-object warmup excludes first-use shader setup from the recorded draw.
        self.draws([[0, 1]], prefix != "legacy", explicit)
        g("glFinish", None)()
        g("glClearColor", None, F, F, F, F)(0.067, 0.086, 0.075, 1)
        g("glClear", None, U)(0x4000 | 0x0100)
        g("glFinish", None)()
        start = time.perf_counter()
        self.draws(plan["batches"], prefix != "legacy", explicit)
        g("glFinish", None)()
        elapsed = (time.perf_counter() - start) * 1000
        pixels = (C.c_ubyte * (self.SIZE * self.SIZE * 4))()
        g("glReadPixels", None, I, I, I, I, U, U, P)(
            0, 0, self.SIZE, self.SIZE, 0x1908, 0x1401, pixels)
        self.gl.check()
        raw = bytes(pixels)
        coverage = sum(raw[i:i + 3] != bytes((17, 22, 19)) for i in range(0, len(raw), 4))
        return raw, {"route": route, "submitted_instances": plan["submitted"],
                     "culled_instances": plan["culled"], "draw_calls": len(plan["batches"]),
                     "object_pixels": coverage, "sha256": hashlib.sha256(raw).hexdigest(),
                     "host_glfinish_wall_ms": round(elapsed, 3)}

    def feedback_check(self, case, vp, eye, fog, plan, expected):
        g = self.gl.fn
        reports = []
        for explicit in (False, True):
            self.configure("current", case, vp, eye, fog, explicit)
            g("glBindTransformFeedback", None, U, U)(0x8E22, self.feedback)
            g("glBindBuffer", None, U, U)(0x8C8E, self.feedback_buffer)
            size = case["count"] * 20
            g("glBufferData", None, U, C.c_ssize_t, P, U)(0x8C8E, size, None, 0x88E9)
            g("glBindBufferBase", None, U, U, U)(0x8C8E, 0, self.feedback_buffer)
            g("glEnable", None, U)(0x8C89)
            g("glBeginTransformFeedback", None, U)(0)
            self.draws(plan["batches"], True, explicit, points=True)
            g("glEndTransformFeedback", None)()
            g("glFinish", None)()
            g("glDisable", None, U)(0x8C89)
            self.gl.check()
            pointer = g("glMapBufferRange", P, U, C.c_ssize_t, C.c_ssize_t, U)(0x8C8E, 0, size, 1)
            assert pointer
            actual = C.string_at(pointer, size)
            assert g("glUnmapBuffer", C.c_ubyte, U)(0x8C8E)
            transforms_bad, ids_bad = 0, 0
            first_errors = []
            for object_id in range(case["count"]):
                offset = object_id * 20
                bad_transform = actual[offset:offset + 16] != expected[object_id * 16:(object_id + 1) * 16]
                actual_id = struct.unpack_from("<i", actual, offset + 16)[0]
                transforms_bad += bad_transform
                ids_bad += actual_id != object_id
                if (bad_transform or actual_id != object_id) and len(first_errors) < 4:
                    first_errors.append({"expected_id": object_id, "actual_id": actual_id,
                        "actual": list(struct.unpack_from("<4f", actual, offset)),
                        "expected": list(struct.unpack_from("<4f", expected, object_id * 16))})
            reports.append({"route": "explicit_offset" if explicit else "procedural",
                "checked_ids": case["count"], "last_id": case["count"] - 1,
                "batch_count": len(plan["batches"]), "transform_byte_mismatches": transforms_bad,
                "id_mismatches": ids_bad, "first_errors": first_errors})
        return reports

    def case(self, case, work, command):
        side = math.ceil(math.sqrt(case["count"]))
        vp, eye, fog = camera(side, case.get("close", False))
        if case.get("outside"):
            # Translate the complete clip volume far to one side: intentionally empty.
            for column in range(4):
                vp[column * 4] += 100 * vp[column * 4 + 3]
            vp = list(struct.unpack("<16f", struct.pack("<16f", *vp)))
        edits = ",".join("%d:%d" % tuple(item) for item in case["edits"]) or "-"
        run(command + ["case", work, str(case["count"]), str(case["seed"]),
                       str(case["seconds"]), edits] + [str(v) for v in vp])
        plans = json.loads((work / "plan.json").read_text())
        for name, plan in plans.items():
            previous_end = 0
            for base, count in plan["batches"]:
                assert base >= previous_end and 0 < count <= 16384 and base + count <= case["count"]
                if name == "unculled":
                    assert base == previous_end
                previous_end = base + count
            assert sum(n for _, n in plan["batches"]) == plan["submitted"]
            assert plan["submitted"] + plan["culled"] == case["count"]
            if name == "unculled":
                assert previous_end == case["count"] and plan["culled"] == 0
        expected = (work / "transforms.f32").read_bytes()
        g = self.gl.fn
        g("glBindBuffer", None, U, U)(0x8892, self.explicit)
        data = C.create_string_buffer(expected)
        g("glBufferData", None, U, C.c_ssize_t, P, U)(0x8892, len(expected), data, 0x88E4)
        legacy = {"batches": [[0, case["count"]]], "submitted": case["count"], "culled": 0}
        reference, routes = None, []
        for route, plan in (("legacy", legacy), ("indexed_batched", plans["unculled"]),
                            ("procedural_culled", plans["culled"]), ("explicit_culled", plans["culled"])):
            raw, report = self.image(case, vp, eye, fog, route, plan)
            if reference is None:
                reference = raw
            report["different_pixels"] = sum(raw[i:i + 4] != reference[i:i + 4] for i in range(0, len(raw), 4))
            report["max_channel_delta"] = max(abs(a - b) for a, b in zip(raw, reference))
            routes.append(report)
        feedback = self.feedback_check(case, vp, eye, fog, plans["unculled"], expected) if case.get("feedback", True) else []
        report = dict(case, routes=routes, transform_feedback=feedback,
                      planner={name: {k: v for k, v in plan.items() if k != "batches"} for name, plan in plans.items()})
        report["passed"] = (all(r["different_pixels"] == 0
            and ((r["object_pixels"] == 0) if case.get("outside") else (r["object_pixels"] > 0)) for r in routes)
            and all(r["id_mismatches"] == 0 and r["transform_byte_mismatches"] == 0 for r in feedback)
            and (not case.get("close") or plans["culled"]["culled"] > 0)
            and (not case.get("outside") or plans["culled"]["submitted"] == 0))
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--android-jar", required=True, help="Android SDK platform android.jar (API 35 recommended)")
    parser.add_argument("--ecj", help="Optional Eclipse Java compiler jar when javac is unavailable")
    parser.add_argument("--java", default="java")
    parser.add_argument("--javac", default="javac")
    parser.add_argument("--baseline-ref", default="ebd53fc", help="Unoptimized renderer git revision")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--quick", action="store_true", help="Run two small smoke cases; omit million-object proof")
    args = parser.parse_args()
    cases = [
        {"name": "1024_center", "count": 1024, "seed": 2026, "seconds": 0, "selected": 528, "edits": [[527, 7], [528, 6]]},
        {"name": "1024_outside", "count": 1024, "seed": 2026, "seconds": 7.5, "selected": 528, "edits": [[527, 7], [528, 6]], "outside": True},
        {"name": "65535_negative", "count": 65535, "seed": -1, "seconds": 2.75, "selected": 16384, "edits": [[16383, 7], [16384, 6], [65534, 4]]},
        {"name": "65536_boundary", "count": 65536, "seed": 2026, "seconds": 0, "selected": 32768, "edits": [[32767, 7], [32768, 6]]},
        {"name": "65537_tail", "count": 65537, "seed": 2147483647, "seconds": 19.5, "selected": 65536, "edits": [[65535, 7], [65536, 6]]},
        {"name": "100003_rotated_close", "count": 100003, "seed": -2147483648, "seconds": 9.25, "selected": 49152, "edits": [[49151, 7], [49152, 6], [50243, 7], [50244, 6]], "close": True},
        {"name": "1048576_center", "count": 1048576, "seed": 2026, "seconds": 0, "selected": 524288, "edits": [[524287, 7], [524288, 6], [524799, 7], [524800, 6], [1048575, 6]]},
        {"name": "1048576_rotated_close", "count": 1048576, "seed": -1, "seconds": 13.5, "selected": 524288, "edits": [[524287, 7], [524288, 6], [524799, 7], [524800, 6]], "close": True, "feedback": False},
    ]
    if args.quick:
        cases = [cases[0], dict(cases[0], name="1024_close", close=True, seconds=5.75)]
    with tempfile.TemporaryDirectory(prefix="mundox-graphics-") as temporary:
        work = Path(temporary)
        command = compile_exports(work, args)
        harness = Harness(work)
        g = harness.gl.fn
        report = {"scope": "Host EGL/GLES3; actual Java shader/mesh/model/planner methods; 128x128 RGBA8 + depth24; exact pixel and transform comparisons",
                  "timing_scope": "Single validation draw: host wall time through glFinish including Python draw submissions; excludes uploads/readback; not phone FPS or isolated GPU time",
                  "baseline_commit": run(["git", "rev-parse", args.baseline_ref], cwd=ROOT, capture_output=True, text=True).stdout.strip(),
                  "renderer": g("glGetString", C.c_char_p, U)(0x1F01).decode(),
                  "gl_version": g("glGetString", C.c_char_p, U)(0x1F02).decode(),
                  "mesh": harness.mesh,
                  "source_sha256": {name: hashlib.sha256((JAVA / name).read_bytes()).hexdigest()
                      for name in ("ObjectSceneModel.java", "ObjectDrawPlan.java", "ObjectSceneRenderer.java")},
                  "cases": []}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        for case in cases:
            print("Checking " + case["name"], flush=True)
            result = harness.case(case, work, command)
            report["cases"].append(result)
            print(json.dumps({"name": result["name"], "passed": result["passed"],
                              "submitted": result["planner"]["culled"]["submitted"],
                              "max_pixel_delta": max(r["max_channel_delta"] for r in result["routes"])}), flush=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n")
        report["passed"] = all(case["passed"] for case in report["cases"])
        report["full_suite"] = not args.quick
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print("Report: " + str(args.output), flush=True)
        return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
