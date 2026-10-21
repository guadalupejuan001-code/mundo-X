#!/usr/bin/env python3
"""Check the production Solar ray differentials against numerical geometry.

The GPU executes helper functions extracted from solar.frag. The independent
oracle perturbs rays/points and re-intersects spheres/planes in double precision.
This checks differential mathematics, not an image-comparison tolerance.
"""
import argparse
import ctypes.util
import hashlib
import json
import os
from pathlib import Path
import resource
import sys

os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ.setdefault('LP_NUM_THREADS', '1')
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'graphics-desktop'))


def extract_function(source, name):
    start = source.index(name + '(')
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def run(shader, count):
    import numpy as np
    from egl_runtime import GL

    source = shader.read_text()
    names = ['angularGradient', 'sphereNormalGradient', 'radialGradient',
             'ringFootprint', 'ringShadowFootprint']
    helpers = '\n'.join(extract_function(source, name) for name in names)
    fragment = '''#version 300 es
precision highp float;
precision highp int;
const float PI=3.141592653589793;
uniform vec3 n,rd,dx,dy,axis,p;
uniform float t,r,lightDistance;
uniform int mode;
out vec4 outColor;
''' + helpers + '''
void main(){
    vec3 answer=vec3(0);
    if(mode==0)answer=sphereNormalGradient(n,rd,dx,t,r);
    if(mode==1)answer=vec3(angularGradient(n,dx),0);
    if(mode==2)answer.x=ringFootprint(rd,dx,dy,t,axis,p,r);
    if(mode==3)answer.x=ringShadowFootprint(rd,lightDistance,dx,dy,t,axis,p,r);
    uint bits=floatBitsToUint(answer[int(gl_FragCoord.x)]);
    outColor=vec4(uvec4(bits,bits>>8,bits>>16,bits>>24)&255u)/255.0;
}
'''
    vertex = (shader.parent / 'solar.vert').read_text()
    g = GL()
    program = g.program(vertex, fragment)
    g.glUseProgram(program)
    g.glBindVertexArray(g.object('glGenVertexArrays'))
    g.glViewport(0, 0, 3, 1)
    for flag in [0x0BD0, 0x0B71, 0x0BE2, 0x0C11]:
        g.glDisable(flag)  # dither, depth, blend, scissor
    locations = {key: g.glGetUniformLocation(program, key.encode()) for key in
                 ['n', 'rd', 'dx', 'dy', 'axis', 'p', 't', 'r', 'lightDistance', 'mode']}

    def evaluate(mode, **values):
        g.glUniform1i(locations['mode'], mode)
        for key, value in values.items():
            if np.isscalar(value):
                g.glUniform1f(locations[key], float(value))
            else:
                g.glUniform3f(locations[key], *value)
        g.glDrawArrays(4, 0, 3)
        image = np.zeros((1, 3, 4), np.uint8)
        g.glReadPixels(0, 0, 3, 1, 0x1908, 0x1401, image.ctypes.data)
        g.check('gradient readback')
        return np.frombuffer(image.tobytes(), dtype='<f4').astype(float)

    rng = np.random.default_rng(20261021)
    unit = lambda v: v / np.linalg.norm(v)
    vector = lambda: unit(rng.normal(size=3))
    tangent = lambda n: (lambda v: (v - n * np.dot(v, n)) * .001)(rng.normal(size=3))
    h = .001
    difference = lambda fn: (fn(h) - fn(-h)) / (2 * h)
    stats = {}

    def check(name, actual, expected):
        actual, expected = np.atleast_1d(actual), np.atleast_1d(expected)
        delta = np.abs(actual - expected)
        limit = 3e-6 + 3e-4 * np.abs(expected)
        record = stats.setdefault(name, dict(values=0, failures=0, max_absolute_error=0.,
                                           max_error_over_limit=0.))
        record['values'] += len(actual)
        record['failures'] += int(np.sum(~np.isfinite(actual) | (delta > limit)))
        record['max_absolute_error'] = max(record['max_absolute_error'], float(np.max(delta)))
        record['max_error_over_limit'] = max(record['max_error_over_limit'], float(np.max(delta / limit)))

    try:
        for _ in range(count):
            rd, n = vector(), vector()
            while abs(np.dot(rd, n)) < .2:
                n = vector()
            if np.dot(rd, n) > 0:
                n = -n
            radius = float(rng.uniform(.1, 10))
            t = radius * float(rng.uniform(2, 20))
            centre = rd * t - n * radius
            dx = tangent(rd)

            def sphere_normal(offset):
                ray = unit(rd + dx * offset)
                projection = np.dot(centre, ray)
                perpendicular = centre - ray * projection
                near = projection - np.sqrt(radius ** 2 - np.dot(perpendicular, perpendicular))
                return (ray * near - centre) / radius

            check('sphere_normal', evaluate(0, n=n, rd=rd, dx=dx, t=t, r=radius),
                  difference(sphere_normal))

            n = vector()
            while abs(n[1]) > .98:
                n = vector()
            dx = tangent(n)

            def angles(offset):
                direction = unit(n + dx * offset)
                return np.array([np.arctan2(direction[2], direction[0]) / (2 * np.pi),
                                 np.arcsin(direction[1]) / np.pi])

            angular_delta = angles(h) - angles(-h)
            angular_delta[0] = (angular_delta[0] + .5) % 1 - .5
            check('angular_coordinates', evaluate(1, n=n, dx=dx)[:2], angular_delta / (2 * h))

            axis, rd = vector(), vector()
            while abs(np.dot(axis, rd)) < .2:
                rd = vector()
            p = unit(np.cross(axis, vector())) * radius * rng.uniform(1.2, 3)
            t = radius * rng.uniform(2, 20)
            centre = rd * t - p
            dx, dy = tangent(rd), tangent(rd)

            def ring_radius(offset, differential):
                ray = unit(rd + differential * offset)
                distance = np.dot(centre, axis) / np.dot(ray, axis)
                return np.linalg.norm(ray * distance - centre) / radius

            expected = abs(difference(lambda o: ring_radius(o, dx))) + abs(difference(lambda o: ring_radius(o, dy)))
            check('ring_plane_radius', evaluate(2, rd=rd, dx=dx, dy=dy, t=t, axis=axis, p=p, r=radius)[0], expected)

            # Keep the sun fixed while perturbing the receiving surface point.
            # Both the normalized light direction and the plane intersection move.
            point = p - rd * t
            light_distance = radius * rng.uniform(50, 200)
            sun = point + rd * light_distance
            dx, dy = rng.normal(size=3) * radius * .001, rng.normal(size=3) * radius * .001

            def shadow_radius(offset, differential):
                position = point + differential * offset
                light = unit(sun - position)
                distance = -np.dot(position, axis) / np.dot(light, axis)
                return np.linalg.norm(position + light * distance) / radius

            expected = abs(difference(lambda o: shadow_radius(o, dx))) + abs(difference(lambda o: shadow_radius(o, dy)))
            check('ring_shadow_radius', evaluate(3, rd=rd, lightDistance=light_distance, dx=dx, dy=dy,
                                               t=t, axis=axis, p=p, r=radius)[0], expected)

        # Exact tangent and pole endpoints have singular mathematical derivatives;
        # production must provide finite filter inputs there, never NaNs/infinities.
        endpoint_values = [
            evaluate(0, n=[1., 0., 0.], rd=[0., 0., 1.], dx=[.001, 0., 0.], t=10., r=1.),
            evaluate(1, n=[0., 1., 0.], dx=[.001, 0., 0.]),
            evaluate(1, n=[0., -1., 0.], dx=[0., 0., .001]),
        ]
        finite_endpoints = bool(np.isfinite(endpoint_values).all())
        return dict(passed=finite_endpoints and all(v['failures'] == 0 for v in stats.values()),
                    driver=g.info, seed=20261021, cases_per_family=count, statistics=stats,
                    oracle='Double precision central differences of independently re-intersected geometry',
                    oracle_step_pixels=h, absolute_tolerance=3e-6, relative_tolerance=3e-4,
                    finite_tangent_and_poles=finite_endpoints,
                    shader_sha256=hashlib.sha256(source.encode()).hexdigest(),
                    helper_sha256=hashlib.sha256(helpers.encode()).hexdigest(),
                    rss_peak_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
    finally:
        g.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shader', type=Path, default=ROOT / 'solar-android/app/src/main/assets/shaders/solar.frag')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=128)
    parser.add_argument('--egl-library', type=Path)
    args = parser.parse_args()
    if args.cases < 1:
        parser.error('--cases must be positive')
    if args.egl_library:
        find_library = ctypes.util.find_library
        ctypes.util.find_library = lambda name: str(args.egl_library.resolve()) if name == 'EGL' else find_library(name)
    resource.setrlimit(resource.RLIMIT_AS, (1024**3, 1024**3))
    resource.setrlimit(resource.RLIMIT_CPU, (60, 61))
    os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
    result = run(args.shader, args.cases)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    sys.exit(0 if result['passed'] else 1)
