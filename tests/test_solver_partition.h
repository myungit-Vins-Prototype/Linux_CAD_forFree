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

// Ricerca limitata fuori timer: punti campionati delle curve non definite,
// poi segmenti. Ogni target deve riuscire su entrambi i percorsi, senza
// eliminare vincoli o accettare un punto che in realta' non si e' mosso.
struct DocumentDragCase {
    QVector<ForgeCad::PointTarget> targets;
    int attempts=0;
    bool baselineOk=false;
    QString baselineError;
};
static DocumentDragCase selectDocumentDrag(const SketchObject &source) {
    using namespace ForgeCad;
    DocumentDragCase result;
    qputenv("FORGECAD_SOLVER_DENSE","0");
    auto baseline=source;const auto initial=solveSketch(baseline);
    result.baselineOk=initial.ok;result.baselineError=initial.error;
    const auto analysis=analyzeSketch(source);
    QVector<ConstraintRef> candidates;
    const auto appendCurve=[&](int i) {
        const int count=int(source.curves[i].controlPoints.size());
        for(int sample=0;sample<std::min(8,count);++sample) {
            const int point=count<=8?sample:sample*(count-1)/7;
            candidates.append({1,i,point});
        }
    };
    for(int i=0;i<source.curves.size() && candidates.size()<48;++i)
        if(!analysis.curveDefined.value(i))appendCurve(i);
    for(int i=0;i<source.segments.size() && candidates.size()<64;++i)
        if(!analysis.segmentDefined.value(i)){candidates.append({0,i,0});candidates.append({0,i,1});}
    for(const auto &ref:candidates) {
        QPointF point;if(!refPoint(source,ref,point))continue;
        for(const auto &delta:{QPointF(.01,0),QPointF(0,.01),QPointF(.01,.02)}) {
            ++result.attempts;
            const QVector<PointTarget> targets{{ref,point+delta}};
            SketchObject work[2];SolveResult solved[2];
            for(int dense=0;dense<2;++dense) {
                qputenv("FORGECAD_SOLVER_DENSE",dense?"1":"0");
                work[dense]=source;solved[dense]=solveSketch(work[dense],targets);
            }
            require(solved[0].ok==solved[1].ok,"ricerca target: esito CPU denso/partizionato diverso");
            if(!solved[0].ok)continue;
            for(int dense=0;dense<2;++dense) {
                QPointF moved;require(refPoint(work[dense],ref,moved),"ricerca target: riferimento conservato");
                require(QLineF(moved,point+delta).length()<1e-7 && QLineF(moved,point).length()>1e-5,
                    "ricerca target: spostamento effettivamente applicato");
            }
            result.targets=targets;return result;
        }
    }
    return result;
}

