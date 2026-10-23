#!/usr/bin/env python3
"""Exercise the public CSV decoder with synthetic controls and invalid inputs.

No hardware measurements are generated or claimed by this regression suite.
Usage: python3 tools/test_physical_ether_cli.py /path/to/mundo_x_physical_ether_lab
"""

import copy
import csv
import io
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


BINARY = str(Path(sys.argv.pop(1)).resolve())
SOURCE = Path(__file__).resolve().parents[1] / "examples/physical_ether_dipole_synthetic.csv"


class PhysicalCSVTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        lines = (line for line in SOURCE.read_text().splitlines() if not line.startswith("#"))
        reader = csv.DictReader(lines)
        self.fields = reader.fieldnames
        self.rows = list(reader)
        self.train = [row for row in self.rows if row["role"] == "TRAIN"]
        self.holdout = [row for row in self.rows if row["role"] == "HOLDOUT"]

    def run_csv(self, rows=None, query=(), filename="measurements.csv"):
        path = Path(self.folder.name) / filename
        stream = io.StringIO()
        writer = csv.DictWriter(stream, fieldnames=self.fields)
        writer.writeheader()
        writer.writerows(self.rows if rows is None else rows)
        path.write_text(stream.getvalue())
        return subprocess.run([BINARY, str(path), *map(str, query)],
                              capture_output=True, text=True, timeout=15)

    def rejected(self, result, message):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "", "failure leaked a partial/success report")
        self.assertIn(message, result.stderr)

    def report(self, result):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        return json.loads(result.stdout)

    def test_original_fixture_and_query(self):
        result = self.report(self.run_csv(query=(0, 0, .24, 2)))
        self.assertEqual((result["train"], result["holdout"]), (16, 8))
        self.assertLess(result["holdout_rmse_uT"], .1)
        self.assertTrue(result["passes_predeclared_20_percent_gain"])
        self.assertFalse(result["claimed_universal_lattice"])
        self.assertTrue(result["physical_sensor_validation_required"])
        self.assertEqual(len(result["query_prediction_uT"]), 3)

    def test_training_rows_cannot_be_relabelled_holdout(self):
        leaked = [dict(row, role="HOLDOUT", time_s="2") for row in self.train[:8]]
        self.rejected(self.run_csv(self.train + leaked), "overlaps TRAIN")

    def test_two_positions_are_not_eight_sites(self):
        # The two radii already identify all six model parameters; this is a
        # spatial protocol violation, not necessarily a rank deficiency.
        rows = []
        for i in range(8):
            x = .2 if i % 2 == 0 else .4
            rows.append(dict(self.train[0], x_m=str(x), y_m="0", z_m="0",
                             bx_uT=str(38 + 2 * .125 / x**3),
                             by_uT=str(-11 + .064 / x**3),
                             bz_uT=str(22 - .088 / x**3)))
        held = [dict(rows[0], role="HOLDOUT", x_m=str(x),
                     bx_uT=str(38 + 2 * .125 / x**3),
                     by_uT=str(-11 + .064 / x**3),
                     bz_uT=str(22 - .088 / x**3)) for x in (.24, .28, .32, .36)]
        self.rejected(self.run_csv(rows + held), "repeated measurement position")

    def test_duplicate_holdout_sites(self):
        rows = self.train + self.holdout[:4] * 2
        self.rejected(self.run_csv(rows), "repeated measurement position")

    def test_training_jitter_smaller_than_resolution(self):
        rows = copy.deepcopy(self.train)
        rows[-1] = dict(rows[0], x_m=str(float(rows[0]["x_m"]) + .002))
        self.rejected(self.run_csv(rows + self.holdout), "repeated measurement position")

    def test_holdout_jitter_cannot_disguise_training_overlap(self):
        held = copy.deepcopy(self.holdout)
        held[0] = dict(self.train[0], role="HOLDOUT", time_s="2",
                       x_m=str(float(self.train[0]["x_m"]) + .002))
        self.rejected(self.run_csv(self.train + held), "overlaps TRAIN")

    def test_uniform_field_is_not_dipole_evidence(self):
        rows = [dict(row, bx_uT="38", by_uT="-11", bz_uT="22") for row in self.rows]
        result = self.report(self.run_csv(rows))
        self.assertFalse(result["passes_predeclared_20_percent_gain"])

    def test_changed_source_fails_without_refitting(self):
        held = copy.deepcopy(self.holdout)
        moment, background = (-.04, .12, -.07), (38., -11., 22.)
        for row in held:
            r = [float(row[key]) for key in ("x_m", "y_m", "z_m")]
            radius = math.sqrt(sum(v*v for v in r))
            dot = sum(a*b for a, b in zip(r, moment))
            for i, key in enumerate(("bx_uT", "by_uT", "bz_uT")):
                row[key] = str(background[i] + 3*r[i]*dot/radius**5 - moment[i]/radius**3)
        result = self.report(self.run_csv(self.train + held))
        self.assertGreater(result["holdout_rmse_uT"], 5.)
        self.assertFalse(result["passes_predeclared_20_percent_gain"])

    def test_invalid_queries_emit_no_result(self):
        for query, message in [((7, 0, .24, 2), "six-meter"),
                               ((0, 0, .24, "nan"), "invalid finite number"),
                               ((0, 0, .24, 100), "not calibrated at this time")]:
            with self.subTest(query=query):
                self.rejected(self.run_csv(query=query), message)

    def test_nonfinite_sensor_sample(self):
        rows = copy.deepcopy(self.rows)
        rows[-1]["bx_uT"] = "nan"
        self.rejected(self.run_csv(rows), "invalid finite number")

    def test_mixed_reference_frames(self):
        rows = copy.deepcopy(self.rows)
        rows[-1]["frame"] = "other_frame"
        self.rejected(self.run_csv(rows), "mixed reference frames")

    def test_missing_holdout(self):
        self.rejected(self.run_csv(self.train), "at least 8 TRAIN and 4 HOLDOUT")

    def test_hardware_log_is_not_a_position_calibrated_dataset(self):
        path = Path(self.folder.name) / "hardware-latest.json"
        path.write_text(json.dumps({"schema": "mundox.hardware-probe.v1", "samples": []}))
        result = subprocess.run([BINARY, str(path)], capture_output=True, text=True, timeout=15)
        self.rejected(result, "invalid CSV heading")

    def test_filename_is_valid_json(self):
        result = self.report(self.run_csv(filename='measurements\t"quoted".csv'))
        self.assertEqual(Path(result["dataset"]).name, 'measurements\t"quoted".csv')


if __name__ == "__main__":
    unittest.main(verbosity=2)
