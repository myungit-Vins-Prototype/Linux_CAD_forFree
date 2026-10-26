#!/usr/bin/env python3
"""Confronto solver su trascinamenti ammissibili: originali in sola lettura."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import statistics
import subprocess


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("document", type=Path)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if args.repeats < 1 or args.timeout < 1:
        parser.error("--repeats e --timeout devono essere positivi")
    root = Path(__file__).resolve().parent.parent
    binary = args.build_dir.resolve() / "forgecad_view_tests"
    document = args.document.resolve()
    digest = sha256(document)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"document_sha256": digest, "document_bytes": document.stat().st_size,
              "binary_sha256": sha256(binary), "platform": platform.platform(), "runs": [],
              "protocol": "Processi nuovi sequenziali; 5 ripetizioni alternate dense/partition per processo. Ricerca target fuori timer su copie originali. Equivalenza geometrica, target e vincoli verificati dal runner; analisi DOF sullo schizzo originale. Nessun controllo frequenze o carico esterno."}
    report["sources_sha256"] = {name: sha256(root / name) for name in [
        "tests/test_solver_partition.h", "tests/test_viewport.cpp", "cad_constraints.cpp",
        "tools/benchmark_document_solver.py"]}
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_OFFSCREEN_NO_GLX="1")
    env.pop("FORGECAD_PROFILE", None)
    env.pop("FORGECAD_PROFILE_READ", None)
    try:
        selected = None
        for repeat in range(args.repeats):
            with (output / f"{repeat}.stderr.log").open("w") as errors:
                child = subprocess.Popen([str(binary), "--document-solver-benchmark", str(document),
                                          "--admissible-targets"], env=env, stdout=subprocess.PIPE,
                                         stderr=errors, text=True, start_new_session=True)
                try:
                    stdout, _ = child.communicate(timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.communicate()
                    raise RuntimeError(f"Timeout nel processo {repeat}")
            (output / f"{repeat}.json").write_text(stdout)
            if child.returncode:
                raise RuntimeError(f"Processo {repeat} fallito, codice {child.returncode}; consultare stderr")
            data = json.loads(stdout)
            cases = [(row["index"], row["target"]) for row in data["sketches"] if row["selected"]]
            if not cases or len(cases) != data["selected_sketches"]:
                raise RuntimeError("Nessun trascinamento ammissibile verificato")
            if selected is not None and cases != selected:
                raise RuntimeError("La selezione dei target varia tra processi")
            selected = cases
            if not all(row["solve_ok"] for row in data["sketches"] if row["selected"]):
                raise RuntimeError("Trascinamento selezionato non riuscito")
            report["runs"].append(data)
            print(f"{repeat + 1}/{args.repeats}: {len(cases)} schizzi con target applicati", flush=True)
        keys = ["dense_solve_ms", "partition_solve_ms", "dense_analysis_ms", "partition_analysis_ms"]
        report["summary"] = {}
        for key in keys:
            values = [run[key] for run in report["runs"]]
            report["summary"][key] = dict(median=statistics.median(values), min=min(values), max=max(values))
    finally:
        report["input_unchanged"] = sha256(document) == digest
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        if not report["input_unchanged"]:
            raise RuntimeError("Il documento originale e' cambiato durante il confronto")
    print(json.dumps(report["summary"], indent=2))


if __name__ == "__main__":
    main()
