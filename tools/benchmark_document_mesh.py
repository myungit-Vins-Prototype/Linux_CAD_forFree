#!/usr/bin/env python3
"""Confronto isolato eager/pipeline/GPU: processi nuovi, firme e input immutabile."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import statistics
import subprocess
import time


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("document", type=Path)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reference-binary", type=Path, help="confronta due binari con la stessa pipeline CPU")
    parser.add_argument("--no-profile", action="store_true", help="disattiva la profilazione durante le misure")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--cached", action="store_true", help="mantieni le cache lette; default ricostruzione completa")
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats deve essere positivo")
    document = args.document.resolve()
    binary = args.build_dir.resolve() / "forgecad_view_tests"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    digest = sha256(document)
    report = {"platform": platform.platform(), "document_sha256": digest,
              "document_bytes": document.stat().st_size, "binary_sha256": sha256(binary),
              "protocol": "Processi nuovi sequenziali, ordine ruotato, qualità 1; firme fuori timer. Tassellazione CPU nel runner; filtro GPU opt-in. RSS include validazione.",
              "forced_rebuild": not args.cached, "profiling_enabled": not args.no_profile, "runs": []}
    root = Path(__file__).resolve().parent.parent
    report["revision"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    report["sources_sha256"] = {name: sha256(root / name) for name in [
        "CMakeLists.txt", "tests/test_viewport.cpp", "tests/test_lazy_mesh.h",
        "forgeCad2026_viewport.cpp", "forgeCad2026_viewport_document.cpp",
        "forgeCad2026_viewport_render.cpp", "cad_document_io.cpp", "cad_document_read.cpp",
        "cad_document_read.h", "tools/benchmark_document_mesh.py"]}
    report["logical_cpus"] = os.cpu_count()
    if args.reference_binary:
        args.reference_binary = args.reference_binary.resolve()
        report["reference_binary_sha256"] = sha256(args.reference_binary)
    selected_modes = ["reference", "current"] if args.reference_binary else ["baseline", "pipeline", "gpu"]
    reference = None
    for repeat in range(args.repeats):
        rotation = repeat % len(selected_modes)
        modes = selected_modes[rotation:] + selected_modes[:rotation]
        for mode in modes:
            env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_OFFSCREEN_NO_GLX="1",
                       QT_QPA_PLATFORMTHEME="", QT_STYLE_OVERRIDE="Fusion",
                       FORGECAD_SOLVER_DENSE=str(int(mode == "baseline")),
                       FORGECAD_EAGER_MESH=str(int(mode == "baseline")),
                       FORGECAD_GPU_BROAD_PHASE=str(int(mode == "gpu")), FORGECAD_PROFILE="1")
            if args.no_profile:
                env.pop("FORGECAD_PROFILE", None)
                env.pop("FORGECAD_PROFILE_READ", None)
            executable = args.reference_binary if mode == "reference" else binary
            name = f"{repeat}-{mode}"
            rss = output / (name + ".rss")
            command = ["/usr/bin/time", "-f", "%M", "-o", str(rss), str(executable),
                       "--document-mesh-benchmark", str(document)]
            if not args.cached:
                command.append("--rebuild")
            start = time.monotonic()
            with (output / (name + ".json")).open("w") as stdout, (output / (name + ".log")).open("w") as stderr:
                process = subprocess.Popen(command, env=env, stdout=stdout, stderr=stderr, start_new_session=True)
                try:
                    code = process.wait(timeout=args.timeout)
                except BaseException:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                    raise
                if code:
                    raise RuntimeError(f"{name}: processo terminato con codice {code}; vedere il log")
            wall = time.monotonic() - start
            data = json.loads((output / (name + ".json")).read_text())
            if data["open_seconds"] > wall + 0.1:
                raise RuntimeError("clock non coerenti")
            if reference is None:
                reference = data["bodies"]
            if data["bodies"] != reference:
                raise RuntimeError(f"Geometria/mesh/errori/visibilità divergenti in {name}")
            item = {k: v for k, v in data.items() if k != "bodies"}
            item.update(mode=mode, repeat=repeat, wall_seconds=wall,
                        peak_rss_mib=int(rss.read_text().strip()) / 1024, equivalent=True)
            report["runs"].append(item)
            (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            print(f"{name}: {data['open_seconds']:.3f}s, identità verificata", flush=True)
    report["input_unchanged"] = sha256(document) == digest
    if not report["input_unchanged"]:
        raise RuntimeError("Documento originale cambiato durante il confronto")
    report["summary"] = {}
    for mode in selected_modes:
        rows = [r for r in report["runs"] if r["mode"] == mode]
        values = [r["open_seconds"] for r in rows]
        report["summary"][mode] = dict(median_seconds=statistics.median(values),
                                       min_seconds=min(values), max_seconds=max(values),
                                       median_read_seconds=statistics.median(r["read_seconds"] for r in rows),
                                       median_load_seconds=statistics.median(r["load_seconds"] for r in rows),
                                       median_peak_rss_mib=statistics.median(r["peak_rss_mib"] for r in rows))
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
