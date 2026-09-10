#!/usr/bin/env python3
"""Checks firmware/src/ui/pressure_logic.cpp against design/logic.json.

The JSON is the specification: five outlook bands keyed on the 3-hour
pressure change, and four body-effect rules written as C-style boolean
expressions over delta_3h, pressure, humidity and temp_c. This script
compiles the firmware's implementation with the host compiler, feeds it a
grid of inputs that straddles every threshold, and evaluates the JSON's own
expressions in Python for the same inputs. Any disagreement fails the build.

Band edges: a band's `delta_3h_max` is inclusive when negative and exclusive
when positive, so exactly -3.0 is Storm Risk and exactly +3.0 is Wind Risk -
the SPEC's "<= -3.0" and ">= +3.0". The last band (max null) takes the rest.

    python3 tools/check_pressure_logic.py
"""
import itertools
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGIC = os.path.join(ROOT, "design", "logic.json")
SRC = os.path.join(ROOT, "firmware", "src", "ui", "pressure_logic.cpp")
HARNESS = os.path.join(ROOT, "firmware", "test", "host", "pressure_logic_harness.cpp")
INCLUDE = os.path.join(ROOT, "firmware", "src")

DELTAS = [-5.0, -3.5, -3.0, -2.9, -2.5, -2.4, -2.0, -1.5, -1.4, -1.2, -1.1, -1.0,
          -0.9, -0.5, 0.0, 0.5, 0.9, 1.0, 1.1, 1.2, 1.3, 1.5, 2.4, 2.5, 2.6, 2.9, 3.0, 3.5]
PRESSURES = [995.0, 1004.0, 1005.0, 1006.0, 1007.9, 1008.0, 1012.0, 1018.0, 1018.1,
             1020.0, 1022.0, 1022.1, 1030.0]
HUMIDITIES = [40.0, 80.0, 80.1, 95.0]
TEMPS = [-5.0, 4.9, 5.0, 9.9, 10.0, 25.0]


def band_index(bands, delta):
    for i, band in enumerate(bands):
        mx = band["delta_3h_max"]
        if mx is None:
            return i
        if (mx < 0 and delta <= mx) or (mx >= 0 and delta < mx):
            return i
    return len(bands) - 1


def evaluate(expr, env):
    py = expr.replace("||", " or ").replace("&&", " and ")
    py = re.sub(r"\babs\(", "abs(", py)
    return bool(eval(py, {"abs": abs, "__builtins__": {}}, env))


def risk_level(rule, env):
    if evaluate(rule["high"], env):
        return 2
    if evaluate(rule["medium"], env):
        return 1
    return 0


def main():
    with open(LOGIC) as f:
        logic = json.load(f)
    bands = logic["pressure_outlook_bands"]
    rules = logic["biometrics"]
    if [r["id"] for r in rules] != ["joint_pain", "migraine", "sinus_ears", "heart_strain"]:
        print("logic.json biometric order changed; update the harness", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "harness")
        cmd = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", INCLUDE,
               SRC, HARNESS, "-o", exe]
        subprocess.run(cmd, check=True)

        cases = list(itertools.product(DELTAS, PRESSURES, HUMIDITIES, TEMPS))
        stdin = "".join(f"{d} {p} {h} {t}\n" for d, p, h, t in cases)
        run = subprocess.run([exe], input=stdin, capture_output=True, text=True)
        if run.returncode != 0:
            print(f"harness self-test failed with code {run.returncode}", file=sys.stderr)
            return 1
        lines = run.stdout.strip().splitlines()

    if len(lines) != len(cases):
        print(f"expected {len(cases)} results, got {len(lines)}", file=sys.stderr)
        return 1

    failures = 0
    for (d, p, h, t), line in zip(cases, lines):
        got = [int(v) for v in line.split()]
        env = {"delta_3h": d, "pressure": p, "humidity": h, "temp_c": t}
        want = [band_index(bands, d)] + [risk_level(r, env) for r in rules]
        if got != want:
            failures += 1
            if failures <= 20:
                print(f"MISMATCH delta={d} p={p} rh={h} t={t}: firmware {got}, spec {want}")
    if failures:
        print(f"{failures} of {len(cases)} cases disagree with design/logic.json")
        return 1
    print(f"pressure logic: {len(cases)} cases agree with design/logic.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
