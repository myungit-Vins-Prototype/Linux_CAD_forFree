#!/usr/bin/env python3
"""Regressione documenti reali; originali verificati con SHA256 e mai salvati.
Il runner usa le API della storyboard, massimo quattro corpi logici.
JSON dettagliati e copie rigenerate vanno soltanto nella directory di output.
"""
import argparse, concurrent.futures, hashlib, json, os, pathlib, subprocess, time

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''): h.update(chunk)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=pathlib.Path);p.add_argument('output',type=pathlib.Path)
    p.add_argument('--runner',type=pathlib.Path,required=True)
    p.add_argument('--phase',choices=['inventory','baseline','variants','all'],default='all')
    p.add_argument('--file',action='append',default=[],help='Limita ai nomi indicati, ripetibile')
    p.add_argument('--timeout',type=int,default=300);p.add_argument('--workers',type=int,default=2)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    files=sorted(x for x in a.directory.rglob('*') if x.is_file() and x.suffix.lower() in {'.prt','.step','.stp','.iges','.igs'})
    if a.file: files=[f for f in files if f.name in a.file]
    if not files: p.error('Nessun documento trovato con il filtro richiesto')
    identities={str(f):digest(f) for f in files}
    if a.phase=='variants' and (a.output/'manifest.json').exists():
        identities=json.loads((a.output/'manifest.json').read_text()).get('original_sha256',identities)
    env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='',QT_STYLE_OVERRIDE='Fusion')
    records=[]
    runner_hash=digest(a.runner)
    def execute(item,scenario,source=None):
        number=files.index(item);stem=f'{number:02d}-{item.stem}-{scenario}'
        output=a.output/(stem+'.json');log=a.output/(stem+'.log');started=time.monotonic()
        try:
            with log.open('w') as stream:
                run=subprocess.run([str(a.runner),'--document-regression',scenario,str(source or item),str(output)],
                    env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=a.timeout)
            row=json.loads(output.read_text()) if output.exists() else {}
            row['process_return_code']=run.returncode
            if run.returncode: row['status']='crash' if run.returncode<0 else 'process_error'
        except subprocess.TimeoutExpired:
            row=json.loads(output.read_text()) if output.exists() else {};row['status']='timeout'
        row.update(original=str(item),scenario=scenario,seconds=round(time.monotonic()-started,3),output=str(output),log=str(log))
        output.write_text(json.dumps(row,ensure_ascii=False,indent=2)+'\n')
        print(f'{scenario}: {item.name}: {row.get("status")} ({row["seconds"]} s)',flush=True)
        return row
    def pool(jobs):
        result=[]
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as workers:
            futures=[workers.submit(execute,*job) for job in jobs]
            for future in concurrent.futures.as_completed(futures):
                result.append(future.result());records.append(result[-1]);write_manifest()
        return result
    def write_manifest():
        # I dettagli dei nomi restano nei JSON dei singoli casi, senza
        # duplicarli centinaia di volte nell'indice del corpus.
        def concise(row):
            result={k:v for k,v in row.items() if k not in {'before','after','repeat','restored','roundtrip','cases'}}
            for stage in {'before','after','repeat','restored','roundtrip'}:
                if stage in row: result[stage+'_summary']={k:row[stage].get(k) for k in {'feature_errors','invalid_shapes','incomplete_metrics'}}
            if 'cases' in row:result['cases']=[concise(case) for case in row['cases']]
            return result
        (a.output/'manifest.json').write_text(json.dumps({'source':str(a.directory),'max_logical_bodies':4,
            'parameter_factor':[0.999,1.001],'timeout_seconds':a.timeout,'records':[concise(row) for row in records],
            'original_sha256':identities,'runner_sha256':runner_hash},ensure_ascii=False,indent=2)+'\n')
    if a.phase in {'all','inventory','baseline'}:
        inventory=pool([(f,'inventory') for f in files])
    else:
        inventory=[]
        for file in a.output.glob('*-inventory.json'):inventory.append(json.loads(file.read_text()))
        records.extend(inventory)
    eligible=[pathlib.Path(r['original']) for r in inventory if r.get('status')=='eligible']
    if a.phase in {'all','baseline'}:baselines=pool([(f,'baseline') for f in eligible])
    else:
        baselines=[]
        for file in a.output.glob('*-baseline.json'):baselines.append(json.loads(file.read_text()))
        records.extend(baselines)
    if a.phase in {'all','variants'}:
        # Le sei varianti di ciascun documento condividono solo la cache di
        # integrali/checkBody per B-rep identici; nomi e audit sempre ricalcolati.
        jobs=[(pathlib.Path(row['original']),'variants',pathlib.Path(row['rebuilt_document'])) for row in baselines
            if row.get('rebuilt_document') and pathlib.Path(row['rebuilt_document']).exists() and row.get('process_return_code')==0]
        pool(jobs)
    changed=[str(f) for f in files if digest(f)!=identities[str(f)]]
    write_manifest()
    (a.output/'integrity.json').write_text(json.dumps({'originals_unchanged':not changed,'changed':changed,'sha256':identities},indent=2)+'\n')
    print(f'Originali invariati: {not changed}. Record: {len(records)}',flush=True)
    return 1 if changed else 0
if __name__=='__main__':raise SystemExit(main())
