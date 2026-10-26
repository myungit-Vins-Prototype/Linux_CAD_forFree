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

// Risultati applicativi, persistenza e indipendenza delle singole copie.
void patternResultTests() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    for (bool sheet : {false, true}) {
        ExtrusionObject source;
        source.feature = BodyFeature::Primitive;
        source.primitive.kind = PrimitiveKind::Box;
        source.primitive.size[0] = 2; source.primitive.size[1] = 3; source.primitive.size[2] = 4;
        if (sheet) {
            source.feature = BodyFeature::Imported;
            ExchangeBody exchange; exchange.body = makePlaneSheet(Frame3({3,0,0},{0,0,1},{1,0,0}), 1);
            source.importData = QByteArray::fromStdString(writeStep({exchange}));
        }
        DocumentState initial; initial.extrusions = {source};
        CadViewport view; view.loadDocument(initial);
        const auto original = writeBodyBinary(*view.extrusions()[0].forgeBody);
        ExtrusionObject repeated; repeated.feature = BodyFeature::Pattern;
        repeated.firstBody = 0; repeated.name = "Ripetizione";
        repeated.pattern.count = 4; repeated.pattern.spacing = 5;
        GeometryRef axis; axis.kind = 2; axis.index = 0; repeated.pattern.refs = {axis};
        const auto check = [&](int count) {
            const auto tips = view.resultBodiesBefore(-1);
            require(tips.size() == count, "ogni componente e' un corpo indipendente");
            QSet<quint64> ids;
            double total = 0;
            for (int i : tips) {
                const auto &part = view.extrusions()[i];
                require(part.error.isEmpty() && part.forgeBody && part.solid != sheet, "tipo e geometria dei risultati");
                require(checkBody(*part.forgeBody).empty(), "topologia valida dei risultati separati");
                ids.insert(part.modelBodyId);
                total += sheet ? faceArea(*part.forgeBody, part.forgeBody->faces().front()) : massProperties(*part.forgeBody).volume;
            }
            require(ids.size() == count, "identita' logiche distinte");
            require(std::abs(total - count * (sheet ? 4.0 : 24.0)) < 1e-8, "area o volume delle copie disgiunte");
            require(writeBodyBinary(*view.extrusions()[0].forgeBody) == original, "originale immutato");
        };
        require(view.createBody(repeated, {0}).isEmpty(), "creazione ripetizione separata"); check(4);
        require(view.setFeatureSuppressed(1, true).isEmpty(), "sopprime tutta la ripetizione");
        require(view.resultBodiesBefore(-1).size() == 1, "soppressione ripristina solo il corpo sorgente");
        require(view.setFeatureSuppressed(1, false).isEmpty(), "ripristina tutta la ripetizione"); check(4);
        const auto oldOccurrence = view.extrusions()[3].modelBodyId;
        const auto oldGeometry = writeBodyBinary(*view.extrusions()[3].forgeBody);
        auto changed = view.extrusions()[1]; changed.pattern.excluded = {1,3};
        require(view.updateBody(1, changed).isEmpty(), "esclude occorrenze"); check(2);
        const auto surviving = view.extrusions()[view.resultBodiesBefore(-1).last()];
        require(surviving.modelBodyId == oldOccurrence && writeBodyBinary(*surviving.forgeBody) == oldGeometry, "esclusione non rinumera un'altra occorrenza");
        view.undo(); check(4); view.redo(); check(2);
        changed = view.extrusions()[1]; changed.pattern.spacing = 9;
        require(view.updateBody(1, changed).isEmpty(), "cambia passo conservando esclusioni"); check(2);
        require(view.extrusions()[1].pattern.excluded == QVector<int>({1,3}), "numeri esclusi conservati");
        QByteArray bytes;
        require(encodeDocument(view.currentDocument(), bytes).isEmpty(), "salva componenti ed esclusioni");
        DocumentState restored; require(decodeDocument(bytes, restored).isEmpty(), "legge componenti ed esclusioni");
        view.loadDocument(restored); check(2);
        require(view.rebuildAllFeatures() == 0, "rigenera componenti ed esclusioni"); check(2);
        changed = view.extrusions()[1]; changed.pattern.excluded.clear(); changed.pattern.count = 5;
        require(view.updateBody(1, changed).isEmpty(), "ripristina copie e aumenta conteggio"); check(5);
        // Modificare un risultato derivato apre e aggiorna la stessa ripetizione.
        changed = view.extrusions()[1]; changed.pattern.count = 3;
        require(view.updateBody(view.resultBodiesBefore(-1).last(), changed).isEmpty(), "modifica attraverso un risultato derivato"); check(3);
        // Escludere tutto e' un errore transazionale: il documento resta valido.
        changed = view.extrusions()[1]; changed.pattern.excluded = {0,1,2};
        require(!view.updateBody(1, changed).isEmpty(), "tutte le occorrenze escluse rifiutate"); check(3);
        // L'elenco reale del pannello permette esclusione e ripristino.
        {
            CadViewport gui; gui.loadDocument(initial);
            QMainWindow window;
            std::exception_ptr failure;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = window.findChild<QDialog *>();
                try {
                    auto *list = dialog ? dialog->findChild<QListWidget *>("patternOccurrences") : nullptr;
                    require(list && list->count() == 4, "elenco occorrenze nel pannello");
                    list->item(1)->setCheckState(Qt::Unchecked);
                    // refresh ricrea la lista, leggere nuovamente l'elemento.
                    require(list->item(1)->checkState() == Qt::Unchecked, "spunta esclusione aggiornata");
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                    require(dialog->result() == QDialog::Accepted, "pannello applica esclusione");
                } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
            });
            patternDialog(&window, &gui, "Ripetizione", -1, repeated,
                [&](const ExtrusionObject &definition) { return gui.createBody(definition, {definition.firstBody}); });
            if (failure) std::rethrow_exception(failure);
            require(gui.resultBodiesBefore(-1).size() == 3 && gui.extrusions()[1].pattern.excluded == QVector<int>{1}, "esclusione dal pannello costruisce tre corpi");
            // Anche le ripetizioni aggregate dei vecchi documenti si separano
            // quando viene richiesta la rigenerazione completa.
            auto legacy = initial; legacy.extrusions.append(repeated);
            gui.loadDocument(legacy);
            require(gui.rebuildAllFeatures() == 0 && gui.resultBodiesBefore(-1).size() == 4, "rigenera ripetizioni storiche in corpi separati");
        }
        // Occorrenze incluse anche in circolare, griglia e specchio.
        for (int kind : {0,1,2}) {
            CadViewport other; other.loadDocument(initial);
            auto def = repeated; def.pattern.kind = kind;
            def.pattern.count = 4; def.pattern.excluded = {1};
            if (kind == 0) {
                def.pattern.count2 = 2; def.pattern.spacing2 = 8;
                GeometryRef y = axis; y.index = 1; def.pattern.refs.append(y);
            } else if (kind == 1) {
                def.pattern.refs[0].index = 2;
                // Sposta la base solida lontano dall'asse, evitando contatti.
                if (!sheet) { auto far = initial; far.extrusions[0].primitive.origin[0] = 6; other.loadDocument(far); }
            } else { def.pattern.refs[0].kind = 1; def.pattern.refs[0].index = 2; }
            require(other.createBody(def, {0}).isEmpty(), "esclusione nei tre tipi di ripetizione");
            require(other.resultBodiesBefore(-1).size() == (kind == 0 ? 7 : kind == 1 ? 3 : 1), "numero copie dopo esclusione per tipo");
        }
    }
    // Una ripetizione storica puo' alimentare altre feature come aggregato.
    // La sua separazione visiva non deve dimezzare l'operando della scala.
    {
        DocumentState legacy;
        ExtrusionObject base; base.feature=BodyFeature::Primitive;
        base.primitive.kind=PrimitiveKind::Box;
        base.primitive.size[0]=2; base.primitive.size[1]=3; base.primitive.size[2]=4;
        base.primitive.origin[0]=5; base.name="Base legacy";
        ExtrusionObject mirror; mirror.feature=BodyFeature::Pattern; mirror.firstBody=0;
        mirror.pattern.kind=2; mirror.name="Specchio legacy";
        GeometryRef plane; plane.kind=1; plane.index=2; mirror.pattern.refs={plane};
        ExtrusionObject scale; scale.feature=BodyFeature::Scale; scale.firstBody=1;
        scale.scaleFactor=2; scale.name="Scala aggregato";
        legacy.extrusions={base,mirror,scale};
        CadViewport view; view.loadDocument(legacy);
        const auto verify=[&] {
            int root=-1,scaled=-1; QVector<int> children;
            const auto &bodies=view.extrusions();
            for(int i=0;i<bodies.size();++i) {
                if(bodies[i].name==mirror.name) root=i;
                if(bodies[i].name==scale.name) scaled=i;
            }
            require(root>=0 && scaled>root,"madre e scala storiche conservate");
            for(int i=0;i<bodies.size();++i)
                if(bodies[i].feature==BodyFeature::Pattern && bodies[i].patternComponent>0 && bodies[i].firstBody==root) children.append(i);
            require(children.size()==2 && children[0]==root+1 && children[1]==root+2 && scaled>children.last(),
                    "componenti legacy subito dopo la madre e prima dei consumatori");
            require(bodies[root].patternComponent==-1 && bodies[scaled].firstBody==root,
                    "consumatore storico mantiene il riferimento all'aggregato");
            require(bodies[scaled].error.isEmpty() && bodies[scaled].forgeBody
                    && std::abs(massProperties(*bodies[scaled].forgeBody).volume-384)<1e-8,
                    "scala storica riceve entrambe le copie");
            view.setStoryboardPosition(view.storyboard().size());
            require(view.resultBodiesBefore(-1)==QVector<int>{scaled},"scala aggregata consuma tutti i risultati separati");
            view.setStoryboardPosition(view.storyboardIndex(false,root)+1);
            require(view.resultBodiesBefore(-1)==children,"rollback dopo la madre include tutti i risultati");
            for (int child : children)
                require(view.extrusions()[child].visible && !view.extrusions()[child].display.vertices.isEmpty(),
                        "risultati visibili e mesh pronte anche dopo rigenerazioni ripetute");
            require(bodies[children[0]].modelBodyId!=bodies[children[1]].modelBodyId,"identita' separate nel rollback");
            view.setStoryboardPosition(view.storyboard().size());
        };
        require(view.rebuildAllFeatures()==0,"rigenerazione storia legacy con consumatori"); verify();
        require(view.rebuildAllFeatures()==0,"seconda rigenerazione senza duplicazioni"); verify();
        QByteArray bytes; require(encodeDocument(view.currentDocument(),bytes).isEmpty(),"salva aggregato e risultati legacy");
        DocumentState decoded; require(decodeDocument(bytes,decoded).isEmpty(),"legge aggregato e risultati legacy");
        view.loadDocument(decoded); verify();
        require(view.rebuildAllFeatures()==0,"rigenera dopo riapertura legacy"); verify();
        view.undo(); verify(); view.redo(); verify();
        int root=-1, child=-1;
        for(int i=0;i<view.extrusions().size();++i) {
            if(view.extrusions()[i].name==mirror.name) root=i;
            if(view.extrusions()[i].patternComponent==2) child=i;
        }
        const auto survivingId=view.extrusions()[child].modelBodyId;
        auto changed=view.extrusions()[root]; changed.pattern.excluded={0};
        require(view.updateBody(root,changed).isEmpty(),"esclude originale legacy senza cambiare consumatori");
        view.setStoryboardPosition(view.storyboardIndex(false,root)+1);
        auto tips=view.resultBodiesBefore(-1);
        require(tips.size()==1 && view.extrusions()[tips.first()].modelBodyId==survivingId,
                "originale escluso: una sola copia, nessun aggregato duplicato, ID superstite stabile");
        require(!view.extrusions()[root].visible,"aggregato legacy nascosto anche se escluso il primo risultato");
        view.setStoryboardPosition(view.storyboard().size());
        changed=view.extrusions()[root]; changed.pattern.excluded.clear();
        require(view.updateBody(root,changed).isEmpty(),"ripristina originale legacy"); verify();
    }
    // Solidi collegati si uniscono; le shell delle cavita' restano nel solido.
    QString error;
    auto outer = std::make_shared<const Body>(makeBox(Frame3(), 6,6,6));
    auto inner = std::make_shared<const Body>(makeBox(Frame3({1,1,1},{0,0,1},{1,0,0}), 2,2,2));
    auto hollow = forgeBoolean(outer,inner,::BooleanOperation::Difference,&error);
    require(hollow && error.isEmpty(), "solido con cavita' di riferimento");
    auto all = forgePattern(hollow,{Transform3::translation({10,0,0})},true,&error);
    auto separated = forgePatternComponents(all,&error);
    require(error.isEmpty() && separated.size() == 2, "cavita' non esposte come corpi");
    for (const auto &part : separated)
        require(!part->isSheet() && checkBody(*part).empty() && std::abs(massProperties(*part).volume - 208) < 1e-8, "volume netto delle cavita' conservato");
    all = forgePattern(outer,{Transform3::translation({6,0,0})},true,&error);
    require(forgePatternComponents(all,&error).size() == 1, "solidi con faccia comune restano uniti");
    std::cout << "PASS pattern results: corpi solidi/superfici, esclusioni, storia, cavita' e persistenza" << std::endl;
}
