#!/usr/bin/env python3
"""Apre la mappa nel browser e la aggiorna a ogni apertura della pagina.

python3 tools/open_architecture.py [--build-dir build-linux] [--no-browser]
Il server ascolta solo su loopback. Ctrl+C lo chiude; nessun servizio installato.
"""
from __future__ import annotations
import argparse
from collections import deque
import json
import os
from pathlib import Path
import secrets
import signal
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit
import webbrowser

ROOT = Path(__file__).resolve().parents[1]
LOCAL_CONFIG = ROOT / '.forgecad-architecture.json'


def find_build(explicit):
    candidates = []
    if explicit:
        candidates = [explicit.expanduser().resolve()]
    else:
        try:candidates.append(Path(json.loads(LOCAL_CONFIG.read_text())['buildDir']))
        except (OSError, ValueError, KeyError):pass
        candidates.extend(ROOT / name for name in ('build-linux', 'build-macos', 'build'))
        candidates.extend(sorted(ROOT.glob('build-*')))
    for path in candidates:
        if (path / 'compile_commands.json').is_file():return path.resolve()
    raise ValueError('Build non trovata. Usare --build-dir /percorso/build con compile_commands.json. '
                     'Configurare CMake e preparare gli header generati prima della prima analisi.')


class Updater:
    def __init__(self, build):
        self.build = build
        self.lock = threading.Lock()
        self.phase = 'idle'
        self.lines = deque(maxlen=160)
        self.process = None
        self.thread = None
        self.stopping = False

    def start(self):
        with self.lock:
            if self.phase == 'running' or self.stopping:return
            self.phase = 'running'
            self.lines.clear()
            self.thread = threading.Thread(target=self.run, daemon=True)
            self.thread.start()

    def run(self):
        try:
            command = [sys.executable, str(ROOT / 'tools/generate_architecture.py'),
                       '--update', '--build-dir', str(self.build), '--jobs', '1']
            with self.lock:
                if self.stopping:return
                self.process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, bufsize=1, start_new_session=True)
                process = self.process
            for line in process.stdout:
                with self.lock:self.lines.append(line.rstrip())
            result = process.wait()
            with self.lock:
                self.phase = 'ready' if result == 0 else 'error'
                self.process = None
        except Exception as error:
            with self.lock:
                self.phase = 'error'
                self.lines.append(str(error))

    def status(self):
        with self.lock:return {'phase': self.phase, 'log': '\n'.join(self.lines)}

    def stop(self):
        with self.lock:
            self.stopping = True
            process = self.process
        if process and process.poll() is None:
            try:os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:pass
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:pass
        if self.thread:self.thread.join(timeout=6)


LANDING = '''<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>ForgeCAD · Apertura architettura</title>
<style>body{margin:0;font:15px system-ui;background:#f3f6f9;color:#142d43}main{max-width:900px;margin:8vh auto;padding:28px;background:white;border:1px solid #ccd9e3;border-radius:10px}h1{font-size:26px}p{line-height:1.6}pre{white-space:pre-wrap;overflow-wrap:anywhere;max-height:45vh;overflow:auto;background:#142d43;color:#e1edf5;padding:18px;border-radius:6px;font-size:12px}button{padding:10px 16px;background:#1e5a9b;color:white;border:0;border-radius:5px;cursor:pointer}button:disabled{opacity:.5}iframe{display:none;border:0;width:100%;height:100vh}body.ready main{display:none}body.ready iframe{display:block}</style></head>
<body><main><h1>Mappa dell’architettura ForgeCAD</h1><p id="status" role="status" aria-live="polite">Controllo dei sorgenti e aggiornamento dello schema…</p><p>Il controllo parte a ogni apertura o ricaricamento di questa pagina. Se i sorgenti sono invariati viene riutilizzata la mappa; la prima analisi può richiedere alcuni minuti.</p><pre id="log" aria-label="Avanzamento dell’analisi"></pre><button id="retry" disabled>Riprova</button></main><iframe title="Architettura aggiornata di ForgeCAD"></iframe>
<script>
const statusNode=document.getElementById('status'),log=document.getElementById('log'),retry=document.getElementById('retry');
async function poll(){try{const response=await fetch('api/status',{cache:'no-store'});if(!response.ok)throw Error('Server locale non disponibile');const data=await response.json();log.textContent=data.log;log.scrollTop=log.scrollHeight;if(data.phase==='ready'){statusNode.textContent='Schema aggiornato.';document.querySelector('iframe').src='docs/architecture/index.html';document.body.className='ready';return}if(data.phase==='error'){statusNode.textContent='Aggiornamento non riuscito. La vista precedente è conservata; correggere l’errore indicato e riprovare.';retry.disabled=false;return}setTimeout(poll,700)}catch(error){statusNode.textContent=error.message;retry.disabled=false}}
async function start(){retry.disabled=true;statusNode.textContent='Controllo dei sorgenti e aggiornamento dello schema…';try{const response=await fetch('api/open',{method:'POST'});if(!response.ok)throw Error('Avvio dell’analisi non riuscito');await poll()}catch(error){statusNode.textContent=error.message;retry.disabled=false}}
retry.onclick=start;start();
</script></body></html>'''


