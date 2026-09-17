#!/usr/bin/env python3
"""Compare sequential readers and both random access modes."""

import argparse
import csv
import hashlib
import json
import pathlib
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--reference", required=True, type=pathlib.Path)
    parser.add_argument("--candidate", required=True, type=pathlib.Path)
    parser.add_argument("--generic", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--threads", type=int, default=32)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--requests", type=int, default=256)
    parser.add_argument("--metadata", type=int, choices=(0, 1), default=0)
    args = parser.parse_args()
    if min(args.threads, args.rounds, args.requests) < 1:
        parser.error("Thread, round, and request counts must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    manifest = {"input": str(args.input.resolve()), "input_bytes": args.input.stat().st_size, "commands": []}
    manifest["executables"] = {
        name: {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        for name, path in (("reference", args.reference), ("candidate", args.candidate), ("generic", args.generic))
    }

    def run(label, binary, mode, iteration, last_index=0):
        command = [str(binary.resolve()), str(args.input.resolve()), mode, str(args.threads), "1",
                   str(args.metadata), str(args.requests), str(last_index)]
        destination = args.output / f"{label}-{mode}-{iteration}.csv"
        print(f"Start {label} {mode} round {iteration}", flush=True)
        manifest["commands"].append({"label": label, "mode": mode, "round": iteration, "argv": command})
        (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        with destination.open("w") as output:
            subprocess.run(command, stdout=output, check=True)
        with destination.open() as output:
            rows = list(csv.DictReader(output))
        if len(rows) != 1:
            raise RuntimeError(f"Expected one result in {destination}")
        row = rows[0]
        row.update(label=label, round=iteration)
        records.append(row)
        print(f"Done {label} {mode}: total={float(row['total_seconds']):.3f}s "
              f"read={float(row['read_seconds']):.3f}s "
              f"PSS={int(row['live_pss_kib']) / 1048576:.3f}GiB "
              f"index={int(row['index_bytes']) / 1048576:.3f}MiB", flush=True)
        return row

    def counts(row):
        return tuple(row[key] for key in ("nodes", "ways", "relations", "checksum"))

    warmup = run("warmup", args.reference, "blocks", 0)
    last_index = int(warmup["max_index"])
    if last_index != int(warmup["blocks"]) or last_index == 0:
        raise RuntimeError("Random requests require consecutive data block indexes from one")
    sequential = [("reference", args.reference, "entities"), ("candidate", args.candidate, "entities"),
                  ("reference", args.reference, "blocks"), ("candidate", args.candidate, "blocks")]
    random = [("generic", args.generic, "random"), ("candidate", args.candidate, "random"),
              ("candidate", args.candidate, "random-index")]
    expected_random = None
    for iteration in range(args.rounds):
        for label, binary, mode in sequential[::1 if iteration % 2 == 0 else -1]:
            if counts(run(label, binary, mode, iteration)) != counts(warmup):
                raise RuntimeError("Sequential entity totals or checksum differ")
    for iteration in range(args.rounds):
        for label, binary, mode in random[::1 if iteration % 2 == 0 else -1]:
            row = run(label, binary, mode, iteration, last_index)
            if int(row["blocks"]) != args.requests:
                raise RuntimeError("Random request count differs")
            if expected_random is None:
                expected_random = counts(row)
            if counts(row) != expected_random:
                raise RuntimeError("Random entity totals or checksum differ")

    columns = ("setup_seconds", "read_seconds", "total_seconds", "cpu_seconds", "max_rss_kib",
               "index_bytes", "live_rss_kib", "live_pss_kib", "live_anon_kib", "index_build_seconds",
               "teardown_seconds", "page_table_kib")
    summary = []
    groups = sorted({(row["label"], row["mode"]) for row in records if row["label"] != "warmup"})
    for label, mode in groups:
        rows = [row for row in records if row["label"] == label and row["mode"] == mode]
        result = {"label": label, "mode": mode, "runs": len(rows)}
        for column in columns:
            values = [float(row[column]) for row in rows]
            result[column] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
        summary.append(result)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print("All entity totals and checksums match.", flush=True)


if __name__ == "__main__":
    main()
