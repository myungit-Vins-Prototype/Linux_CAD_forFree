"""Regression for the libclang architecture cache, run with python3 tests/test_architecture_cache.py."""
from pathlib import Path
import concurrent.futures
import json
import shutil
import sqlite3
import subprocess
import sys
import tempfile

project = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='forgecad-cache-proof-') as tmp, tempfile.TemporaryDirectory() as sdk:
    root = Path(tmp)
    for directory in ('tools', 'docs/architecture', 'build/generated', 'build/secondary', 'src'):
        (root / directory).mkdir(parents=True)
    script = root / 'tools/generate_architecture.py'
    shutil.copyfile(project / 'tools/generate_architecture.py', script)
    shutil.copyfile(project / 'docs/architecture/template.html', root / 'docs/architecture/template.html')
    subprocess.run(['git','init','-q','-b','main'], cwd=root, check=True)
    build = root / 'build'
    legacy = build / 'architecture-cache'
    legacy.mkdir()
    marker = legacy / 'old-entry.json'
    marker.write_text('legacy data')
    (build / 'other.sqlite3').write_text('unrelated data')
    (root / 'src/a.cpp').write_text('#include "outer.h"\n#ifdef GENERATED_SIDE\nint generated_side() { return f(); }\n#endif\nint a() { return f(); }\n')
    (root / 'src/b.cpp').write_text('int independent() { return 3; }\n')
    (root / 'src/outer.h').write_text('#include "inner.hpp"\n')
    inner = root / 'src/inner.hpp'
    original = '#if __has_include(<probe>)\n#include <probe>\n#else\ninline int f() { return 0; }\n#endif\n'
    inner.write_text(original)
    files = [root / 'src' / name for name in ('a.cpp','b.cpp')]
    db = [{'directory':str(root), 'file':str(file),
           'arguments':['clang++', '-std=c++17', '-I'+str(build/'generated'),
                        '-I'+str(build/'secondary'), '-I'+sdk, '-c',str(file)]} for file in files]
    commands = build / 'compile_commands.json'
    commands.write_text(json.dumps(db))
    graph = root / 'docs/architecture/graph.json'

    def run(*args, hits=None, rc=0):
        result = subprocess.run([sys.executable, str(script), '--build-dir',str(build),
                                 '--jobs','1', *args],cwd=root,text=True,capture_output=True)
        assert result.returncode == rc, (result.stdout,result.stderr)
        if hits is not None:
            assert f'{hits} unità da cache, ' in result.stdout, (args,hits,result.stdout,result.stderr)
        return result.stdout

    def same_as_clean():
        snapshot = graph.read_bytes()
        run('--no-cache', hits=0)
        assert snapshot == graph.read_bytes()

    run(hits=0)
    run('--update', hits=2)
    inner.write_text(original.replace('return 0;', 'return 13;'))
    run(hits=1)
    same_as_clean()
    run(hits=2)
    probe = build / 'secondary/probe'
    probe.write_text('#pragma once\ninline int g() { return 22; }\ninline int f() { return g(); }\n')
    run('--update', hits=0)
    same_as_clean()
    probe.write_text('#define GENERATED_SIDE\n' + probe.read_text())
    run('--update', hits=1)
    same_as_clean()
    unit = next(n for n in json.loads(graph.read_text())['nodes'] if n['id'] == 'src/a.cpp')
    assert 'generated_side' in unit['symbols']
    shadow = build / 'generated/probe'
    shadow.write_text('inline int f() { return 123; }\n')
    run(hits=0)
    same_as_clean()
    unit = next(n for n in json.loads(graph.read_text())['nodes'] if n['id'] == 'src/a.cpp')
    assert 'generated_side' not in unit['symbols']
    shadow.unlink()
    probe.unlink()
    run(hits=0)
    same_as_clean()
    inner.write_text(original.replace('#if __has_include(<probe>)', '#if 1'))
    snapshot = graph.read_bytes()
    run(rc=1, hits=1)
    assert snapshot == graph.read_bytes()
    inner.write_text(original)
    run(hits=1)
    same_as_clean()
    cache, = build.glob('.forgecad-architecture-*.sqlite3')
    with sqlite3.connect(cache) as conn:
        assert conn.execute('SELECT COUNT(*) FROM units').fetchone() == (2,)
        conn.execute("UPDATE units SET payload = 'corrupt' WHERE filename = 'src/a.cpp'")
    run(hits=1)
    same_as_clean()
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        one = pool.submit(run, '--rebuild-cache', hits=0)
        two = pool.submit(run, hits=2)
        one.result()
        two.result()
    commands.write_text(json.dumps(db[:-1]))
    run(hits=1)
    with sqlite3.connect(cache) as conn:
        assert conn.execute('SELECT filename FROM units').fetchall() == [('src/a.cpp',)]
    # SDK outside the checkout. An explicit rebuild must retire the old row even
    # when the newly resolved input depends on the time and cannot be cached.
    (Path(sdk) / 'probe').write_text('#define GENERATED_SIDE\ninline int f() { return __TIME__[0]; }\n')
    run('--rebuild-cache', hits=0)
    with sqlite3.connect(cache) as conn:
        assert conn.execute('SELECT COUNT(*) FROM units').fetchone() == (0,)
    run('--update', hits=0)
    same_as_clean()
    assert marker.read_text() == 'legacy data'
    assert (build/'other.sqlite3').read_text() == 'unrelated data'
    print('PASS: warm, transitive edit, clean diff, optional generated, shadowing, missing, diagnostics, corruption, concurrent, stale slots')
