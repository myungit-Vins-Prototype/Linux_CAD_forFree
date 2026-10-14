#!/usr/bin/env python3
"""Produce un rapporto HTML dagli snapshot JSON della regressione reale.
Si possono passare directory successive: le ripetizioni piu' recenti prevalgono.
"""
import argparse, collections, html, json, pathlib

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories',type=pathlib.Path,nargs='+')
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args();rows={};inventories={};integrity=[]
    for directory in args.directories:
        for file in directory.glob('*-inventory.json'):
            row=json.loads(file.read_text());inventories[row.get('original',row.get('source'))]=row
        for suffix in ('baseline','variants'):
            for file in directory.glob('*-'+suffix+'.json'):
                row=json.loads(file.read_text());source=row.get('original',row.get('source',''))
                if suffix=='variants' and not row.get('original'):
                    # Le copie rigenerate sono sempre identificate dal baseline.
                    continue
                rows.setdefault(source,{})[suffix]=(row,file)
        if (directory/'integrity.json').exists():
            integrity.append(json.loads((directory/'integrity.json').read_text()))
    def esc(value): return html.escape(str(value))
    def link(file): return '<a href="'+esc(file.as_uri())+'">JSON dettagliato</a>'
    content=['<!doctype html><html lang="it"><meta charset="utf-8"><title>Regressione storyboard</title>',
        '<style>body{font:16px system-ui;margin:2rem;color:#172333}table{border-collapse:collapse;width:100%}td,th{border:1px solid #b9c5d0;padding:.5rem;text-align:left;vertical-align:top}small{color:#425466}pre{white-space:pre-wrap}</style>',
        '<h1>Regressione parametrica storyboard</h1>',
        '<p>Massimo quattro corpi logici. Parametri iniziale/finale ±0,1%, riordino, soppressione e ripristino. Tolleranza relativa volumi 10⁻⁶. Le differenze delle varianti sono attese; instabile indica differenze alla ripetizione o al ripristino. I limiti degli integrali non sono verifiche superate.</p>',
        '<p>Originali invariati: '+esc(bool(integrity) and all(x.get('originals_unchanged') for x in integrity))+'.</p>',
        '<table><tr><th>Documento / corpi</th><th>Rigenerazione</th><th>Facce e volume dei risultati</th><th>Varianti</th></tr>']
    counts=collections.Counter();total=0
    for source,record in sorted(rows.items()):
        if 'baseline' not in record: continue
        base,file=record['baseline'];stage=base.get('roundtrip',base.get('repeat',{}))
        features={x['id']:x for x in stage.get('features',[])}
        tips=[]
        for body in stage.get('bodies',[]):
            tip=features.get(str(body.get('tip_feature_id')),{});tips.append(esc(body.get('name',body.get('id','')))+': '+esc(tip.get('faces','—'))+' facce; '+esc(tip.get('volume','—'))+' mm³')
        variants=[]
        if 'variants' in record:
            var,vfile=record['variants']
            for case in var.get('cases',[]):
                counts[case.get('status','unknown')]+=1;total+=1
                detail=case.get('after',{})
                variants.append(esc(case.get('scenario'))+': <b>'+esc(case.get('status'))+'</b> ('+esc(detail.get('feature_errors','—'))+' errori, '+esc(detail.get('incomplete_metrics','—'))+' misure incomplete)')
            variants.append(link(vfile))
        info=esc(base.get('status'))+'; '+esc(stage.get('feature_errors','—'))+' errori; '+esc(stage.get('incomplete_metrics','—'))+' misure incomplete; differenze snapshot '+esc(len(base.get('snapshot_changes',[])))
        if 'roundtrip_changes' in base: info+='; differenze riapertura '+esc(len(base['roundtrip_changes']))
        content.append('<tr><td>'+esc(pathlib.Path(source).name)+' / '+esc(base.get('bodies'))+'</td><td>'+info+'<br>'+link(file)+'</td><td>'+'<br>'.join(tips)+'</td><td>'+'<br>'.join(variants)+'</td></tr>')
    content.extend(['</table><h2>Esiti delle varianti ('+str(total)+')</h2><pre>'+esc(json.dumps(counts,indent=2))+'</pre><h2>Documenti esclusi</h2><ul>'])
    for source,row in sorted(inventories.items()):
        if row.get('status')!='eligible':content.append('<li>'+esc(pathlib.Path(source).name)+': '+esc(row.get('status'))+'; corpi/definizioni solide '+esc(row.get('bodies',row.get('solid_definition_count','—')) )+'</li>')
    content.append('</ul><p>I nomi completi, i volumi per feature, gli avvisi e le differenze sono conservati nei JSON collegati. Le superfici aperte hanno area e volume nullo. Eventuali errori geometrici restano espliciti anche quando il ripristino e’ stabile.</p></html>')
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text('\n'.join(content))
    print(json.dumps({'variant_cases':total,'statuses':dict(counts),'report':str(args.output)}))
if __name__=='__main__':main()
