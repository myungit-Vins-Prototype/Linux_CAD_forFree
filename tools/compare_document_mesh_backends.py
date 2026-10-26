#!/usr/bin/env python3
"""Confronto numerico CPU/CUDA delle mesh visibili ricostruite (richiede NumPy)."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shlex
import signal
import struct
import subprocess
import time

import numpy as np


class SnapshotReader:
    def __init__(self, data):
        if data[:8] != b"FCMSH001":
            raise ValueError("Schema snapshot mesh non supportato")
        self.data, self.offset = data, 8

    def count(self):
        if len(self.data) - self.offset < 8:
            raise ValueError("Conteggio snapshot troncato")
        value, = struct.unpack_from("<Q", self.data, self.offset)
        self.offset += 8
        return value

    def array(self, kind, components=1):
        count = self.count()
        size = components * 4
        if count > (len(self.data) - self.offset) // size:
            raise ValueError("Array snapshot troncato o conteggio non valido")
        values = np.frombuffer(self.data, dtype=kind, count=count * components, offset=self.offset)
        self.offset += count * size
        return values.reshape((count, components)) if components != 1 else values

    def nested(self, kind, components=1):
        count = self.count()
        if count > (len(self.data) - self.offset) // 8:
            raise ValueError("Liste snapshot troncate")
        return [self.array(kind, components) for _ in range(count)]

    def read(self):
        result = dict(vertices=self.array("<f4", 3), normals=self.array("<f4", 3),
                      triangle_faces=self.array("<i4"), edges=self.nested("<f4", 3),
                      edge_ids=self.array("<i4"), face_ids=self.array("<i4"),
                      labels=self.array("<f4", 3), face_edges=self.nested("<i4"),
                      instances=self.array("<f4", 16))
        if self.offset != len(self.data):
            raise ValueError("Byte residui nello snapshot mesh")
        if len(result["vertices"]) != len(result["normals"]) or len(result["vertices"]) % 3:
            raise ValueError("Conteggi vertici/normali/triangoli incoerenti")
        if len(result["triangle_faces"]) != len(result["vertices"]) // 3:
            raise ValueError("Mappa triangoli-facce incompleta")
        if len(result["edges"]) != len(result["edge_ids"]) or len(result["labels"]) != len(result["face_ids"]):
            raise ValueError("Mappa spigoli o etichette incompleta")
        return result


def vector_difference(a, b, tolerance, normals=False):
    if not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError("Coordinate o normali non finite nello snapshot")
    result = dict(shape_equal=a.shape == b.shape, reference_count=len(a), candidate_count=len(b))
    if a.shape != b.shape:
        return dict(result, passed=False)
    numeric = np.any(a != b, axis=1)
    bits = np.any(a.view("<u4") != b.view("<u4"), axis=1)
    delta = a.astype(np.float64) - b.astype(np.float64)
    lengths = np.linalg.norm(delta, axis=1)
    result.update(numeric_different_rows=int(numeric.sum()), bitwise_different_rows=int(bits.sum()),
                  max_distance=float(lengths.max()) if len(a) else 0.,
                  rms_distance=float(np.sqrt(np.mean(lengths * lengths))) if len(a) else 0.,
                  max_component=float(np.abs(delta).max()) if len(a) else 0.,
                  worst_row=int(np.argmax(lengths)) if len(a) else None)
    result["passed"] = result["max_distance"] <= tolerance
    if normals:
        an, bn = np.linalg.norm(a.astype(np.float64), axis=1), np.linalg.norm(b.astype(np.float64), axis=1)
        nonzero = (an != 0) & (bn != 0)
        result.update(reference_zero_normals=int((an == 0).sum()), candidate_zero_normals=int((bn == 0).sum()),
                      zero_mask_equal=bool(np.array_equal(an == 0, bn == 0)))
        if nonzero.any():
            av, bv = a[nonzero].astype(np.float64) / an[nonzero, None], b[nonzero].astype(np.float64) / bn[nonzero, None]
            angles = np.arctan2(np.linalg.norm(np.cross(av, bv), axis=1), np.sum(av * bv, axis=1))
            result["max_angle_degrees"] = float(np.rad2deg(angles.max()))
        else:
            result["max_angle_degrees"] = 0.
        result["passed"] = result["passed"] and result["zero_mask_equal"]
    return result


def integer_difference(a, b):
    return dict(equal=bool(np.array_equal(a, b)), reference_count=len(a), candidate_count=len(b))


def compare_snapshots(a, b, position_tolerance=1e-5, normal_tolerance=1e-5):
    result = {}
    for key in ["vertices", "labels", "instances"]:
        result[key] = vector_difference(a[key], b[key], position_tolerance)
    result["normals"] = vector_difference(a["normals"], b["normals"], normal_tolerance, normals=True)
    topology = {key: integer_difference(a[key], b[key]) for key in ["triangle_faces", "edge_ids", "face_ids"]}
    topology["face_edges"] = dict(equal=len(a["face_edges"]) == len(b["face_edges"]) and all(
        np.array_equal(x, y) for x, y in zip(a["face_edges"], b["face_edges"])))
    edge_shapes = [len(edge) for edge in a["edges"]] == [len(edge) for edge in b["edges"]]
    topology["edge_polyline_sizes"] = dict(equal=edge_shapes)
    result["topology"] = topology
    if edge_shapes:
        edges_a = np.concatenate(a["edges"]) if a["edges"] else np.empty((0, 3), dtype="<f4")
        edges_b = np.concatenate(b["edges"]) if b["edges"] else np.empty((0, 3), dtype="<f4")
        result["edges"] = vector_difference(edges_a, edges_b, position_tolerance)
    else:
        result["edges"] = dict(passed=False, shape_equal=False)
    if a["vertices"].shape == b["vertices"].shape:
        triangles_a, triangles_b = a["vertices"].astype(np.float64).reshape((-1, 3, 3)), b["vertices"].astype(np.float64).reshape((-1, 3, 3))
        cross_a = np.cross(triangles_a[:, 1] - triangles_a[:, 0], triangles_a[:, 2] - triangles_a[:, 0])
        cross_b = np.cross(triangles_b[:, 1] - triangles_b[:, 0], triangles_b[:, 2] - triangles_b[:, 0])
        zero_a, zero_b = np.linalg.norm(cross_a, axis=1) == 0, np.linalg.norm(cross_b, axis=1) == 0
        flips = int((np.sum(cross_a * cross_b, axis=1) < 0).sum())
        result["triangles"] = dict(reference_degenerate=int(zero_a.sum()), candidate_degenerate=int(zero_b.sum()),
                                   degenerate_mask_equal=bool(np.array_equal(zero_a, zero_b)), winding_flips=flips)
    else:
        result["triangles"] = dict(degenerate_mask_equal=False, winding_flips=None)
    result["per_face"] = []
    if topology["triangle_faces"]["equal"] and a["vertices"].shape == b["vertices"].shape:
        for face in np.unique(a["triangle_faces"]):
            mask = np.repeat(a["triangle_faces"] == face, 3)
            result["per_face"].append(dict(face=int(face), triangles=int(mask.sum() // 3),
                vertices=vector_difference(a["vertices"][mask], b["vertices"][mask], position_tolerance),
                normals=vector_difference(a["normals"][mask], b["normals"][mask], normal_tolerance, normals=True)))
    result["passed"] = (all(value["equal"] for value in topology.values())
        and all(result[key]["passed"] for key in ["vertices", "normals", "edges", "labels", "instances"])
        and result["triangles"]["degenerate_mask_equal"] and result["triangles"]["winding_flips"] == 0)
    return result


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def link_cuda_probe(build, destination):
    # CMake Unix Makefiles: riusa gli stessi oggetti del runner, sostituendo
    # soltanto lo stub CPU. Per altri generatori fornire --cuda-binary.
    view_link = build / "CMakeFiles/forgecad_view_tests.dir/link.txt"
    cuda_link = build / "CMakeFiles/forgecad_cuda_tests.dir/link.txt"
    if not view_link.exists() or not cuda_link.exists():
        raise RuntimeError("Ricollegamento disponibile con CMake Unix Makefiles; fornire --cuda-binary per altre build")
    args, cuda_args = shlex.split(view_link.read_text()), shlex.split(cuda_link.read_text())
    gpu_objects = [arg for arg in cuda_args if arg.endswith("/cuda_support.cu.o")]
    stub = [arg for arg in args if arg.endswith("/cuda_support_cpu.cpp.o")]
    if len(gpu_objects) != 1 or len(stub) != 1:
        raise RuntimeError("Oggetti CUDA/CPU non identificati nella build")
    gpu_object = build / gpu_objects[0]
    if not gpu_object.exists():
        raise RuntimeError("Compilare prima forgecad_cuda_tests")
    args = [str(gpu_object) if arg == stub[0] else arg for arg in args if not arg.startswith("-Wl,--dependency-file=")]
    args[args.index("-o") + 1] = str(destination)
    args += [arg for arg in cuda_args if arg.startswith("-L") or arg in ["-lcudadevrt", "-lcudart_static", "-lcudart", "-lrt", "-lpthread", "-ldl"]]
    subprocess.run(args, cwd=build, check=True)
    return destination


def compare_runs(reference, candidate, position_tolerance, normal_tolerance):
    ar, br = reference["data"], candidate["data"]
    a = {body["index"]: body for body in ar["bodies"]}
    b = {body["index"]: body for body in br["bodies"]}
    result = dict(reference=reference["name"], candidate=candidate["name"], bodies=[])
    counts_equal = all(ar[key] == br[key] for key in ["features", "sketches", "valid_bodies", "visible_features", "visible_triangles"])
    result["counts_equal"] = counts_equal
    passed = counts_equal and a.keys() == b.keys()
    for index in sorted(a.keys() & b.keys()):
        x, y = a[index], b[index]
        metadata = all(x.get(key) == y.get(key) for key in ["drawn", "error", "brep_sha256", "triangles"])
        row = dict(index=index, metadata_equal=metadata, mesh_hash_equal=x.get("mesh_sha256") == y.get("mesh_sha256"))
        if x["drawn"] and y["drawn"]:
            if x.get("has_instanced_base") or y.get("has_instanced_base"):
                raise ValueError("Base instanziata annidata: questo confronto non la copre")
            left, right = Path(x["numeric_snapshot"]), Path(y["numeric_snapshot"])
            row["snapshot_hash_equal"] = sha256(left) == sha256(right)
            row["numeric"] = compare_snapshots(SnapshotReader(left.read_bytes()).read(), SnapshotReader(right.read_bytes()).read(),
                                                position_tolerance, normal_tolerance)
            metadata = metadata and row["numeric"]["passed"]
        row["passed"] = metadata
        passed = passed and metadata
        result["bodies"].append(row)
    result["passed"] = passed
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("document", type=Path)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cuda-binary", type=Path)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--specialized", action="store_true")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--position-tolerance", type=float, default=1e-5, help="mm, solo soglia del confronto diagnostico")
    parser.add_argument("--normal-tolerance", type=float, default=1e-5, help="distanza euclidea tra normali")
    args = parser.parse_args()
    if args.repeats < 1 or args.timeout < 1 or any(not math.isfinite(v) or v <= 0 for v in [args.position_tolerance, args.normal_tolerance]):
        parser.error("Ripetizioni, timeout e tolleranze devono essere positivi e finiti")
    document, build, output = args.document.resolve(), args.build_dir.resolve(), args.output.resolve()
    digest = sha256(document)
    output.mkdir(parents=True, exist_ok=False)
    cpu = build / "forgecad_view_tests"
    cuda = args.cuda_binary.resolve() if args.cuda_binary else link_cuda_probe(build, output / "view-cuda")
    root = Path(__file__).resolve().parent.parent
    report = dict(document_sha256=digest, platform=platform.platform(), runs=[], comparisons=[],
        position_tolerance_mm=args.position_tolerance, normal_tolerance=args.normal_tolerance,
        protocol="Processi nuovi sequenziali, qualità1 e rigenerazione completa. Confronto indicizzato con mappa facce/topologia uguale, snapshot fuori timer; non misura framerate o distanza continua delle superfici.")
    report["binary_sha256"] = dict(cpu=sha256(cpu), cuda=sha256(cuda))
    report["sources_sha256"] = {name: sha256(root / name) for name in [
        "tests/test_viewport.cpp", "tests/test_lazy_mesh.h", "cuda_support.cu", "cad_cuda_tessellation.cpp",
        "tools/compare_document_mesh_backends.py"]}
    modes = ["cpu", "cuda-generic"] + (["cuda-specialized"] if args.specialized else [])
    reference = None
    try:
        for repeat in range(args.repeats):
            for mode in modes[repeat % len(modes):] + modes[:repeat % len(modes)]:
                name = f"{repeat}-{mode}"
                directory = output / name
                directory.mkdir()
                env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_OFFSCREEN_NO_GLX="1", QT_QPA_PLATFORMTHEME="", QT_STYLE_OVERRIDE="Fusion",
                    FORGECAD_SOLVER_DENSE="0", FORGECAD_EAGER_MESH="0", FORGECAD_GPU_BROAD_PHASE="0",
                    FORGECAD_CUDA_GENERIC="0" if mode == "cuda-specialized" else "1", FORGECAD_CUDA_SPECIALIZED="1" if mode == "cuda-specialized" else "0")
                env.pop("FORGECAD_PROFILE", None)
                env.pop("FORGECAD_PROFILE_READ", None)
                command = [str(cpu if mode == "cpu" else cuda), "--document-mesh-benchmark", str(document), "--rebuild",
                           "--mesh-snapshot-dir", str(directory)]
                if mode != "cpu":
                    command.append("--require-cuda")
                start = time.monotonic()
                with (directory / "stderr.log").open("w") as errors:
                    child = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=errors, text=True, start_new_session=True)
                    try:
                        stdout, _ = child.communicate(timeout=args.timeout)
                    except BaseException:
                        os.killpg(child.pid, signal.SIGKILL)
                        child.communicate()
                        raise
                (directory / "result.json").write_text(stdout)
                if child.returncode:
                    raise RuntimeError(f"{name}: codice {child.returncode}; vedere stderr.log (77 = CUDA assente)")
                data = json.loads(stdout)
                if mode != "cpu" and (not data["cuda_available"] or data["cuda_points"] <= 0):
                    raise RuntimeError("Il confronto non ha esercitato la GPU")
                if mode == "cpu" and data["cuda_points"] != 0:
                    raise RuntimeError("Il riferimento CPU ha usato CUDA")
                run = dict(name=name, mode=mode, wall_seconds=time.monotonic()-start, data=data)
                report["runs"].append(run)
                if reference is None:
                    reference = run
                else:
                    report["comparisons"].append(compare_runs(reference, run, args.position_tolerance, args.normal_tolerance))
                report["input_unchanged"] = sha256(document) == digest
                (output / "report.json").write_text(json.dumps(report, indent=2)+"\n")
                print(f"{name}: {data['visible_triangles']} triangoli, {data['cuda_points']} punti CUDA", flush=True)
        report["passed"] = all(item["passed"] for item in report["comparisons"])
        if not report["passed"]:
            raise RuntimeError("Differenze oltre soglia o topologia diversa: consultare il rapporto numerico")
    except BaseException as error:
        report["error"] = str(error)
        report["passed"] = False
        raise
    finally:
        report["input_unchanged"] = sha256(document) == digest
        (output / "report.json").write_text(json.dumps(report, indent=2)+"\n")
        if not report["input_unchanged"]:
            raise RuntimeError("Il documento originale e' cambiato durante il confronto")
    print("PASS confronto numerico e integrità del documento")


if __name__ == "__main__":
    main()
