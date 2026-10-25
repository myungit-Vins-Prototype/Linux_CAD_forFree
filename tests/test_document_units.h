// Include dentro ViewportInteractionTest: verifica due documenti reali senza
// dipendere da focus, locale o ordine dei callback asincroni.
static void documentUnitsTests() {
    using namespace ForgeCad;
    PdfWindow first, second;
    DocumentState mm, inches;
    mm.lengthUnitSet = inches.lengthUnitSet = true;
    mm.lengthUnit = LengthUnit::Millimeter;
    inches.lengthUnit = LengthUnit::Inch;
    first.viewport_->loadDocument(mm);
    QDialog panel(&first);
    ExpressionSpinBox firstField(&panel);
    firstField.setRange(-1e6, 1e6);
    firstField.setDecimals(6);
    firstField.setValue(25.4);
    second.viewport_->loadDocument(inches);
    ExpressionSpinBox secondField(&second);
    secondField.setRange(-1e6, 1e6);
    secondField.setDecimals(6);
    secondField.setValue(25.4);
    require(firstField.valueFromText("10") == 10 && secondField.valueFromText("10") == 254,
            "due finestre convertono nelle rispettive unita'");
    require(firstField.suffix().trimmed() == "mm" && secondField.suffix().trimmed() == "in",
            "aprire un secondo documento non cambia il suffisso del primo");
    require(formatLength(25.4, 6, true, &second) == "1 in"
            && formatLength(25.4, 6, true, &first).endsWith(" mm"),
            "formattazione esplicita indipendente dal documento aperto per ultimo");
    SketchConstraint dimension; dimension.type = ConstraintType::Distance; dimension.value = 25.4;
    require(first.viewport_->dimensionText(dimension).endsWith(" mm")
            && second.viewport_->dimensionText(dimension) == "1 in",
            "quote dei due viewport conservano le proprie unita'");
    MeasureEntity point; point.kind=MeasureEntity::Kind::Point; point.point=Kernel::Vec3(25.4,0,0);
    require(measureEntities({point}, &second).lines.first() == "X: 1 in"
            && measureEntities({point}, &first).lines.first().endsWith(" mm"),
            "rapporto misure usa il contesto del chiamante");
    int changed = 0;
    QObject::connect(&firstField, &QDoubleSpinBox::valueChanged, &panel, [&](double){ ++changed; });
    first.viewport_->setLengthUnit(LengthUnit::Centimeter);
    require(firstField.value() == 25.4 && changed == 0 && firstField.valueFromText("10") == 100,
            "cambio unita' aggiorna il campo proprietario senza mutare i millimetri");
    require(secondField.valueFromText("10") == 254 && secondField.suffix().trimmed() == "in",
            "cambio unita' non contamina altra finestra");
    first.viewport_->setLengthUnit(LengthUnit::Millimeter);
    first.viewport_->setLengthUnit(LengthUnit::Millimeter);
    require(firstField.valueFromText("10") == 10, "riselezione della stessa unita' resta coerente");
    CadViewport preview(&first);
    preview.loadPreviewDocument(inches);
    require(displayLengthUnit(&preview) == LengthUnit::Inch && displayLengthUnit(&first) == LengthUnit::Millimeter,
            "anteprima file non cambia il documento della finestra");
    first.viewport_->restoreDocument(inches);
    require(firstField.valueFromText("10") == 254 && first.viewport_->lengthUnit() == LengthUnit::Inch,
            "ripristino documento aggiorna contesto e controlli");
    ExpressionSpinBox orphan;
    require(orphan.valueFromText("10") == 10 && displayLengthUnit() == LengthUnit::Millimeter,
            "campo senza documento usa mm, non l'ultima finestra");
}
