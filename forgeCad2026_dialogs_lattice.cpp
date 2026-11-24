#include "forgeCad2026_dialogs.h"
#include "forgeCad2026_viewport.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>

#include "cad_expression.h"
#include "cad_forge.h"
#include "cad_lattice_preview.h"
#include "fk_cancel.h"
#include "fk_classify.h"
#include "fk_precision.h"

namespace {
namespace Kernel = ForgeCad::Kernel;
using ForgeCad::Gui::LatticePreviewMeshes;

struct TrialInput {
    ForgeCad::ForgeBody source;
    ExtrusionObject definition;
    ForgeCad::PreparedVolumeStructure prepared;
    std::shared_ptr<const Kernel::CurvedGyroidEnvelope> envelope;
    int quality = 0;
    double accuracy = 1e-7;
};

struct LatticeTrial {
    Kernel::CancellationToken cancellation = std::make_shared<std::atomic<bool>>(false);
    LatticePreviewMeshes meshes;
    ForgeCad::ForgeBody body;
    BodyDisplay display;
    QString status;
    std::shared_ptr<const Kernel::CurvedGyroidEnvelope> envelope;
};

struct LivePreview {
    Kernel::CancellationToken cancellation = std::make_shared<std::atomic<bool>>(false);
    BodyDisplay source;
    LatticePreviewMeshes meshes;
    std::shared_ptr<LatticeTrial> native;
};

ForgeCad::ExpressionSpinBox *trialLength(QWidget *parent, double value, bool coordinate = false) {
    auto *field = new ForgeCad::ExpressionSpinBox(parent);
    field->setDecimals(6);
    field->setRange(coordinate ? -Kernel::kSizeBox : 0.000001, Kernel::kSizeBox);
    field->setSingleStep(0.1);
    field->setValue(value);
    field->setKeyboardTracking(false);
    // Invio applica la quota tramite valueChanged e resta nel pannello.
    field->onReturn = [] {};
    return field;
}

QVector<QVector3D> outlineFor(const Kernel::Box &box) {
    std::array<QVector3D, 8> corners;
    for (int n = 0; n < 8; ++n)
        for (int axis = 0; axis < 3; ++axis)
            corners[n][axis] = float((n & (1 << axis)) ? box.hi[axis] : box.lo[axis]);
    QVector<QVector3D> outline;
    outline.reserve(24);
    for (int axis = 0; axis < 3; ++axis)
        for (int n = 0; n < 8; ++n)
            if (!(n & (1 << axis))) {
                outline.append(corners[n]);
                outline.append(corners[n | (1 << axis)]);
            }
    return outline;
}

LatticePreviewMeshes displayTrialResult(const BodyDisplay &display, const TrialInput &in) {
    if (!ForgeCad::isInterpolatedGyroid(in.prepared.mode))
        return ForgeCad::Gui::latticeResultMeshes(display, in.prepared.region, in.prepared.axis);

    // Mantiene i colori delle due zone senza mostrare gli spigoli
    // delle facce triangolari della parete.
    BodyDisplay faces;
    faces.vertices = display.vertices;
    faces.normals = display.normals;
    return ForgeCad::Gui::latticeResultMeshes(std::move(faces), in.prepared.region, in.prepared.axis);
}

void runLatticeTrial(LatticeTrial &trial, const TrialInput &in) {
    using Clock = std::chrono::steady_clock;
    try {
        const Kernel::CancellationScope cancel(trial.cancellation);
        const Kernel::PrecisionScope precision(in.accuracy);
        const auto start = Clock::now();
        if (ForgeCad::usesSampledGyroidEnvelope(in.prepared.mode)) {
            trial.envelope = in.envelope;
            if (!trial.envelope)
                trial.envelope = Kernel::sampleCurvedGyroidEnvelope(*in.source, in.prepared.settings.samplingPitch);
        }
        trial.body = std::make_shared<const Kernel::Body>(in.prepared.build(*in.source, trial.envelope.get()));
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        Kernel::cancellationPoint();
        trial.status = QStringLiteral("Un solido chiuso, %1 facce. Calcolo %2 s.")
            .arg(trial.body->faces().size()).arg(seconds, 0, 'f', 2);
        try {
            if (ForgeCad::isInterpolatedGyroid(in.prepared.mode))
                ForgeCad::forgeTessellateFacetedSolid(*trial.body, in.quality, trial.display);
            else
                ForgeCad::forgeTessellate(*trial.body, in.quality, trial.display);
            trial.meshes = displayTrialResult(trial.display, in);
        } catch (const Kernel::OperationCancelled &) {
            throw;
        } catch (const std::exception &) {
            trial.display = {};
            trial.status += QStringLiteral(" Visualizzazione del risultato non disponibile.");
        }
    } catch (const Kernel::OperationCancelled &) {
        trial.body.reset();
        trial.status = QStringLiteral("Calcolo interrotto.");
    } catch (const std::exception &error) {
        trial.status = QString::fromUtf8(error.what());
    }
}
}

