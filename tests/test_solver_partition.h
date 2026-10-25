#ifndef FORGECAD_TEST_SOLVER_PARTITION_H
#define FORGECAD_TEST_SOLVER_PARTITION_H

// Confronto differenziale sul solver intero: il percorso denso e' il riferimento
// precedente; i controlli geometrici restano indipendenti dal confronto.
static void solverPartitionTests(bool benchmark = false) {
    using namespace ForgeCad;
    const bool wasSet = qEnvironmentVariableIsSet("FORGECAD_SOLVER_DENSE");
    const QByteArray saved = qgetenv("FORGECAD_SOLVER_DENSE");
    struct Restore { bool set; QByteArray value; ~Restore() {
        if (set) qputenv("FORGECAD_SOLVER_DENSE",value); else qunsetenv("FORGECAD_SOLVER_DENSE");
    }} restore{wasSet,saved};
    const auto compare = [&](const SketchObject &source, const QVector<PointTarget> &targets = {}) {
        auto dense=source, partitioned=source;
        qputenv("FORGECAD_SOLVER_DENSE","1"); const auto reference=solveSketch(dense,targets);
        const auto referenceAnalysis=analyzeSketch(source);
        qputenv("FORGECAD_SOLVER_DENSE","0"); const auto actual=solveSketch(partitioned,targets);
        const auto actualAnalysis=analyzeSketch(source);
        require(referenceAnalysis.rank==actualAnalysis.rank
            && referenceAnalysis.degreesOfFreedom==actualAnalysis.degreesOfFreedom
            && referenceAnalysis.segmentDefined==actualAnalysis.segmentDefined
            && referenceAnalysis.curveDefined==actualAnalysis.curveDefined,"analisi per componenti: rango e geometria determinata conservati");
        require(reference.ok==actual.ok,"solver partizionato: stesso esito del riferimento");
        if (!actual.ok) {
            require(partitioned.segments==source.segments,"conflitto: modifica atomica");
            return;
        }
        for (int i=0;i<source.segments.size();++i) {
            require(QLineF(dense.segments[i].first,partitioned.segments[i].first).length()<1e-7
                && QLineF(dense.segments[i].second,partitioned.segments[i].second).length()<1e-7,
                "solver partizionato: conservata soluzione di norma minima");
        }
        for (const auto &c:partitioned.geometricConstraints)
            require(constraintError(partitioned,c)<1e-7,"vincolo finale verificato indipendentemente");
    };
    for (int count : {1,8,64,256}) {
        SketchObject source;
        for (int i=0;i<count;++i) {
            source.segments.append({{double(i)*3,0.1},{double(i)*3+2,0.2}});
            source.geometricConstraints.append(makeConstraint(source,ConstraintType::Horizontal,{{0,i,-1}}));
        }
        compare(source);
        for (int i=1;i<count;++i)
            source.geometricConstraints.append(makeConstraint(source,ConstraintType::Coincident,{{0,i-1,1},{0,i,0}}));
        compare(source);
    }
    SketchObject conflict;
    conflict.segments={{{0,0},{3,1}},{{10,0},{13,2}}};
    conflict.geometricConstraints.append(makeConstraint(conflict,ConstraintType::Horizontal,{{0,0,-1}}));
    conflict.geometricConstraints.append(makeConstraint(conflict,ConstraintType::Horizontal,{{0,1,-1}}));
    compare(conflict,{{{0,0,0},{0,0}},{{0,0,1},{3,2}}});
    // Regolarizzazione globale: righe ridondanti e componenti di scale diverse.
    auto redundant=conflict;
    redundant.geometricConstraints.append(redundant.geometricConstraints.first());
    compare(redundant);
    if(!benchmark)return;
    for (int count : {100,400,800}) {
        SketchObject source;
        for (int i=0;i<count;++i) {
            source.segments.append({{double(i)*2,0},{double(i)*2+1,0.1}});
            source.geometricConstraints.append(makeConstraint(source,ConstraintType::Horizontal,{{0,i,-1}}));
        }
        QVector<double> times[2];
        for(int repeat=0;repeat<5;++repeat) for(int pass=0;pass<2;++pass) {
            const int dense=(repeat%2)^pass;
            qputenv("FORGECAD_SOLVER_DENSE",dense?"1":"0");
            auto copy=source; QElapsedTimer timer;timer.start(); const auto result=solveSketch(copy);
            times[dense].append(timer.nsecsElapsed()/1e6);
            require(result.ok,"benchmark solver: soluzione valida");
            for(const auto &segment:copy.segments)
                require(std::abs(segment.first.y()-segment.second.y())<1e-8,"benchmark solver: orizzontalita'");
        }
        for(auto &values:times) std::sort(values.begin(),values.end());
        std::cout<<"solver independent="<<count<<" dense_ms="<<times[1][2]
                 <<" partition_ms="<<times[0][2]<<" speedup="<<times[1][2]/times[0][2]<<std::endl;
    }
}
#endif
