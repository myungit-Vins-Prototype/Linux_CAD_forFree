// Incluso nel runner come membro di ViewportInteractionTest.
static void primitiveScaleCreationFromMenu() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    enum class Finish { Apply, ChangeParameter, Cancel };
    PrimitiveParameters primitive;
    primitive.size[0] = primitive.size[1] = primitive.size[2] = 2.;
    for (bool scale : {false, true}) {
        const QString title = scale ? QStringLiteral("Scala") : QStringLiteral("Parallelepipedo");
        for (auto finish : {Finish::Apply, Finish::ChangeParameter, Finish::Cancel}) {
            PdfWindow window;
            CadViewport &view = *window.viewport_;
            if (scale) {
                clientOk(view.createPrimitive(primitive, QStringLiteral("Base da scalare")));
                require(view.selection().index == 0, "base selezionata per il comando Scala");
            }
            QAction *action = nullptr;
            for (auto *candidate : window.findChildren<QAction *>())
                if (candidate->text() == title + QStringLiteral("...")) { action = candidate; break; }
            require(action, "voce di menu del comando");

            ForgeBody ready;
            std::exception_ptr failure;
            QElapsedTimer elapsed;
            elapsed.start();
            QTimer driver;
            QObject::connect(&driver, &QTimer::timeout, &window, [&] {
                QDialog *panel = nullptr;
                for (auto *candidate : window.findChildren<QDialog *>())
                    if (candidate->windowTitle() == title) { panel = candidate; break; }
                if (!panel) return;
                try {
                    require(elapsed.elapsed() < 15000, "timeout anteprima Primitiva/Scala");
                    require(view.preview_.error.isEmpty(), "anteprima Primitiva/Scala senza errori");
                    if (!view.preview_.valid) return;
                    ready = view.preview_.geometry;
                    require(ready != nullptr, "anteprima Primitiva/Scala completa");
                    driver.stop();
                    if (finish == Finish::ChangeParameter) {
                        QDoubleSpinBox *parameter = nullptr;
                        const QString label = scale ? QStringLiteral("Fattore di scala:") : QStringLiteral("Lunghezza (X):");
                        for (auto *form : panel->findChildren<QFormLayout *>()) {
                            for (int row = 0; row < form->rowCount(); ++row) {
                                const auto *field = form->itemAt(row, QFormLayout::LabelRole);
                                const auto *caption = field ? qobject_cast<QLabel *>(field->widget()) : nullptr;
                                if (caption && caption->text() == label) {
                                    auto *item = form->itemAt(row, QFormLayout::FieldRole);
                                    parameter = item ? qobject_cast<QDoubleSpinBox *>(item->widget()) : nullptr;
                                }
                            }
                        }
                        require(parameter, "campo che modifica le dimensioni del risultato");
                        parameter->setValue(3.);
                    }
                    auto *buttons = panel->findChild<QDialogButtonBox *>();
                    require(buttons, "pulsanti di conferma Primitiva/Scala");
                    buttons->button(finish == Finish::Cancel ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok)->click();
                } catch (...) {
                    failure = std::current_exception();
                    driver.stop();
                    panel->reject();
                }
            });
            driver.start(5);
            action->trigger();
            driver.stop();
            if (failure) std::rethrow_exception(failure);
            require(ready != nullptr, "il comando apre il pannello e calcola l'anteprima");
            const int inputCount = scale ? 1 : 0;
            if (finish == Finish::Cancel) {
                require(view.extrusions().size() == inputCount && (!scale || view.extrusions().front().visible),
                        "Annulla conserva il documento");
                continue;
            }

            require(view.extrusions().size() == inputCount + 1, "la conferma aggiunge un solo risultato");
            const auto &result = view.extrusions().last();
            require(result.forgeBody && view.selection().index == inputCount
                    && (!scale || !view.extrusions().front().visible), "selezione del risultato e consumo della base");
            require((result.forgeBody == ready) == (finish == Finish::Apply), "riuso soltanto dei parametri invariati");

            CadViewport direct;
            const double dimension = finish == Finish::ChangeParameter ? 3. : 2.;
            if (scale) {
                clientOk(direct.createPrimitive(primitive, QStringLiteral("Base da scalare")));
                clientOk(direct.createScale(0, dimension, 0, {}, QStringLiteral("Scala diretta")));
            } else {
                auto parameters = primitive;
                parameters.size[0] = dimension;
                clientOk(direct.createPrimitive(parameters, QStringLiteral("Primitiva diretta")));
            }
            const auto &reference = direct.extrusions().last();
            require(reference.featureId > 0 && result.featureId == reference.featureId,
                    "stesso ID persistente con e senza anteprima");
            require(writeBodyBinary(*result.forgeBody) == writeBodyBinary(*reference.forgeBody),
                    "B-rep identico alla creazione senza anteprima");
            const auto operands = genealogyOperandsOf(*result.forgeBody);
            const auto referenceOperands = genealogyOperandsOf(*reference.forgeBody);
            require(operands.size() == inputCount && referenceOperands.size() == inputCount,
                    "operandi attesi nella genealogia");
            if (scale)
                require(operands.front().index == 0 && operands.front().featureId == referenceOperands.front().featureId,
                        "stessa base nella genealogia di Scala");
            const auto history = featureHistoryOf(*result.forgeBody);
            const auto referenceHistory = featureHistoryOf(*reference.forgeBody);
            require(history && referenceHistory, "genealogia della feature appena creata");
            const auto compare = [&](EntityKind kind, const auto &entities) {
                for (const auto id : entities) {
                    require(elementName(*result.forgeBody, kind, id.index) == elementName(*reference.forgeBody, kind, id.index),
                            "stessi nomi topologici dopo la conferma");
                    const auto &a = history->origins(kind, id.index);
                    const auto &b = referenceHistory->origins(kind, id.index);
                    require(a.size() == b.size() && history->isDeclared(kind, id.index)
                            == referenceHistory->isDeclared(kind, id.index), "stesse origini dichiarate");
                    for (std::size_t i = 0; i < a.size(); ++i)
                        require(a[i].operand == b[i].operand && a[i].kind == b[i].kind && a[i].index == b[i].index
                                && a[i].relation == b[i].relation, "stessa evoluzione topologica");
                }
            };
            compare(EntityKind::Face, result.forgeBody->faces());
            compare(EntityKind::Edge, result.forgeBody->edges());
            compare(EntityKind::Vertex, result.forgeBody->vertices());
        }
    }
}