void volumeStructureDialog(QMainWindow *parent, CadViewport *viewport, int editIndex,
                           const ExtrusionObject &original) {
    const bool editing = editIndex >= 0;
    const auto &saved = original.volumeStructure;
    FunctionDialogPanel dialog(parent);
    ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(editing ? QStringLiteral("Modifica struttura volumetrica")
                                  : QStringLiteral("Struttura volumetrica"));
    dialog.setSizeIdentity(QStringLiteral("volume-structure"));
    dialog.setObjectName(QStringLiteral("wovenLatticeTrial"));
    auto *form = dialog.createScrollableForm();
    auto *body = new QComboBox(&dialog);
    std::vector<int> solids;
    const auto &bodies = viewport->extrusions();
    const SceneSelection selection = viewport->selection();
    int selected = -1, lastVisible = -1;
    const int historyLimit = viewport->historyPosition();
    for (int index = 0; index < bodies.size(); ++index) {
        const auto &candidate = bodies.at(index);
        if (editing ? index != original.firstBody : (historyLimit >= 0 && index >= historyLimit)) continue;
        if (!candidate.forgeBody || candidate.forgeBody->isSheet() || candidate.suppressed) continue;
        body->addItem(candidate.name);
        solids.push_back(index);
        const int row = body->count() - 1;
        if (candidate.visible) lastVisible = row;
        if (selection.kind == SceneObjectKind::Extrusion && selection.index == index)
            selected = row;
    }
    if (solids.empty()) {
        QMessageBox::information(parent, dialog.windowTitle(), QStringLiteral("Serve un solido di partenza disponibile a questo punto della storyboard."));
        return;
    }
    body->setCurrentIndex(selected >= 0 ? selected : std::max(lastVisible, 0));
    body->setEnabled(!editing);
    body->setObjectName(QStringLiteral("volumeStructureSource"));

    auto *mode = new QComboBox(&dialog);
    mode->setObjectName(QStringLiteral("volumeStructureMode"));
    mode->addItems({QStringLiteral("Reticolo intrecciato"), QStringLiteral("Reticolo cubico"),
                    QStringLiteral("Voxelizzazione"), QStringLiteral("Giroide a parete (sperimentale)"),
                    QStringLiteral("TPMS a parete curva"),
                    QStringLiteral("TPMS con parete spline (sperimentale)"),
                    QStringLiteral("TPMS con rifilo spline (sperimentale)")});
    mode->setCurrentIndex(int(saved.mode));

    auto *axis = new QComboBox(&dialog);
    axis->setObjectName(QStringLiteral("latticeTrialAxis"));
    axis->addItems({QStringLiteral("Intero solido"), QStringLiteral("Intervallo X"),
                    QStringLiteral("Intervallo Y"), QStringLiteral("Intervallo Z")});
    axis->setCurrentIndex(saved.axis + 1);
    auto *from = trialLength(&dialog, saved.from, true);
    auto *to = trialLength(&dialog, saved.to, true);
    from->setObjectName(QStringLiteral("latticeTrialFrom"));
    to->setObjectName(QStringLiteral("latticeTrialTo"));
    auto *sampling = trialLength(&dialog, saved.samplingPitch);
    auto *pitch = trialLength(&dialog, saved.cellPitch);
    auto *strut = trialLength(&dialog, saved.strutWidth);
    auto *filament = trialLength(&dialog, saved.filamentWidth);
    auto *amplitude = trialLength(&dialog, saved.amplitude);
    auto *wall = trialLength(&dialog, saved.wallThickness);
    wall->setObjectName(QStringLiteral("volumeStructureWall"));
    wall->setToolTip(QStringLiteral("Spessore nominale della fascia del giroide prima del campionamento."));
    sampling->setObjectName(QStringLiteral("volumeStructureSampling"));
    strut->setObjectName(QStringLiteral("volumeStructureStrut"));
    pitch->setObjectName(QStringLiteral("latticeTrialPitch"));
    filament->setObjectName(QStringLiteral("latticeTrialFilament"));
    form->addRow(QStringLiteral("Solido"), body);
    form->addRow(QStringLiteral("Struttura"), mode);
    form->addRow(QStringLiteral("Zona"), axis);
    form->addRow(QStringLiteral("Da"), from);
    form->addRow(QStringLiteral("A"), to);
    auto *suggest = new QPushButton(QStringLiteral("Parametri automatici"), &dialog);
    suggest->setObjectName(QStringLiteral("latticeTrialSuggest"));
    form->addRow(suggest);
    form->addRow(QStringLiteral("Passo di campionamento"), sampling);
    form->addRow(QStringLiteral("Passo del reticolo"), pitch);
    form->addRow(QStringLiteral("Larghezza aste"), strut);
    form->addRow(QStringLiteral("Larghezza fili"), filament);
    form->addRow(QStringLiteral("Ampiezza intreccio"), amplitude);
    form->addRow(QStringLiteral("Spessore nominale parete"), wall);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    status->setObjectName(QStringLiteral("latticeTrialStatus"));
    form->addRow(status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *run = buttons->addButton(QStringLiteral("Calcola"), QDialogButtonBox::ActionRole);
    auto *confirm = buttons->addButton(QStringLiteral("Conferma"), QDialogButtonBox::AcceptRole);
    auto *close = buttons->button(QDialogButtonBox::Close);
    close->setText(QStringLiteral("Annulla"));
    run->setObjectName(QStringLiteral("latticeTrialCalculate"));
    confirm->setObjectName(QStringLiteral("volumeStructureConfirm"));
    static_cast<QVBoxLayout *>(dialog.layout())->addWidget(buttons);
    confirm->setEnabled(false);

    const double accuracy = viewport->calculationAccuracy();
    Kernel::Box bounds;
    int boundsIndex = -1;
    BodyDisplay cachedSource;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(160);
    quint64 revision = 0;
    bool automatic = !editing, automaticSampling = saved.automaticSampling, running = false;
    bool preserveSampling = editing, showingSaved = false, pendingNative = false;
    double preferredSampling = sampling->value();
    std::optional<TrialInput> requested;
    std::shared_ptr<LivePreview> live;
    std::shared_ptr<LatticeTrial> trial;
    std::shared_ptr<const Kernel::CurvedGyroidEnvelope> cachedEnvelope;
    ForgeCad::ForgeBody envelopeSource;
    double envelopeStep = 0;
    const auto useCachedEnvelope = [&](TrialInput &in) {
        if (in.source == envelopeSource && in.prepared.settings.samplingPitch == envelopeStep)
            in.envelope = cachedEnvelope;
    };
    const auto cacheEnvelope = [&](const TrialInput &in, const LatticeTrial &built) {
        if (!built.envelope || !requested || requested->source != in.source
            || requested->prepared.settings.samplingPitch != in.prepared.settings.samplingPitch) return;
        cachedEnvelope = built.envelope;
        envelopeSource = in.source;
        envelopeStep = in.prepared.settings.samplingPitch;
    };

    const auto cancelLive = [&] {
        timer.stop();
        if (live) live->cancellation->store(true, std::memory_order_relaxed);
    };
    const auto refresh = [&] {
        ++revision;
        cancelLive();
        trial.reset();
        requested.reset();
        confirm->setEnabled(false);
        run->setEnabled(false);
        showingSaved = pendingNative = false;
        try {
            const Kernel::PrecisionScope precision(accuracy);
            TrialInput in;
            in.accuracy = accuracy;
            in.quality = viewport->tessellationQuality();
            in.definition = original;
            in.definition.feature = BodyFeature::VolumetricStructure;
            const int source = solids.at(body->currentIndex());
            in.definition.firstBody = source;
            in.source = viewport->extrusions().at(source).forgeBody;
            auto &p = in.definition.volumeStructure;
            p.mode = VolumeStructureMode(mode->currentIndex());
            p.axis = axis->currentIndex() - 1;
            p.from = from->value();
            p.to = to->value();
            p.automaticSampling = automaticSampling;
            const auto region = ForgeCad::volumeStructureRegion(bounds, p.axis, p.from, p.to);
            viewport->setLatticeTrialRegion(source, outlineFor(region));
            const bool woven = p.mode == VolumeStructureMode::Woven;
            const bool voxel = p.mode == VolumeStructureMode::Voxel;
            const bool curved = ForgeCad::isInterpolatedGyroid(p.mode);
            const bool gyroid = ForgeCad::isGyroidMode(p.mode);
            if (!editing || original.name == mode->itemText(int(saved.mode)))
                in.definition.name = mode->currentText();

            if (automatic) {
                const auto values = Kernel::suggestWovenLattice(region);
                const QSignalBlocker bs(sampling), bp(pitch), ba(strut), bf(filament), bw(amplitude), bt(wall);
                sampling->setValue(values.samplingPitch);
                preferredSampling = sampling->value();
                if (gyroid) {
                    pitch->setValue((curved ? 20.0 : 14.0) * preferredSampling);
                    wall->setValue((curved ? 3.5 : 2.2) * preferredSampling);
                } else {
                    pitch->setValue(values.pattern.frame.cellPitch);
                    strut->setValue(values.pattern.frame.strutWidth);
                    filament->setValue(values.pattern.filamentWidth);
                    amplitude->setValue(values.pattern.amplitude);
                }
            } else if (automaticSampling && !preserveSampling) {
                const Kernel::WovenLattice pattern{{{}, pitch->value(), strut->value()}, filament->value(), amplitude->value()};
                double limit = preferredSampling;
                if (woven) limit = Kernel::wovenLatticeSamplingLimit(pattern);
                else if (curved) limit = Kernel::curvedGyroidSamplingLimit({{}, pitch->value(), wall->value()});
                else if (gyroid) limit = Kernel::sheetGyroidSamplingLimit({{}, pitch->value(), wall->value()});
                else if (!voxel) limit = std::min(strut->value(), pitch->value() - strut->value());
                if (limit > 0 && std::isfinite(limit)) {
                    const double compatible = std::floor(std::min(preferredSampling, limit) * 1e6) / 1e6;
                    const QSignalBlocker blocker(sampling);
                    sampling->setValue(compatible);
                }
            }
            preserveSampling = false;
            p.samplingPitch = sampling->value();
            p.cellPitch = pitch->value();
            p.strutWidth = strut->value();
            p.filamentWidth = filament->value();
            p.amplitude = amplitude->value();
            p.wallThickness = wall->value();
            in.prepared = ForgeCad::prepareVolumeStructure(bounds, p);
            requested = std::move(in);
            run->setEnabled(true);
            if (editing && revision == 1 && original.error.isEmpty() && !original.suppressed
                && original.forgeBody && !original.display.vertices.isEmpty()) {
                auto meshes = displayTrialResult(original.display, *requested);
                if (!meshes.lattice.vertices.isEmpty()) {
                    viewport->setLatticeTrialPreview(std::move(meshes.lattice), std::move(meshes.retained));
                    status->setText(QStringLiteral("Struttura corrente. Modifica i parametri per aggiornare l’anteprima."));
                    showingSaved = true;
                    confirm->setEnabled(true);
                    return;
                }
            }
            status->setText(QStringLiteral("Aggiornamento anteprima…"));
            timer.start();
        } catch (const std::exception &error) {
            status->setText(QString::fromUtf8(error.what()));
        }
    };
    const auto updateZone = [&](bool keepInterval) {
        const int index = solids.at(body->currentIndex());
        const int direction = axis->currentIndex() - 1;
        form->setRowVisible(from, direction >= 0);
        form->setRowVisible(to, direction >= 0);
        try {
            if (index != boundsIndex) {
                const Kernel::PrecisionScope precision(accuracy);
                bounds = Kernel::RayFaceIndex(*viewport->extrusions().at(index).forgeBody).bounds;
                boundsIndex = index;
                cachedSource = viewport->extrusions().at(index).display;
            }
            if (direction >= 0 && !keepInterval) {
                const QSignalBlocker low(from), high(to);
                from->setValue(bounds.lo[direction]);
                to->setValue(bounds.hi[direction]);
            }
            refresh();
        } catch (const std::exception &error) {
            cancelLive();
            ++revision;
            requested.reset();
            trial.reset();
            run->setEnabled(false);
            confirm->setEnabled(false);
            viewport->clearLatticeTrialPreview();
            status->setText(QString::fromUtf8(error.what()));
        }
    };
    const auto showModeFields = [&] {
        const auto current = VolumeStructureMode(mode->currentIndex());
        form->setRowVisible(pitch, current != VolumeStructureMode::Voxel);
        form->setRowVisible(strut, current == VolumeStructureMode::Woven || current == VolumeStructureMode::Cubic);
        form->setRowVisible(wall, ForgeCad::isGyroidMode(current));
        const bool curved = ForgeCad::isInterpolatedGyroid(current);
        if (auto *label = qobject_cast<QLabel *>(form->labelForField(from)))
            label->setText(curved ? QStringLiteral("Punto di partenza") : QStringLiteral("Da"));
        from->setToolTip(curved ? QStringLiteral("Quota sull’asse scelto. La parte prima di questa quota resta piena.")
                                : QString());
        form->setRowVisible(filament, current == VolumeStructureMode::Woven);
        form->setRowVisible(amplitude, current == VolumeStructureMode::Woven);
        if (auto *label = qobject_cast<QLabel *>(form->labelForField(sampling)))
            label->setText(current == VolumeStructureMode::Voxel ? QStringLiteral("Lato del voxel")
                                                                : QStringLiteral("Passo di campionamento"));
    };
    QObject::connect(mode, &QComboBox::currentIndexChanged, &dialog, [&] {
        showModeFields();
        refresh();
    });
    for (auto *choice : {body, axis})
        QObject::connect(choice, &QComboBox::currentIndexChanged, &dialog, [&] { updateZone(false); });
    for (auto *field : {from, to})
        QObject::connect(field, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(sampling, &QDoubleSpinBox::valueChanged, &dialog, [&] {
        automatic = automaticSampling = false;
        refresh();
    });
    for (auto *field : {pitch, strut, filament, amplitude, wall})
        QObject::connect(field, &QDoubleSpinBox::valueChanged, &dialog, [&] {
            automatic = false;
            refresh();
        });
    QObject::connect(suggest, &QPushButton::clicked, &dialog, [&] {
        automatic = automaticSampling = true;
        refresh();
    });

    QObject::connect(&timer, &QTimer::timeout, &dialog, [&] {
        if (!requested || running || trial || live) return;
        TrialInput in = *requested;
        useCachedEnvelope(in);
        const quint64 started = revision;
        live = std::make_shared<LivePreview>();
        if (pendingNative) {
            run->setEnabled(false);
            live->native = std::make_shared<LatticeTrial>();
            live->native->cancellation = live->cancellation;
        } else {
            live->source = cachedSource;
        }
        auto *worker = QThread::create([job = live, in] {
            if (job->native) {
                runLatticeTrial(*job->native, in);
                return;
            }
            try {
                const Kernel::CancellationScope cancel(job->cancellation);
                const Kernel::PrecisionScope precision(in.accuracy);
                if (job->source.vertices.isEmpty()) ForgeCad::forgeTessellate(*in.source, 0, job->source);
                job->meshes = ForgeCad::Gui::latticePreviewMeshes(job->source, in.prepared, in.definition.volumeStructure.axis);
            } catch (const Kernel::OperationCancelled &) {
            } catch (const std::exception &) {
                job->meshes = {};
            }
        });
        QObject::connect(worker, &QThread::finished, &dialog, [&, job = live, in, started] {
            live.reset();
            if (job->native) cacheEnvelope(in, *job->native);
            if (in.definition.firstBody == boundsIndex && cachedSource.vertices.isEmpty() && !job->cancellation->load())
                cachedSource = std::move(job->source);
            if (running || trial || !requested) return;
            if (started != revision) {
                if (!timer.isActive()) timer.start();
                return;
            }
            if (job->native) {
                trial = job->native;
                status->setText(trial->status);
                if (!trial->meshes.lattice.vertices.isEmpty())
                    viewport->setLatticeTrialPreview(std::move(trial->meshes.lattice), std::move(trial->meshes.retained));
                run->setEnabled(!trial->body);
                confirm->setEnabled(bool(trial->body));
                return;
            }
            const bool hasMesh = !job->meshes.lattice.vertices.isEmpty();
            if (hasMesh)
                viewport->setLatticeTrialPreview(std::move(job->meshes.lattice), std::move(job->meshes.retained));
            if (ForgeCad::usesSampledGyroidEnvelope(in.prepared.mode)) {
                status->setText(hasMesh ? QStringLiteral("Anteprima indicativa. Calcolo della parete TPMS…")
                                        : QStringLiteral("Calcolo della parete TPMS…"));
                pendingNative = true;
                timer.start();
            } else if (in.prepared.mode == VolumeStructureMode::TrimmedSplineGyroid) {
                status->setText(hasMesh ? QStringLiteral("Anteprima indicativa. Premi Calcola per il rifilo spline.")
                                        : QStringLiteral("Premi Calcola per il rifilo spline."));
            } else if (hasMesh) {
                status->setText(job->meshes.simplified ? QStringLiteral("Anteprima semplificata. Calcola per verificare il solido.")
                                                      : QStringLiteral("Anteprima grafica. Calcola per verificare il solido."));
            } else {
                status->setText(QStringLiteral("Anteprima grafica vuota. Usa Calcola per verificare il solido."));
            }
        });
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    });

    const auto setRunning = [&](bool active) {
        running = active;
        for (QWidget *field : std::initializer_list<QWidget *>{mode, axis, from, to, sampling, pitch, strut, filament, amplitude, wall, suggest, run})
            field->setEnabled(!active);
        body->setEnabled(!editing && !active);
        close->setText(active ? QStringLiteral("Annulla calcolo") : QStringLiteral("Annulla"));
    };
    QObject::connect(run, &QPushButton::clicked, &dialog, [&] {
        if (!requested) return;
        cancelLive();
        showingSaved = false;
        TrialInput in = *requested;
        useCachedEnvelope(in);
        confirm->setEnabled(false);
        trial = std::make_shared<LatticeTrial>();
        auto *worker = QThread::create([job = trial, in] { runLatticeTrial(*job, in); });
        QObject::connect(worker, &QThread::finished, &dialog, [&, job = trial, in] {
            setRunning(false);
            cacheEnvelope(in, *job);
            status->setText(job->status);
            if (!job->meshes.lattice.vertices.isEmpty())
                viewport->setLatticeTrialPreview(std::move(job->meshes.lattice), std::move(job->meshes.retained));
            confirm->setEnabled(bool(job->body));
        });
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        setRunning(true);
        status->setText(QStringLiteral("Calcolo e verifica del solido…"));
        worker->start();
    });
    QObject::connect(confirm, &QPushButton::clicked, &dialog, [&] {
        if (showingSaved) {
            dialog.accept();
            return;
        }
        if (!requested || !trial || !trial->body) return;
        const auto &definition = requested->definition;
        const int source = definition.firstBody;
        if (source >= viewport->extrusions().size() || viewport->extrusions().at(source).forgeBody != requested->source) {
            status->setText(QStringLiteral("Il solido di partenza è cambiato. Riapri il comando."));
            confirm->setEnabled(false);
            run->setEnabled(false);
            return;
        }
        viewport->stageVolumeStructureResult(definition, trial->body, trial->display, editIndex);
        const QString error = editing ? viewport->updateBody(editIndex, definition) : viewport->createBody(definition);
        viewport->closeFunctionPreview();
        if (error.isEmpty()) dialog.accept();
        else status->setText(error);
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    runModeless(parent, viewport, dialog, [&] {
        showModeFields();
        updateZone(editing);
    });
    cancelLive();
    if (trial) trial->cancellation->store(true, std::memory_order_relaxed);
    viewport->clearLatticeTrialPreview();
}
