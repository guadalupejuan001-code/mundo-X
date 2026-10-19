#!/usr/bin/env python3
"""Black-box contracts for the separate InfiniteCore runner (stdlib only)."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    checks = 0
    cases = []

    def check(condition, description):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(description)

    def run(name, source="", options=(), error=None):
        process = subprocess.run(
            [str(binary), *options], input=source, text=True, encoding="utf-8",
            capture_output=True, timeout=30,
        )
        if error is None:
            check(process.returncode == 0, f"{name}: {process.stderr}")
            check(not process.stderr, f"{name}: unexpected stderr: {process.stderr}")
        else:
            check(process.returncode != 0, f"{name}: rejection expected")
            check(error in process.stderr, f"{name}: diagnostic: {process.stderr!r}")
        records = [json.loads(line) for line in process.stdout.splitlines()]
        cases.append({"name": name, "status": "passed", "expected_rejection": error is not None})
        return records

    far = "1" + "0" * 100
    partner = far[:-1] + "1"
    untouched = run("zero_background", f"stats\nprobz {far}\nexpect Z:{far}\nexpect X:{far}\nstats\n")
    check(untouched[0] == untouched[-1], "queries preserve all state counters")
    check(untouched[0]["active_qubits"] == 0, "zero background is implicit")
    check(untouched[1]["probability_one"] == 0, "remote zero probability")
    check([item["value"] for item in untouched[2:4]] == [1, 0], "zero background Paulis")

    bell = run("bell_background", f"""background bell
expect X:{far} X:{partner}
expect Y:{far} Y:{partner}
expect Z:{far} Z:{partner}
expect - Y:{far} Y:{partner}
expect X:{far}
expect X:{far} Z:{partner}
stats
""")
    check([item["value"] for item in bell[:-1]] == [1, -1, 1, 1, 0, 0], "signed Bell correlations")
    check(bell[-1]["active_qubits"] == bell[-1]["tableau_bytes"] == 0, "Bell background remains implicit")

    sparse = run("remote_bell_creation_and_inverse", f"""begin
h 0
cx 0 {far}
end
assert expect 1 X:0 X:{far}
assert expect -1 Y:0 Y:{far}
assert expect 1 Z:0 Z:{far}
stats
begin
cx 0 {far}
h 0
end
compact
stats
""")
    check(sparse[0]["active_qubits"] == sparse[0]["largest_component"] == 2, "only two remote sites materialize")
    check(sparse[0]["revision"] == 1, "batch commits one revision")
    check(sparse[0]["tableau_bytes"] == 34, "two-qubit packed payload")
    check(sparse[0]["address_bytes"] == 102, "text payload reflects decimal address length")
    check(sparse[1]["released_qubits"] == 2, "exact inverse releases both sites")
    check(sparse[2]["active_qubits"] == sparse[2]["tableau_bytes"] == sparse[2]["address_bytes"] == 0,
          "compaction returns storage counters to zero")
    check(sparse[2]["revision"] == 3, "inverse batch and compaction each commit")

    conditioned = run("bell_measurement_correlations", f"""background bell
