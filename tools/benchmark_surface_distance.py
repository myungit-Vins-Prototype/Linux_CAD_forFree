#!/usr/bin/env python3
"""Confronta il predicato di distanza con la proiezione globale, senza file CAD.

Compila la sonda C++ con la configurazione CMake corrente. La misura riguarda
query ordinate sulla stessa superficie: non e' un benchmark del CAD completo.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shlex
import statistics
import subprocess

CPP_SOURCE = r'''
#include "fk_surface_distance.h"
#include "fk_surface_algo.h"
#include "fk_bspline_surface.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
using namespace ForgeCad::Kernel;
static BSplineSurface makeSurface(int spans,bool rational,bool curved) {
 const int p=3,nu=p+spans,nv=p+spans;std::vector<double>k(p+1,0);for(int i=1;i<spans;++i)k.push_back(double(i)/spans);k.insert(k.end(),p+1,1);std::vector<Vec3>poles;std::vector<double>w;
 for(int i=0;i<nu;++i)for(int j=0;j<nv;++j){poles.push_back(Vec3(i*.5,j*.5,curved?.2*std::sin(i*.4)*std::cos(j*.5):0.));if(rational)w.push_back(1+.2*std::sin(i*.3+j*.2));}
 return BSplineSurface(p,p,k,k,nu,nv,poles,w);
}
int main(int argc,char**argv){try{int offset=argc>1?std::stoi(argv[1]):0;std::size_t verified=0;std::cout<<"[";bool first=true;
for(int block=0;block<8;++block){int kind=(block+offset)%8;int spans=kind<2?1:5;auto surface=makeSurface(spans,kind%2,kind>=2);std::vector<Vec3>points;for(int i=0;i<128;++i){Vec3 p=surface.point(.05+.9*i/127.,.43);p[2]+=(kind>=6||(kind>=4&&i%4==0))?1.0:1e-9;points.push_back(p);}std::vector<double>expect,actual(points.size());Vec2 hint(std::numeric_limits<double>::quiet_NaN(),0);std::size_t within=0;
for(const auto&p:points){auto q=projectPoint(surface,p);double e=q.distance<=1e-7?0.:q.distance;double value=surfaceDeviationAboveTolerance(surface,p,1e-7,&hint);if(std::memcmp(&e,&value,8))throw std::runtime_error("deviation query mismatch");expect.push_back(e);within+=e==0.;++verified;}
std::vector<double>times[2];volatile double sink=0;
for(int rep=0;rep<7;++rep)for(int pass=0;pass<2;++pass){bool fast=(rep%2)^pass;hint=Vec2(std::numeric_limits<double>::quiet_NaN(),0);auto start=std::chrono::steady_clock::now();for(std::size_t i=0;i<points.size();++i){if(fast)actual[i]=surfaceDeviationAboveTolerance(surface,points[i],1e-7,&hint);else{const auto q=projectPoint(surface,points[i]);actual[i]=q.distance<=1e-7?0.:q.distance;}}auto end=std::chrono::steady_clock::now();times[fast].push_back(std::chrono::duration<double,std::milli>(end-start).count());for(std::size_t i=0;i<actual.size();++i){if(std::memcmp(&actual[i],&expect[i],8))throw std::runtime_error("timed deviation mismatch");sink+=actual[i];}}
for(auto&v:times) { std::sort(v.begin(),v.end()); }
(void)sink;
if(!first)std::cout<<",";first=false;std::cout<<"{\"case\":"<<kind<<",\"spans_per_axis\":"<<spans<<",\"rational\":"<<(kind%2?"true":"false")<<",\"curved\":"<<(kind>=2?"true":"false")<<",\"queries\":"<<points.size()<<",\"within\":"<<within<<",\"before_ms\":"<<times[0][3]<<",\"after_ms\":"<<times[1][3]<<",\"speedup\":"<<times[0][3]/times[1][3]<<",\"before_samples_ms\":[";for(int i=0;i<7;++i)std::cout<<(i?",":"")<<times[0][i];std::cout<<"],\"after_samples_ms\":[";for(int i=0;i<7;++i)std::cout<<(i?",":"")<<times[1][i];std::cout<<"]}";}
std::cout<<"]\n";std::cerr<<"PASS "<<verified<<" query results; 8workloads, 7alternating samples/mode, validation outside timing\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
'''

def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--processes", type=int, default=3)
    args = parser.parse_args()
    if args.processes < 1:
        parser.error("--processes deve essere positivo")
    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
    output = args.output.resolve()
    database = json.loads((build / "compile_commands.json").read_text())
    source_file = root / "kernel/fk_surface_distance.cpp"
    entry = next(x for x in database if Path(x["file"]).resolve() == source_file)
    library = build / "kernel/libforgekernel.a"
    if not library.is_file():
        parser.error("Compilare prima il target forgekernel nella build indicata")
    output.mkdir(parents=True, exist_ok=False)
    source = output / "query.cpp"
    obj = output / "query.o"
    binary = output / "query"
    source.write_text(CPP_SOURCE)
    command = shlex.split(entry["command"])
    command[command.index(entry["file"])] = str(source)
    command[command.index("-o") + 1] = str(obj)
    with (output / "build.log").open("w") as log:
        subprocess.run(command, cwd=entry["directory"], stdout=log, stderr=subprocess.STDOUT, check=True)
        # Native builds: use the same compiler, preserving architecture/SDK flags.
        flags = []
        index = 1
        while index < len(command):
            item = command[index]
            if item in ("-arch", "-isysroot", "-target", "--sysroot"):
                flags.extend(command[index:index + 2])
                index += 2
                continue
            if item.startswith(("-stdlib=", "--target=", "--sysroot=", "-mmacosx-version-min=")):
                flags.append(item)
            index += 1
        subprocess.run([command[0], *flags, str(obj), str(library), "-pthread", "-o", str(binary)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    report = {"platform": platform.platform(), "backend": "CPU", "library_sha256": sha256(library),
              "source_sha256": sha256(source), "binary_sha256": sha256(binary),
              "protocol": "Processi nuovi sequenziali; casi ruotati, sette lotti alternati per metodo,128 query ordinate per caso; cache riscaldate e validazione fuori timer. Nessuna misura CAD completo.",
              "processes": []}
    for index in range(args.processes):
        with (output / f"process-{index}.log").open("w") as log:
            result = subprocess.run([str(binary), str(index)], stdout=subprocess.PIPE, stderr=log,
                                    text=True, check=True, timeout=180)
        data = json.loads(result.stdout)
        assert len(data) == 8 and all(x["queries"] == 128 for x in data)
        (output / f"process-{index}.json").write_text(result.stdout)
        report["processes"].append(data)
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    report["summary"] = []
    for case in range(8):
        rows = [next(x for x in process if x["case"] == case) for process in report["processes"]]
        before = statistics.median(x["before_ms"] for x in rows)
        after = statistics.median(x["after_ms"] for x in rows)
        report["summary"].append({"case": case, "before_ms": before, "after_ms": after,
                                  "speedup": before / after, "within_queries": rows[0]["within"]})
        print(f"case {case}: {before:.3f} -> {after:.3f} ms, {before / after:.2f}x", flush=True)
    assert sha256(library) == report["library_sha256"], "Libreria cambiata durante il confronto"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
