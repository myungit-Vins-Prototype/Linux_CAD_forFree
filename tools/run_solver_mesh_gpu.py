#!/usr/bin/env python3
"""Avvia la build sperimentale con opzioni riproducibili, senza cambiare preferenze."""
import argparse
import os
from pathlib import Path

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
mode = parser.add_mutually_exclusive_group()
mode.add_argument("--baseline", action="store_true", help="solver denso e mesh immediate, GPU booleane disattivata")
mode.add_argument("--gpu", action="store_true", help="solver/mesh ottimizzati e filtro GPU delle booleane")
parser.add_argument("--profile", action="store_true", help="stampa tempi e backend sul terminale")
parser.add_argument("--build-dir", type=Path, default=root / "forgecad-release")
parser.add_argument("document", nargs="?", type=Path)
args = parser.parse_args()
binary = args.build_dir.resolve() / "forgecad"
if not binary.is_file():
    parser.error(f"Eseguibile assente: {binary}; compilare il target forgecad")
os.environ["FORGECAD_SOLVER_DENSE"] = "1" if args.baseline else "0"
os.environ["FORGECAD_EAGER_MESH"] = "1" if args.baseline else "0"
os.environ["FORGECAD_GPU_BROAD_PHASE"] = "1" if args.gpu else "0"
if args.profile:
    os.environ["FORGECAD_PROFILE"] = "1"
command = [str(binary)]
if args.document:
    command.append(str(args.document.resolve()))
os.execv(str(binary), command)