postselect {far} 1
probz {partner}
measure {partner}
compact
stats
""")
    check(conditioned[0]["probability"] == 0.5, "Bell branch probability")
    check(conditioned[1]["probability_one"] == 1, "remote partner collapses with measured member")
    check(conditioned[2]["outcome_one"] and not conditioned[2]["random"], "partner result is deterministic")
    check(conditioned[3]["released_qubits"] == 0, "collapsed pair is not its Bell background")
    check(conditioned[4]["active_qubits"] == 2 and conditioned[4]["revision"] == 1,
          "deterministic measurement and unsuccessful compaction do not revise")

    random_program = "background bell\nseed 987654321\n" + "\n".join(f"measure {2*i}" for i in range(24)) + "\nstats\n"
    first = run("seeded_measurements_a", random_program)
    second = run("seeded_measurements_b", random_program)
    check(first == second, "identical seeded scripts reproduce outcomes and state counters")
    check(all(item["probability"] == 0.5 and item["random"] for item in first[:-1]), "each new Bell pair is sampled")

    deterministic = run("zero_budget_queries", f"seed 42\nmeasure {far}\nprobz {far}\nexpect\nstats\n",
                        ("--max-active-qubits", "0", "--max-component-qubits", "0", "--max-state-bytes", "0", "--max-query-terms", "0"))
    check(not deterministic[0]["random"] and not deterministic[0]["outcome_one"], "zero-budget deterministic measurement")
    check(deterministic[-1]["revision"] == deterministic[-1]["active_qubits"] == 0, "fixed query needs no materialization budget")

    aliases = run("canonical_aliases", "x 0009\nassert probz 9 1\nprobz 00009\nstats\n")
    check(aliases[0]["address"] == "9" and aliases[1]["address_bytes"] == 1, "canonical address identity")
    run("raw_digit_budget_single", "x 0009\n", ("--max-address-digits", "3"), "digit budget")
    run("raw_digit_budget_batch", "begin\nx 0009\nend\n", ("--max-address-digits", "3"), "digit budget")
    run("digit_budget_zero", options=("--max-address-digits", "0"), error="must be positive")
    run("unsupported_t", "t 0\n", error="line 1: unknown command")
    run("duplicate_alias_query", "expect X:0 Z:0000\n", error="duplicate canonical")
    run("duplicate_identity_query", "expect I:0 I:00\n", error="duplicate canonical")
    run("same_control_target", "cx 9 0009\n", error="distinct addresses")
    run("query_terms_limit", "expect Z:0 Z:1\n", ("--max-query-terms", "1"), "query-term budget")
    run("active_limit", "h 0\nh 1\n", ("--max-active-qubits", "1"), "active qubit")
    run("component_limit", "h 0\ncx 0 1\n", ("--max-component-qubits", "1"), "component qubit")
    run("payload_limit", "begin\nh 0\ncx 0 1\nend\n", ("--max-state-bytes", "33"), "tableau payload")
    run("bell_partner_budget", "background bell\nz 0\n", ("--max-active-qubits", "1"), "active qubit")
    run("impossible_postselection", "postselect 0 1\n", error="impossible")
    run("failed_assertion", "assert probz 0 1\n", error="assertion failed")
    run("malformed_assertion", "assert expect nan X:0\n", error="expected expectation")
    run("late_background", "x 0\nbackground zero\n", error="first command")
    run("conflicting_background", "background bell\n", ("--background", "zero"), "conflicts")
    run("matching_background", "background bell\nassert expect 1 X:0 X:1\n", ("--background", "bell"))
    run("empty_batch", "begin\nend\nassert probz 0 0\n")
    run("nested_batch", "begin\nbegin\n", error="nested batches")
    run("unterminated_batch", "begin\nh 0", error="unterminated batch")
    run("batch_query", "begin\nstats\nend\n", error="gates only")
    run("end_without_begin", "end\n", error="no matching begin")
    run("batch_count_limit", "begin\n" + "h 0\n" * 65537 + "end\n", error="65536 gates")
    long_address = "1" * 4096
    run("batch_text_limit", "begin\n" + f"h {long_address}\n" * 4097 + "end\n", error="16 MiB")
    run("line_limit", "#" * (1024 * 1024 + 1) + "\n", error="line exceeds 1 MiB")
    run("control_z_is_not_eof", "stats\n\x1a\nx 0\n", error="line 2: unknown command")
    for index, address in enumerate(("-1", "+1", "1e3", "0x10", "1:2", "１", "1\x002")):
        run(f"invalid_address_{index}", f"probz {address}\n", error="ASCII decimal digits")
    run("uint64_seed_max", "seed 18446744073709551615\n")
    run("uint64_seed_overflow", "seed 18446744073709551616\n", error="integer range")
    run("unsigned_limit_negative", options=("--max-active-qubits", "-1"), error="decimal digits")
    run("unsigned_limit_overflow", options=("--max-active-qubits", "9" * 80), error="integer range")
    run("unknown_option", options=("--infinite-physical-memory",), error="unknown option")
    run("extra_argument", options=("one.xi", "two.xi"), error="one program file")
    run("missing_file", options=(str(binary.parent / "nonexistent_infinite_fixture.xi"),), error="cannot open program")
    for name in ("zero", "bell"):
        source = Path(__file__).resolve().parents[1] / "examples" / f"infinite_{name}.xi"
        run(f"example_file_{name}", options=(str(source),))

    result = {"suite": "infinite_cli", "status": "passed", "cases": len(cases),
              "checks": checks, "details": cases}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "details"}))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Infinite CLI validation failed: {error}", file=sys.stderr)
        sys.exit(1)
