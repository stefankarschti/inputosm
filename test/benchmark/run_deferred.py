#!/usr/bin/env python3
"""Compare deferred PBF workloads in separate processes."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import time


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def memory(pid):
    try:
        lines = Path(f"/proc/{pid}/status").read_text().splitlines()
    except (FileNotFoundError, ProcessLookupError):
        return {}
    result = {}
    for line in lines:
        key, _, value = line.partition(":")
        if key in ("VmRSS", "RssAnon", "VmPTE"):
            result[key] = int(value.split()[0])
    return result


def run(command, stem):
    peaks = {}
    pid_file = stem.with_suffix(".pid")
    environment = dict(os.environ, INPUTOSM_BENCH_PID_FILE=str(pid_file.resolve()))
    host_pid = None
    with stem.with_suffix(".csv").open("w") as output, stem.with_suffix(".stderr").open("w") as error:
        process = subprocess.Popen(command, stdout=output, stderr=error, env=environment)
        while process.poll() is None:
            if host_pid is None:
                try:
                    host_pid = int(pid_file.read_text())
                except (FileNotFoundError, ValueError):
                    pass
            if host_pid is not None:
                for key, value in memory(host_pid).items():
                    peaks[key] = max(peaks.get(key, 0), value)
            time.sleep(0.05)
    pid_file.unlink(missing_ok=True)
    if process.returncode:
        raise RuntimeError(f"Command failed: {command}\n{stem.with_suffix('.stderr').read_text()}")
    with stem.with_suffix(".csv").open() as output:
        records = list(csv.DictReader(output))
    if len(records) != 1:
        raise RuntimeError(f"Expected one result: {command}")
    return {"command": command, "result": records[0], "sampled_peak_kib": peaks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--main", required=True)
    parser.add_argument("--reader", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--threads", type=int, default=32)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--requests", type=int, default=4096)
    parser.add_argument("--only", help="Run case names that contain this text.")
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    binaries = {}
    for label in ("candidate", "main", "reader"):
        source = Path(getattr(args, label)).resolve()
        target = output / f"{label}-benchmark"
        shutil.copy2(source, target)
        binaries[label] = str(target.resolve())
    source_files = [Path("src/inputosmpbf.cpp"), Path("src/pbfcolumns.hpp"), Path("include/inputosm/inputosm.hpp"),
                    Path("test/benchmark/pbf_benchmark.cpp")]
    manifest = {"input": str(Path(args.input).resolve()), "file_size": os.stat(args.input).st_size,
                "input_mtime_ns": os.stat(args.input).st_mtime_ns, "platform": platform.platform(),
                "arguments": vars(args), "binaries": {key: digest(value) for key, value in binaries.items()},
                "source_hashes": {str(path): digest(path) for path in source_files},
                "main_revision": subprocess.check_output(["git", "rev-parse", "main"], text=True).strip(),
                "reader_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    base = [args.input, "blocks", str(args.threads), "1", "0"]
    warm = run([binaries["candidate"]] + base, output / "warmup")
    maximum = int(warm["result"]["max_index"])
    expected = {key: int(warm["result"][key]) for key in ("blocks", "nodes", "ways", "relations", "checksum")}
    if maximum != expected["blocks"]:
        raise RuntimeError("Random workloads require consecutive data block indexes.")
    jobs = []
    for meta in (0, 1):
        for binary in ("main", "reader", "candidate"):
            jobs.append((f"legacy-{meta}", binary, "entities", meta, [], 14))
        jobs.append((f"decode-all-{meta}", "candidate", "blocks", meta, [], 14))
    for binary in ("main", "reader"):
        jobs.append(("eager-counts", binary, "blocks", 0, ["--consume=count"], 14))
    for mask in range(1, 16):
        jobs.append((f"counts-{mask:02d}", "candidate", "blocks", 0, [f"--counts={mask}"], mask & 14))
    for name, flags, counts in (
        ("nodes-positions", ["--nodes=7", "--consume=all"], 2),
        ("ways-tags-refs", ["--ways=3", "--consume=all"], 4),
        ("relations-full", ["--relations=31", "--consume=all"], 8),
    ):
        jobs.append((name, "candidate", "blocks", 0, flags, counts))
    for mode in ("random", "random-index"):
        for binary in ("reader", "candidate"):
            jobs.append((mode, binary, mode, 0, [], 14))
        jobs.append((mode + "-counts", "candidate", mode, 0, ["--counts=15"], 14))
    if args.only:
        jobs = [job for job in jobs if args.only in job[0]]
    records = []
    consistent = {}
    for round_number in range(args.rounds):
        for case, binary, mode, meta, flags, count_mask in (jobs if round_number % 2 == 0 else reversed(jobs)):
            stem = output / f"{round_number:02d}-{binary}-{case}"
            command = [binaries[binary], args.input, mode, str(args.threads), "1", str(meta),
                       str(args.requests), str(maximum)] + flags
            record = run(command, stem)
            record.update(case=case, binary=binary, round=round_number)
            row = record["result"]
            if not mode.startswith("random"):
                for key, bit in (("nodes", 2), ("ways", 4), ("relations", 8)):
                    if int(row[key]) != (expected[key] if count_mask & bit else 0):
                        raise RuntimeError(f"Incorrect {key}: {record}")
                if (case.startswith("legacy-") or case.startswith("decode-all-")) and int(row["checksum"]) != expected["checksum"]:
                    raise RuntimeError(f"Incorrect ID checksum: {record}")
            values = tuple(row[key] for key in ("blocks", "nodes", "ways", "relations", "checksum", "values_checksum"))
            identity = (case, binary)
            if identity in consistent and consistent[identity] != values:
                raise RuntimeError(f"Results changed between rounds: {record}")
            consistent[identity] = values
            if mode.startswith("random") and not flags:
                other = (case, "reader" if binary == "candidate" else "candidate")
                if other in consistent and consistent[other] != values:
                    raise RuntimeError("Random baseline results differ.")
            records.append(record)
            (output / "results.json").write_text(json.dumps(records, indent=2) + "\n")
            print(f"{round_number + 1}/{args.rounds} {binary:9} {case:22} {float(row['total_seconds']):9.3f} s", flush=True)
    summary = []
    for case, binary in sorted(consistent):
        selected = [record for record in records if record["case"] == case and record["binary"] == binary]
        entry = {"case": case, "binary": binary, "runs": len(selected)}
        for key in ("total_seconds", "read_seconds", "setup_seconds", "teardown_seconds", "index_build_seconds",
                    "cpu_seconds", "max_rss_kib", "live_pss_kib", "live_anon_kib", "page_table_kib", "index_bytes"):
            values = [float(record["result"][key]) for record in selected]
            entry[key] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
        for key in ("VmRSS", "RssAnon", "VmPTE"):
            values = [record["sampled_peak_kib"][key] for record in selected if key in record["sampled_peak_kib"]]
            entry["sampled_peak_" + key] = ({"median": statistics.median(values), "min": min(values), "max": max(values)}
                                            if values else {"median": None, "min": None, "max": None})
        summary.append(entry)
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
