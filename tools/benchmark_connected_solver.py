#!/usr/bin/env python3
"""Misure sequenziali del solver connesso, con verifica numerica fuori timer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import statistics
import subprocess


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--previous-runner', type=Path, required=True,
                        help='Runner con la stessa fixture e il solo oggetto cad_constraints precedente')
    parser.add_argument('--document', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--processes', type=int, default=3)
    args = parser.parse_args()
    if args.processes < 1:
        parser.error('--processes deve essere positivo')
    root = Path(__file__).resolve().parent.parent
    binaries = {'previous': args.previous_runner.resolve(),
                'current': args.build_dir.resolve() / 'forgecad_view_tests',
                'algebra': args.build_dir.resolve() / 'forgecad_constraint_linear_tests'}
    document = args.document.resolve() if args.document else None
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_OFFSCREEN_NO_GLX='1')
    for key in ('FORGECAD_PROFILE', 'FORGECAD_PROFILE_READ', 'FORGECAD_SOLVER_DENSE'):
        env.pop(key, None)
    report = {'platform': platform.platform(), 'runs': [],
              'binary_sha256': {name: sha(path) for name, path in binaries.items()},
              'sources_sha256': {name: sha(root / name) for name in (
                  'kernel/fk_constraint_linear.h', 'cad_constraints.cpp',
                  'tests/test_constraint_linear.cpp', 'tests/test_solver_partition.h',
                  'tests/test_viewport.cpp', 'tools/benchmark_connected_solver.py')},
              'protocol': 'Processi nuovi sequenziali, ordine prima/dopo alternato; '
              '5 copie e un warmup per catena, algebra denso/banda alternata nello stesso processo; '
              'verifiche fuori timer. Nessun controllo clock/temperatura/carico esterno. '
              'Il riferimento e il precedente solver partizionato, non main.'}
    if document:
        report['document_sha256'] = sha(document)

    def run(name, binary, arguments):
        with (output / (name + '.stderr.log')).open('w') as errors:
            child = subprocess.Popen([str(binary), *arguments], env=env, stdout=subprocess.PIPE,
                                     stderr=errors, text=True, start_new_session=True)
            try:
                stdout, _ = child.communicate(timeout=180)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.communicate()
                raise RuntimeError(f'Timeout: {name}')
        (output / (name + '.json')).write_text(stdout)
        if child.returncode:
            raise RuntimeError(f'{name}: codice {child.returncode}; consultare stderr')
        return json.loads(stdout)

    try:
        for repeat in range(args.processes):
            data = {'algebra': run(f'{repeat}-algebra', binaries['algebra'], ['--benchmark'])}
            order = ['previous', 'current'] if repeat % 2 == 0 else ['current', 'previous']
            for mode in order:
                data[mode] = run(f'{repeat}-{mode}', binaries[mode], ['--connected-solver-benchmark'])
                if document:
                    data[mode + '_document'] = run(f'{repeat}-{mode}-document', binaries[mode],
                        ['--document-solver-benchmark', str(document), '--admissible-targets'])
            previous, current = data['previous']['cases'], data['current']['cases']
            if [row['segments'] for row in previous] != [row['segments'] for row in current]:
                raise RuntimeError('Catene diverse tra i runner')
            for a, b in zip(previous, current):
                if len(a['geometry']) != len(b['geometry']):
                    raise RuntimeError('Numero segmenti cambiato')
                delta = max(abs(x-y) for p, q in zip(a['geometry'], b['geometry']) for x, y in zip(p, q))
                if delta > 1e-7:
                    raise RuntimeError(f'Geometria prima/dopo diversa: {delta}')
                b['max_coordinate_delta_previous'] = delta
            if document:
                def targets(mode):
                    return [(row['index'], row['target']) for row in data[mode + '_document']['sketches'] if row['selected']]
                if not targets('previous') or targets('previous') != targets('current'):
                    raise RuntimeError('Target reali assenti o diversi')
            report['runs'].append(data)
            print(f'{repeat+1}/{args.processes}: catene e geometria verificate', flush=True)
        report['connected_summary'] = []
        for index, row in enumerate(report['runs'][0]['current']['cases']):
            summary = {'segments': row['segments']}
            for mode in ('previous', 'current'):
                values = [run[mode]['cases'][index]['median_ms'] for run in report['runs']]
                summary[mode + '_ms'] = dict(median=statistics.median(values), min=min(values), max=max(values))
            summary['speedup'] = summary['previous_ms']['median'] / summary['current_ms']['median']
            report['connected_summary'].append(summary)
        if document:
            report['document_summary'] = {}
            for mode in ('previous', 'current'):
                for key in ('partition_solve_ms', 'partition_analysis_ms'):
                    values = [run[mode + '_document'][key] for run in report['runs']]
                    report['document_summary'][mode + '_' + key] = dict(median=statistics.median(values), min=min(values), max=max(values))
    finally:
        report['input_unchanged'] = not document or sha(document) == report['document_sha256']
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        if not report['input_unchanged']:
            raise RuntimeError('Il documento originale e cambiato')
    print(json.dumps({key: report[key] for key in ('connected_summary', 'document_summary') if key in report}, indent=2))


if __name__ == '__main__':
    main()
