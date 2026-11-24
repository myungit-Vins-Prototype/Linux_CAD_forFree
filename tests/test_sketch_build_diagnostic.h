static void sketchBuildDiagnostic() {
    using namespace ForgeCad;
    CadViewport view;
    view.resize(700, 560);
    SketchObject sketch;
    sketch.segments = {{{0, 0}, {4, 0}}};
    const ConstraintRef line{0, 0, -1};
    const ConstraintRef endpoint{0, 0, 1};
    sketch.geometricConstraints = {
        makeConstraint(sketch, ConstraintType::Fix, {line}),
        makeConstraint(sketch, ConstraintType::Distance, {line})
    };
    view.sketches_ = {sketch};
    view.activeSketch_ = 0;
    view.sketchMode_ = true;
    view.setDrawingTool(DrawingTool::Dimension);

    const QString error = view.setConstraintValue(1, 8);
    require(!error.isEmpty() && view.buildDiagnosticError_ == error,
            "una quota impossibile produce diagnostica nella vista");
    require(view.sketches_.first().geometricConstraints.at(1).value == 4 && !view.history_.canUndo(),
            "quota e cronologia ripristinate dopo il rifiuto");
    require(view.buildDiagnostic_.paths == QVector<QVector<QVector3D>>{
                {view.mapSketchPoint(QPointF(0, 0), sketch), view.mapSketchPoint(QPointF(4, 0), sketch)}},
            "la diagnostica localizza il segmento a cui appartiene la quota");

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &escape);
    require(view.buildDiagnosticError_.isEmpty() && view.sketchMode_,
            "Esc annulla lo strumento e pulisce la diagnostica");

    require(view.solveActiveDrag({{endpoint, QPointF(6, 0)}}, sketch)
            && view.sketches_.first().segments.first().second == QPointF(4, 0)
            && !view.history_.canUndo(), "drag impossibile conserva la posizione fissa");
    require(view.buildDiagnostic_.paths == QVector<QVector<QVector3D>>{{view.mapSketchPoint(QPointF(6, 0), sketch)}},
            "il drag fallito mostra la posizione richiesta");
    require(view.solveActiveDrag({{endpoint, QPointF(4, 0)}}, sketch) && view.buildDiagnosticError_.isEmpty(),
            "un drag compatibile cancella la diagnostica");

    SketchConstraint pierce;
    pierce.type = ConstraintType::Pierce;
    pierce.first = endpoint;
    view.sketches_.first().geometricConstraints.append(pierce);
    require(!view.solveActive({}, sketch) && view.buildDiagnostic_.context.contains(QStringLiteral("Pierce")),
            "un riferimento Pierce mancante e' segnalato come tale");
    require(view.buildDiagnostic_.paths == QVector<QVector<QVector3D>>{{view.mapSketchPoint(QPointF(4, 0), sketch)}},
            "la diagnostica Pierce localizza il punto del vincolo");
    require(view.sketches_.first().geometricConstraints.size() == 2, "rollback conserva soltanto i vincoli originali");
    view.endSketchMode();
    require(view.buildDiagnosticError_.isEmpty(), "uscire dallo schizzo rimuove la diagnostica");
    std::cout << "PASS sketch build diagnostic\n";
}
