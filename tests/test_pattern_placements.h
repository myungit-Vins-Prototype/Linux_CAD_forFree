#pragma once
#include "../cad_pattern.h"
#include "../cad_document_io.h"
#include <limits>

namespace {
void patternPlacementTests() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    const auto grid = [](int first, int second = 1) {
        PatternParameters p; p.kind=0; p.count=first; p.count2=second; p.spacing=2; p.spacing2=3;
        GeometryRef x; x.kind=2; x.index=0;
        GeometryRef y=x; y.index=1; p.refs={x,y}; return p;
    };
    const auto reject = [](const PatternParameters &p, const QString &expected) {
        std::vector<Transform3> placements; QString error;
        require(!patternPlacements(p,0,{}, {},placements,&error) && error==expected, "conteggi non validi rifiutati con errore coerente");
        require(placements.empty() && placements.capacity()==0, "rifiuto prima di allocare o generare trasformazioni");
        placements.resize(400); const auto capacity=placements.capacity();
        require(!patternPlacements(p,0,{}, {},placements,nullptr) && placements.empty()
                && placements.capacity()==capacity, "output precedente svuotato senza nuove allocazioni su errore");
    };
    const auto tooMany = QStringLiteral("Troppe istanze (al massimo 500).");
    const auto tooFew = QStringLiteral("Servono almeno due istanze.");
    // La regressione della review: il codice precedente genera prima 249.999
    // trasformazioni, poi restituisce false lasciandole nell'output.
    reject(grid(500,500),tooMany);
    for (const auto &p : {grid(502),grid(2,251),grid(65536,65536),grid(std::numeric_limits<int>::max(),2),
                         grid(std::numeric_limits<int>::max(),std::numeric_limits<int>::max())}) reject(p,tooMany);
    for (const auto &p : {grid(1),grid(0,500),grid(500,0),grid(-1),grid(1,-1),
                         grid(std::numeric_limits<int>::min(),std::numeric_limits<int>::min())}) reject(p,tooFew);
    auto missing=grid(500,500); missing.refs.clear(); reject(missing,tooMany);
    // Conserva il contratto esistente: massimo 500 trasformazioni oltre
    // all'originale, quindi 501 posizioni totali sono ancora ammesse.
    for (const auto &p : {grid(2),grid(500),grid(501),grid(5,100),grid(3,167)}) {
        std::vector<Transform3> placements; QString error;
        require(patternPlacements(p,0,{}, {},placements,&error), "ripetizione sul limite accettata");
        require(placements.size()==std::size_t(p.count)*std::size_t(p.count2)-1, "numero di copie e originale invariati");
        std::size_t next=0;
        for (int j=0;j<p.count2;++j) for (int i=0;i<p.count;++i) if (i||j)
            require(distance(placements[next++].applyToPoint({1,2,3}), {1+2.*i,2+3.*j,3})<1e-12,
                    "ordine e coordinate della griglia invariati");
    }
    {
        auto p=grid(3,2); p.spacing=-2; p.flip2=true;
        std::vector<Transform3> placements; QString error;
        require(patternPlacements(p,0,{}, {},placements,&error) && placements.size()==5
                && distance(placements.back().applyToPoint({}),{-4,-3,0})<1e-12, "passo negativo e secondo verso conservati");
        p.spacing=0;
        require(!patternPlacements(p,0,{}, {},placements,&error) && placements.empty(), "passo nullo rifiutato senza output residuo");
        p.spacing=2; p.refs[1]=p.refs[0];
        require(!patternPlacements(p,0,{}, {},placements,&error) && placements.empty(), "direzioni parallele rifiutate senza output residuo");
    }
    PatternParameters circular; circular.kind=1; circular.refs={grid(2).refs[0]}; circular.refs[0].index=2;
    circular.count2=std::numeric_limits<int>::max(); // inutilizzato per la ripetizione circolare
    for (int count : {2,500,501}) {
        auto p=circular; p.count=count; std::vector<Transform3> placements; QString error;
        require(patternPlacements(p,0,{}, {},placements,&error) && placements.size()==std::size_t(count-1), "limite circolare preservato");
    }
    for (int count : {502,std::numeric_limits<int>::max()}) { auto p=circular; p.count=count; reject(p,tooMany); }
    for (int count : {1,0,-1,std::numeric_limits<int>::min()}) { auto p=circular; p.count=count; reject(p,tooFew); }
    {
        auto p=circular; p.count=4; std::vector<Transform3> placements; QString error;
        require(patternPlacements(p,0,{}, {},placements,&error) && placements.size()==3
                && distance(placements[0].applyToPoint({1,0,0}),{0,1,0})<1e-12
                && distance(placements[2].applyToPoint({1,0,0}),{0,-1,0})<1e-12, "giro completo senza duplicare l'originale");
        p.count=3; p.angle=180;
        require(patternPlacements(p,0,{}, {},placements,&error)
                && distance(placements.back().applyToPoint({1,0,0}),{-1,0,0})<1e-12, "settore include l'estremo");
        p.spread=false; p.angle=45; p.flip=true;
        require(patternPlacements(p,0,{}, {},placements,&error)
                && distance(placements.back().applyToPoint({1,0,0}),{0,-1,0})<1e-12, "passo angolare e verso conservati");
        p.angle=0;
        require(!patternPlacements(p,0,{}, {},placements,&error) && placements.empty(), "angolo nullo svuota output");
    }
    {
        auto mirror=circular; mirror.kind=2; mirror.count=std::numeric_limits<int>::min();
        mirror.refs[0].kind=1; mirror.refs[0].index=2;
        std::vector<Transform3> placements; QString error;
        require(patternPlacements(mirror,0,{}, {},placements,&error) && placements.size()==1
                && distance(placements[0].applyToPoint({2,3,4}),{-2,3,4})<1e-12,
                "simmetria non usa i conteggi delle ripetizioni");
    }
    {
        DocumentState source; ExtrusionObject base; base.feature=BodyFeature::Primitive;
        ExtrusionObject pattern; pattern.feature=BodyFeature::Pattern; pattern.firstBody=0;
        pattern.pattern=grid(std::numeric_limits<int>::max(),std::numeric_limits<int>::max());
        source.extrusions={base,pattern}; QByteArray bytes;
        require(encodeDocument(source,bytes,false).isEmpty(), "fixture con conteggi estremi codificata");
        DocumentState restored;
        require(decodeDocument(bytes,restored).isEmpty() && restored.extrusions.size()==2, "fixture con conteggi estremi riletta");
        reject(restored.extrusions[1].pattern,tooMany);
    }
    std::cout << "PASS pattern placements: limiti, overflow, output, griglie, rotazioni e simmetria" << std::endl;
}
}
