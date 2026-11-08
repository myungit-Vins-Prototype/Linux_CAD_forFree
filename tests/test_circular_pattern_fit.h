#pragma once
#include "../cad_circular_pattern_fit.h"
#include "fk_transform.h"

namespace {
ForgeCad::ForgeBody circularFitWedge(double degrees = 20, double distortion = 0, double scale = 1, bool rational = false) {
    using namespace ForgeCad::Kernel;
    std::vector<Vec3> left{{15,0,0},{15,0,12},{8,0,30},{0,0,30}}, right;
    const auto rotation = Transform3::rotation({}, {0,0,1}, degrees * kPi / 180);
    for (auto &point : left) point *= scale;
    for (const auto &point : left) right.push_back(rotation.applyToPoint(point));
    right[1][2] += distortion * scale; // Estremi identici, forma interna diversa.
    const std::vector<double> knots{0,0,0,0,1,1,1,1};
    const std::vector<double> weights = rational ? std::vector<double>{1,0.8,0.8,1} : std::vector<double>{};
    const auto a = std::make_shared<BSplineCurve<3>>(3,knots,left,weights);
    const auto b = std::make_shared<BSplineCurve<3>>(3,knots,right,weights);
    std::vector<Vec3> poles = left; poles.insert(poles.end(),right.begin(),right.end());
    auto surfaceWeights = weights; surfaceWeights.insert(surfaceWeights.end(),weights.begin(),weights.end());
    Body::BuildFace face;
    face.surface = std::make_shared<BSplineSurface>(1,3,std::vector<double>{0,0,1,1},knots,2,4,poles,surfaceWeights);
    face.loops = {{{2,true,{}},{1,true,{}},{0,false,{}}}};
    const auto base = std::make_shared<Line<3>>(left.front(),right.front()-left.front());
    return std::make_shared<const Body>(Body::buildSheet({left.front(),right.front(),left.back()},
        {{0,2,a,{0,1}},{1,2,b,{0,1}},{0,1,base,{0,distance(left.front(),right.front())}}},{face}));
}

void circularPatternFitTests() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    int count = -1; double gap = -1; QString error;
    for (int expected : {2,3,18,500}) {
        auto wedge = circularFitWedge(360.0 / expected);
        require(fitCircularPatternBorders(wedge,{},{0,0,1},count,gap,&error)
            && count == expected && gap <= kLinearResolution && error.isEmpty(),"conteggio esatto dai bordi NURBS, incluso originale e limiti");
        require(fitCircularPatternBorders(wedge,{},{0,0,-1},count,gap,&error) && count == expected,"asse invertito conserva il numero di spicchi");
    }
    require(fitCircularPatternBorders(circularFitWedge(20,0,1,true),{},{0,0,1},count,gap,&error)
        && count==18 && gap<=kLinearResolution,"bordi razionali con pesi non unitari");
    require(fitCircularPatternBorders(circularFitWedge(20,kLinearResolution/10),{},{0,0,1},count,gap,&error)
        && count==18 && gap<=kLinearResolution,"scarto sotto il budget geometrico accettato");
    require(!fitCircularPatternBorders(circularFitWedge(20,10*kLinearResolution),{},{0,0,1},count,gap,&error),
        "scarto sopra il budget geometrico rifiutato");
    const Vec3 origin{120,-83,41}, axis = normalized(Vec3{1,2,3});
    const auto rotation = Transform3::rotation({},normalized(Vec3{-2,1,0}),std::acos(axis.z()));
    // L'asse ruotato e' calcolato dalla medesima posa del corpo.
    const Vec3 tiltedAxis = rotation.applyToVector({0,0,1});
    for (double scale : {0.001,1000.0}) {
        auto wedge = circularFitWedge(20,0,scale);
        auto tilted = std::make_shared<const Body>(transformBody(*wedge,Transform3::translation(origin) * rotation));
        require(fitCircularPatternBorders(tilted,origin,tiltedAxis,count,gap,&error) && count==18
            && gap <= kLinearResolution,"asse traslato e inclinato, scale diverse, budget invariato");
    }
    for (const auto &wedge : {circularFitWedge(21),circularFitWedge(20,0.01),circularFitWedge(360.0/501)}) {
        require(!fitCircularPatternBorders(wedge,{},{0,0,1},count,gap,&error) && count==0 && gap==0 && !error.isEmpty(),
            "passo non divisore, soli estremi coincidenti e conteggio eccessivo rifiutati senza arrotondamento");
    }
    require(!fitCircularPatternBorders(circularFitWedge(),{1,0,0},{0,0,1},count,gap,&error),"asse decentrato rifiutato");
    require(!fitCircularPatternBorders(circularFitWedge(),{},{},count,gap,&error),"asse nullo rifiutato");
    require(!fitCircularPatternBorders({}, {},{0,0,1},count,gap,&error),"corpo assente rifiutato");