def make_handler(updater, prefix, authority):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):pass

        def route(self):
            # Nessun accesso da Host arbitrari, siti esterni o URL fuori sessione.
            if self.headers.get('Host') != authority:return None
            if self.headers.get('Sec-Fetch-Site', 'none') not in {'same-origin', 'none'}:return None
            path = urlsplit(self.path).path
            if not path.startswith(prefix):return None
            return unquote(path[len(prefix):])

        def send_content(self, data, content_type, code=200):
            self.send_response(code)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('X-Frame-Options', 'SAMEORIGIN')
            self.send_header('Referrer-Policy', 'no-referrer')
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'self'; object-src 'none'; base-uri 'none'")
            self.end_headers()
            try:self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):pass  # Scheda chiusa durante la risposta.

        def do_POST(self):
            if self.route() != 'api/open':self.send_error(404);return
            origin = self.headers.get('Origin')
            if origin and origin != 'http://' + authority:self.send_error(403);return
            updater.start()
            self.send_content(b'{}', 'application/json')

        def do_GET(self):
            path = self.route()
            if path is None:self.send_error(404);return
            if path == '':self.send_content(LANDING.encode(), 'text/html; charset=utf-8');return
            if path == 'api/status':
                self.send_content(json.dumps(updater.status()).encode(), 'application/json');return
            # Solo artefatti della mappa e sorgenti del catalogo, mai l'intero checkout.
            allowed = {'docs/architecture/index.html', 'docs/architecture/graph.json', 'docs/architecture/README.md'}
            try:allowed.update(json.loads((ROOT/'docs/architecture/graph.json').read_text())['hashes'])
            except (OSError, ValueError, KeyError):pass
            if path not in allowed:self.send_error(404);return
            target = (ROOT/path).resolve()
            if not target.is_relative_to(ROOT) or not target.is_file():self.send_error(404);return
            # I sorgenti sono testo, anche se contenessero tag HTML o JavaScript.
            content_type = 'text/html; charset=utf-8' if path=='docs/architecture/index.html' else (
                'application/json' if path.endswith('.json') else 'text/plain; charset=utf-8')
            self.send_content(target.read_bytes(), content_type)
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--no-browser', action='store_true', help='Stampa il link senza aprire il browser')
    parser.add_argument('--port', type=int, default=0, help='Porta loopback; 0 sceglie una porta libera')
    args = parser.parse_args()
    try:build = find_build(args.build_dir)
    except ValueError as error:parser.error(str(error))
    updater = Updater(build)
    prefix = '/' + secrets.token_urlsafe(24) + '/'
    server = ThreadingHTTPServer(('127.0.0.1', args.port), BaseHTTPRequestHandler)
    authority = '127.0.0.1:' + str(server.server_port)
    server.RequestHandlerClass = make_handler(updater, prefix, authority)
    url = 'http://' + authority + prefix
    print('Architettura: ' + url, flush=True)
    print('Build: ' + str(build) + '\nCtrl+C per chiudere il server locale.', flush=True)
    if not args.no_browser:
        threading.Timer(.25, webbrowser.open, args=(url,)).start()
    try:server.serve_forever(poll_interval=.3)
    except KeyboardInterrupt:pass
    finally:updater.stop();server.server_close()


if __name__ == '__main__':main()
