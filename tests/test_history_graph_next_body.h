// Incluso nel friend ViewportInteractionTest.
static void historyGraphNextBodyRegressions() {
    using namespace ForgeCad;
    DocumentState diagnostic;
    SketchObject sketch; sketch.storyId=1; sketch.beforeFeatureId=3;
    sketch.name=QStringLiteral("Profilo"); diagnostic.sketches={sketch};
    ExtrusionObject target; target.feature=BodyFeature::Primitive; target.featureId=1; target.name=QStringLiteral("Limite e destinatario");
    ExtrusionObject other=target; other.featureId=2; other.name=QStringLiteral("Solo limite");
    ExtrusionObject extrude; extrude.featureId=3; extrude.sketchIndex=0; extrude.extent=4;
    extrude.name=QStringLiteral("Fino al corpo e unione"); extrude.booleanTools={0,1};
    extrude.mergeOperation=1; extrude.mergeBodies={0};
    diagnostic.extrusions={target,other,extrude};
    HistoryGraphDialog graph; graph.setDocument(diagnostic);
    auto details=graph.featureDiagnostics(2);
    require(details.count(QStringLiteral("limite prossimo corpo"))==2
                && details.count(QStringLiteral("destinatario fusione"))==1
                && !details.contains(QStringLiteral("fine estrusione"))
                && !details.contains(QStringLiteral("NON VALIDA")) && graph.invalidDependencyCount()==0,
            "grafo extent4 espone entrambi i ruoli e nessun ExtentRef fittizio");
    require(graph.edgeCount()==4, "profilo, due limiti e destinatario conservano quattro archi di ruolo");
    diagnostic.extrusions[2].booleanTools={0,99}; graph.setDocument(diagnostic);
    require(graph.invalidDependencyCount()==1 && graph.featureDiagnostics(2).contains(QStringLiteral("NON VALIDA")),
            "limite reale mancante e' ancora segnalato dal grafo");
    diagnostic.extrusions[2].booleanTools={0,1}; diagnostic.extrusions[2].mergeOperation=0;
    graph.setDocument(diagnostic); details=graph.featureDiagnostics(2);
    require(!details.contains(QStringLiteral("destinatario fusione")) && details.count(QStringLiteral("limite prossimo corpo"))==2
                && graph.invalidDependencyCount()==0 && graph.edgeCount()==3,
            "lista merge inattiva non inventa un destinatario nel grafo");

    // Stesso target in booleanTools e mergeBodies: resta un unico proprietario
    // e un solo operando genealogico, con consumo soltanto per la fusione.
    DocumentState state;
    sketch={}; sketch.segments={{{0,0},{2,0}},{{2,0},{2,2}},{{2,2},{0,2}},{{0,2},{0,0}}}; state.sketches={sketch};
    target={}; target.feature=BodyFeature::Primitive; target.name=QStringLiteral("Limite da fondere");
    target.primitive.origin[0]=target.primitive.origin[1]=-1; target.primitive.origin[2]=4;
    target.primitive.size[0]=target.primitive.size[1]=4; target.primitive.size[2]=2;
    other=target; other.name=QStringLiteral("Limite disgiunto indipendente"); other.primitive.origin[0]=10;
    state.extrusions={target,other};
    CadViewport view; view.tessellationQuality_=0; view.loadDocument(state);
    const quint64 owner=view.extrusions_[0].modelBodyId, independent=view.extrusions_[1].modelBodyId;
    ExtrusionObject definition; definition.sketchIndex=0; definition.extent=4; definition.distance=1;
    definition.mergeOperation=1; definition.mergeAuto=false; definition.mergeBodies={0};
    require(view.createExtrusion(definition,QStringLiteral("Primo incontro e unione")).isEmpty(), "unione analitica con limite anche destinatario");
    const auto check=[&](double volume,bool merged) {
        const auto &body=view.extrusions_.at(2);
        require(body.extent==4 && body.booleanTools==QVector<int>({0,1})
                    && bodyOperands(body)==QVector<int>({0,1})
                    && hiddenOperands(body)==(merged?QVector<int>{0}:QVector<int>{})
                    && body.modelBodyId && (merged ? body.modelBodyId==owner : body.modelBodyId!=owner)
                    && view.extrusions_[1].modelBodyId==independent && view.extrusions_[1].visible,
                "limiti, destinatario, identita' e corpo disgiunto indipendente preservati");
        require(body.forgeBody && std::fabs(Kernel::massProperties(*body.forgeBody).volume-volume)<1e-9,
                "geometria coerente col ruolo di fusione");
        const QVector<int> expected{0,1};
        require(featureGenealogyOperandIndices(body,2,view.extrusions_)==expected,
                "genealogia deduplica il doppio ruolo senza perdere il limite disgiunto");
        const auto operands=genealogyOperandsOf(*body.forgeBody);
        require(operands.size()==2 && operands[0].featureId==view.extrusions_[0].featureId
                    && operands[1].featureId==view.extrusions_[1].featureId,
                "genealogia effettiva conserva le identita' dei due limiti");
    };
    check(48,true);
    const quint64 id=view.extrusions_[2].featureId;
    view.undo(); require(view.extrusions_.size()==2 && view.extrusions_[0].visible && view.extrusions_[1].visible,
                         "Undo rilascia il destinatario e conserva l'indipendente");
    view.redo(); require(view.extrusions_[2].featureId==id,"Redo conserva identita' della funzione"); check(48,true);
    view.setHistoryPosition(2); require(view.isBodyDrawn(0) && view.isBodyDrawn(1) && !view.isBodyDrawn(2),"rollback rilascia soltanto la fusione futura");
    view.setHistoryPosition(3); check(48,true);
    QByteArray bytes; require(encodeDocument(view.currentDocument(),bytes,false).isEmpty(),"codec senza cache conserva entrambi i ruoli");
    DocumentState loaded; require(decodeDocument(bytes,loaded).isEmpty(),"lettura codec doppio ruolo");
    view.loadDocument(loaded); check(48,true);
    auto separate=view.extrusions_[2]; separate.mergeOperation=0;
    require(view.updateBody(2,separate).isEmpty(),"disattivare unione conserva il target come solo limite"); check(16,false);
    require(view.extrusions_[0].visible,"il target non consumato torna visibile");
    view.undo(); check(48,true); view.redo(); check(16,false);
    std::cout<<"PASS grafo prossimo corpo: ruoli distinti, identita', consumo, genealogia, rollback, Undo/Redo e codec"<<std::endl;
}