    ExchangeBody exchange; exchange.body = *circularFitWedge();
    ExtrusionObject source; source.feature=BodyFeature::Imported; source.importData=QByteArray::fromStdString(writeStep({exchange}));
    DocumentState state; state.extrusions={source};
    CadViewport view; view.loadDocument(state);
    QMainWindow window; ExtrusionObject initial; initial.feature=BodyFeature::Pattern; initial.firstBody=0;
    initial.pattern.kind=1; initial.pattern.count=17; initial.pattern.angle=123; initial.pattern.spread=false;
    initial.pattern.refs={GeometryRef{2,2,{},{}}}; initial.pattern.excluded={2};
    std::exception_ptr failure;
    QTimer::singleShot(0,&window,[&] {
        auto *dialog=window.findChild<QDialog *>();
        try {
            auto *button=dialog->findChild<QPushButton *>("patternFitBorders");
            require(button && button->isEnabled() && !button->isHidden(),"calcolo disponibile nel pannello circolare per superfici");
            button->click();
            auto *result=dialog->findChild<QLabel *>("patternFitResult");
            require(dialog->findChild<QSpinBox *>("patternCount")->value()==18 && result && result->text().contains("18 spicchi")
                && !result->isHidden(),"pulsante calcola 18 e mostra il risultato");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        } catch (...) { failure=std::current_exception(); if(dialog)dialog->reject(); }
    });
    ExtrusionObject calculated;
    patternDialog(&window,&view,"Bordi combacianti",-1,initial,[&](const ExtrusionObject &d) {
        calculated=d; return view.createBody(d,{d.firstBody});
    });
    if(failure)std::rethrow_exception(failure);
    require(calculated.pattern.count==18 && calculated.pattern.angle==360 && calculated.pattern.spread
        && calculated.pattern.excluded==QVector<int>{2},"giro intero applicato e esclusioni conservate");
    const int repeatedBodies=view.extrusions().size(); require(repeatedBodies>1,"ripetizione costruita");
    view.undo();require(view.extrusions().size()==1,"Undo calcolo applicato");
    view.redo();require(view.extrusions().size()==repeatedBodies,"Redo calcolo applicato");
    for (bool cache : {false,true}) {
        QByteArray bytes; DocumentState decoded;
        require(encodeDocument(view.currentDocument(),bytes,cache).isEmpty() && decodeDocument(bytes,decoded).isEmpty(),"codec parametri calcolati con e senza cache");
        view.loadDocument(decoded);
        require(view.extrusions()[1].pattern.count==18 && view.rebuildAllFeatures()==0,"conteggio preservato dopo riapertura e rigenerazione");
    }
    QByteArray cached;DocumentState legacy;
    auto migration=view.currentDocument();
    require(encodeDocument(migration,cached,true).isEmpty()
        && decodeDocument(documentWithCacheVersion(cached,13),legacy).isEmpty(),"lettura cache13 ripetizioni");
    for(const auto &body:legacy.extrusions)require(!body.cachedGeometry&&!body.forgeBody,"cache13 di lavoro rigenera trim e mesh delle ripetizioni");
    migration.version.checkinId=QUuid::createUuid();
    require(encodeDocument(migration,cached,true).isEmpty()
        && decodeDocument(documentWithCacheVersion(cached,13),legacy).isEmpty()
        && legacy.extrusions[1].cachedGeometry,"check-in storico conserva cache13 immutabile");
    QMainWindow sewWindow;ExtrusionObject sewInitial;sewInitial.feature=BodyFeature::Sew;sewInitial.sewSolid=false;
    const int candidates=view.resultBodiesBefore(-1).size();
    QTimer::singleShot(0,&sewWindow,[&]{auto *dialog=sewWindow.findChild<QDialog *>();
        try{
            auto *list=dialog->findChild<QListWidget *>("sewSurfaceList");
            auto *all=dialog->findChild<QPushButton *>("sewSelectAll");
            auto *none=dialog->findChild<QPushButton *>("sewClearAll");
            require(list&&all&&none&&list->count()==candidates,"pulsanti cucitura e candidati visibili");
            none->click();for(int i=0;i<list->count();++i)require(list->item(i)->checkState()==Qt::Unchecked,"deselezione completa");
            all->click();for(int i=0;i<list->count();++i)require(list->item(i)->checkState()==Qt::Checked,"selezione completa");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        }catch(...){failure=std::current_exception();dialog->reject();}});
    bool allChosen=false;
    sewDialog(&sewWindow,&view,"Selezione cucitura",-1,sewInitial,[&](const ExtrusionObject &d){allChosen=d.firstBody>=0&&d.booleanTools.size()==candidates-1;return QString();});
    if(failure)std::rethrow_exception(failure);
    require(allChosen,"tutti gli operandi trasferiti alla cucitura");
    std::cout<<"PASS circular border fit: NURBS completi, assi, scale, negativi, pannello, esclusioni, Undo/Redo, codec"<<std::endl;
}
}
