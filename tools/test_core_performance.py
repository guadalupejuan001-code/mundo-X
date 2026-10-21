#!/usr/bin/env python3
"""Small regressions for identity, the pivot-index budget and process limits."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest


SPEC = importlib.util.spec_from_file_location("core_performance", Path(__file__).with_name("core-performance.py"))
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)


def sample(observer_bytes):
    return {"case": "observer_build", "qubits": 20, "iterations": 3276, "elapsed_ns": 1000000,
            "checksum": "2ef75bca5e20bfd6", "core_payload_bytes": 340,
            "observer_payload_bytes": observer_bytes}


class CorePerformanceChecks(unittest.TestCase):
    def test_known_index_growth_is_measured_with_unchanged_identity(self):
        before = DRIVER.fingerprint(sample(560), "observer_build", 20, 3276)
        after = DRIVER.fingerprint(sample(880), "observer_build", 20, 3276)
        DRIVER.require_same_fingerprint(before, after, "observer_build/20")
        record = DRIVER.observer_payload_record({"baseline": [560] * 3, "candidate": [880] * 3},
                                               "observer_build", 20, 8)
        self.assertEqual(record["observer_payload_bytes"], 880)
        self.assertEqual(record["observer_payload_bytes_by_version"], {"baseline": 560, "candidate": 880})
        self.assertEqual(record["observer_payload_delta_bytes"], 320)
        self.assertEqual(record["observer_payload_growth_budget_bytes"], 320)

    def test_checksum_and_core_memory_differences_are_still_rejected(self):
        before = DRIVER.fingerprint(sample(560), "observer_build", 20, 3276)
        for key, value in (("checksum", "different"), ("core_payload_bytes", 341)):
            with self.subTest(field=key):
                changed = sample(880)
                changed[key] = value
                with self.assertRaisesRegex(RuntimeError, "Different results"):
                    DRIVER.require_same_fingerprint(before, DRIVER.fingerprint(changed, "observer_build", 20, 3276),
                                                    "observer_build/20")

    def test_excess_growth_and_unstable_storage_are_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "exceeds pivot_rows_ budget"):
            DRIVER.observer_payload_record({"baseline": [560] * 3, "candidate": [881] * 3},
                                           "observer_build", 20, 8)
        with self.assertRaisesRegex(RuntimeError, "between identical candidate repetitions"):
            DRIVER.observer_payload_record({"baseline": [560] * 3, "candidate": [880, 879, 880]},
                                           "observer_build", 20, 8)

    @unittest.skipUnless(sys.platform == "linux", "The benchmark driver requires Linux RLIMIT_AS")
    def test_native_child_receives_both_address_space_limits(self):
        limit = DRIVER.effective_native_limit()
        result = DRIVER.native_command([sys.executable, "-c",
            "import json,resource;print(json.dumps(resource.getrlimit(resource.RLIMIT_AS)))"], 5, limit)
        self.assertEqual(json.loads(result), [limit, limit])
        self.assertLessEqual(limit, 2 * 1024 ** 3)

    @unittest.skipUnless(sys.platform == "linux", "The benchmark driver requires Linux RLIMIT_AS")
    def test_native_deadline_terminates_an_unfinished_process(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            DRIVER.native_command([sys.executable, "-c", "import time;time.sleep(5)"],
                                  0.05, DRIVER.effective_native_limit())

    @unittest.skipUnless(sys.platform == "linux", "The benchmark driver requires Linux RLIMIT_AS")
    def test_native_address_space_rejects_an_over_budget_mapping(self):
        # mmap reserves virtual address space; no 3 GiB physical allocation is attempted.
        with self.assertRaisesRegex(RuntimeError, "Native process failed"):
            DRIVER.native_command([sys.executable, "-c", "import mmap;mmap.mmap(-1,3*1024**3)"],
                                  5, DRIVER.effective_native_limit())


if __name__ == "__main__":
    unittest.main(verbosity=2)