// Documento reale, mai salvato: piccola modifica dello stesso punto nelle due
// varianti e analisi DOF. Copie ripristinate, cinque ripetizioni alternate.
static void documentSolverBenchmark(const QString &path,bool admissible=false) {
    using namespace ForgeCad;
    DocumentState document;
    const auto error=loadDocumentFile(path,document);
    if(!error.isEmpty())throw std::runtime_error(error.toStdString());
    const bool wasSet=qEnvironmentVariableIsSet("FORGECAD_SOLVER_DENSE");
    const auto saved=qgetenv("FORGECAD_SOLVER_DENSE");
    struct Restore { bool set;QByteArray value;~Restore(){if(set)qputenv("FORGECAD_SOLVER_DENSE",value);else qunsetenv("FORGECAD_SOLVER_DENSE");}} restore{wasSet,saved};
    const auto geometryEqual=[](const SketchObject &a,const SketchObject &b) {
        require(a.segments.size()==b.segments.size() && a.curves.size()==b.curves.size(),"solver reale: numero entita'");
        const auto near=[](const QPointF &p,const QPointF &q){require(QLineF(p,q).length()<1e-7,"solver reale: coordinate equivalenti");};
        for(int i=0;i<a.segments.size();++i){near(a.segments[i].first,b.segments[i].first);near(a.segments[i].second,b.segments[i].second);}
        for(int i=0;i<a.curves.size();++i) {
            const auto &x=a.curves[i];const auto &y=b.curves[i];
            require(x.controlPoints.size()==y.controlPoints.size() && x.tangentHandles.size()==y.tangentHandles.size()
                && x.weights==y.weights && x.knots==y.knots,"solver reale: struttura curve conservata");
            for(int j=0;j<x.controlPoints.size();++j)near(x.controlPoints[j],y.controlPoints[j]);
            for(int j=0;j<x.tangentHandles.size();++j){near(x.tangentHandles[j].first,y.tangentHandles[j].first);near(x.tangentHandles[j].second,y.tangentHandles[j].second);}
        }
    };
    const auto median=[](QVector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];};
    int accepted=0;
    QJsonArray rows;QVector<double> totalSolve[2]={QVector<double>(5,0),QVector<double>(5,0)};
    QVector<double> totalAnalysis[2]={QVector<double>(5,0),QVector<double>(5,0)};
    for(int index=0;index<document.sketches.size();++index) {
        const auto &source=document.sketches[index];QVector<PointTarget> targets;
        DocumentDragCase selection;
        if(admissible) {
            selection=selectDocumentDrag(source);targets=selection.targets;
            if(targets.isEmpty()) {
                rows.append(QJsonObject{{"index",index},{"selected",false},{"attempts",selection.attempts},
                    {"baseline_ok",selection.baselineOk},{"baseline_error",selection.baselineError},
                    {"reason",QStringLiteral("Nessun target ammissibile nel campione limitato")}});
                continue;
            }
            ++accepted;
        }
        else if(!source.segments.isEmpty())targets.append({{0,0,0},source.segments[0].first+QPointF(.01,.02)});
        else if(!admissible)for(int i=0;i<source.curves.size();++i)if(!source.curves[i].controlPoints.isEmpty()) {
            targets.append({{1,i,0},source.curves[i].controlPoints[0]+QPointF(.01,.02)});break;
        }
        QVector<double> solveTimes[2],analysisTimes[2];SolveResult solved[2];SketchAnalysis analysis[2];
        for(int repeat=0;repeat<5;++repeat) {
            SketchObject results[2];
            for(int pass=0;pass<2;++pass) {
                const int dense=(repeat%2)^pass;qputenv("FORGECAD_SOLVER_DENSE",dense?"1":"0");
                results[dense]=source;QElapsedTimer clock;clock.start();
                solved[dense]=solveSketch(results[dense],targets);
                const double solveMs=clock.nsecsElapsed()/1e6;
                solveTimes[dense].append(solveMs);totalSolve[dense][repeat]+=solveMs;
                clock.restart();analysis[dense]=analyzeSketch(source);
                const double analysisMs=clock.nsecsElapsed()/1e6;
                analysisTimes[dense].append(analysisMs);totalAnalysis[dense][repeat]+=analysisMs;
            }
            require(solved[0].ok==solved[1].ok,"solver reale: esito uguale");
            geometryEqual(results[0],results[1]);
            if(admissible)for(int dense=0;dense<2;++dense) {
                require(solved[dense].ok,"trascinamento selezionato riuscito");
                QPointF point;require(refPoint(results[dense],targets[0].point,point)
                    && QLineF(point,targets[0].position).length()<1e-7,"target reale rispettato");
                for(const auto &constraint:results[dense].geometricConstraints) {
                    const double error=constraintError(results[dense],constraint);
                    require(std::isfinite(error) && error<1e-7,"vincoli dopo trascinamento verificati indipendentemente");
                }
            }
            if(!solved[0].ok){geometryEqual(results[0],source);geometryEqual(results[1],source);}
            require(analysis[0].variables==analysis[1].variables && analysis[0].rank==analysis[1].rank
                && analysis[0].degreesOfFreedom==analysis[1].degreesOfFreedom
                && analysis[0].segmentDefined==analysis[1].segmentDefined && analysis[0].curveDefined==analysis[1].curveDefined,
                "solver reale: rango, DOF ed entita' definite equivalenti");
        }
        QJsonObject row{{"index",index},{"segments",source.segments.size()},{"curves",source.curves.size()},
            {"constraints",source.geometricConstraints.size()},{"variables",analysis[0].variables},
            {"rank",analysis[0].rank},{"dof",analysis[0].degreesOfFreedom},{"target_applied",!targets.isEmpty()},
            {"solve_ok",solved[0].ok},{"dense_iterations",solved[1].iterations},{"partition_iterations",solved[0].iterations},
            {"dense_solve_ms",median(solveTimes[1])},{"partition_solve_ms",median(solveTimes[0])},
            {"dense_analysis_ms",median(analysisTimes[1])},{"partition_analysis_ms",median(analysisTimes[0])}};
        if(admissible) {
            const auto &target=targets[0];
            row["selected"]=true;row["attempts"]=selection.attempts;row["baseline_ok"]=selection.baselineOk;
            row["baseline_error"]=selection.baselineError;
            row["target"]=QJsonObject{{"kind",target.point.kind},{"element",target.point.element},{"point",target.point.point},
                {"x",target.position.x()},{"y",target.position.y()}};
        }
        rows.append(row);
    }
    QJsonObject output{{"sketches",rows},{"features",document.extrusions.size()},
        {"admissible_targets",admissible},{"selected_sketches",accepted},{"selection_outside_timer",true},
        {"dense_solve_ms",median(totalSolve[1])},{"partition_solve_ms",median(totalSolve[0])},
        {"dense_analysis_ms",median(totalAnalysis[1])},{"partition_analysis_ms",median(totalAnalysis[0])}};
    std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<std::endl;
}

#endif
