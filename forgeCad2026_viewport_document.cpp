#include "forgeCad2026_viewport_internal.h"
#include "cad_geometry_refs.h"


namespace {
QString sketchPositionError(const CadViewport &view, const ExtrusionObject &feature, int position = -1) {
    if (position < 0) position = view.storyboardPosition();
    for (int sketch : ForgeCad::sketchesOf(feature)) if (sketch >= 0 && sketch < view.sketches().size()
        && view.storyboardIndex(true, sketch) >= position)
        return QStringLiteral("Lo schizzo %1 deve precedere la lavorazione: sposta il cursore o riordina lo schizzo.").arg(view.sketches().at(sketch).name);
    return {};
}
}

DocumentState CadViewport::currentDocument() const { return documentState(); }

DocumentState CadViewport::recoveryDocument() const { return documentState(false); }

bool CadViewport::recoverySnapshotReady() const {
    return !interactionLocked_ && !responsiveWork_ && !pointDragActive_ && !draggingControlPoint_
        && bodyDragSegment_<0 && circleRadiusDrag_<0 && !featureHandleDragging_ && !planeResizing_;
}

bool CadViewport::hasPendingSketchChanges() const { return !deferredSketches_.isEmpty(); }

void CadViewport::adoptDocumentIdentity(const ForgeCad::DocumentVersion &identity, const QUuid &base) {
    documentVersion_ = identity; documentBaseCheckinId_ = base;
}

int CadViewport::rebuildAllFeatures() {
    if(documentReadOnly_)return -1;
    if (extrusions_.isEmpty()) return 0;
    recordUndo();
    // Rigenerare non cambia il contratto dei risultati storici: le feature
    // successive possono riferirsi a facce/bordi dell'intero aggregato.
    // Solo le ripetizioni create nel nuovo formato espongono componenti.
    for (ExtrusionObject &body : extrusions_) body.cachedGeometry = false;
    regenerateAll();
    documentChanged();
    int failed = 0;
    for (const ExtrusionObject &body : extrusions_) failed += !body.suppressed && !body.error.isEmpty() ? 1 : 0;
    return failed;
}

LengthUnit CadViewport::lengthUnit() const { return lengthUnit_; }

void CadViewport::setLengthUnit(LengthUnit unit) {
    if(documentReadOnly_)return;
    if (unit == lengthUnit_) return;
    lengthUnit_ = unit;
    ForgeCad::setDisplayLengthUnit(unit, this);
    documentChanged();
}

void CadViewport::loadDocument(DocumentState state,
                      const std::function<void(int, int, const QString &)> &progress ) {
    ScopedWork work(workCallback_, QStringLiteral("Caricamento e rigenerazione del documento..."));
    if (sketchMode_) endSketchMode();
    deferredSketches_.clear();
    if(!state.pendingSketchChanges.isEmpty())for(auto &body:state.extrusions)body.cachedGeometry=false;
    activeSketch_ = -1;
    selection_ = {};
    history_.clear();
    adoptDocumentIdentity(state.version, state.baseCheckinId);
    if (state.orientationSet) camera_.orientation_ = state.orientation;
    lengthUnit_ = state.lengthUnitSet ? state.lengthUnit : LengthUnit::Millimeter;
    ForgeCad::setDisplayLengthUnit(lengthUnit_, this);
    ForgeCad::normalizePatternResultOrder(state.sketches, state.extrusions);
    ForgeCad::normalizeModelHistory(state);
    sketches_ = std::move(state.sketches);
    storyPosition_ = -1;
    insertionSketchSourceSize_ = -1;
    historyPosition_ = -1;
    insertionSourceSize_ = -1;
    extrusions_ = std::move(state.extrusions);
    modelBodies_ = std::move(state.modelBodies);
    std::vector<CurveObject *> curveJobs;
    for (SketchObject &sketch : sketches_) {
        CurveObject *curves = sketch.curves.data();  // detach dei QVector prima dei worker
        for (int i = 0; i < sketch.curves.size(); ++i) curveJobs.push_back(curves + i);
    }
    ForgeCad::Kernel::parallelFor(curveJobs.size(), ForgeCad::Kernel::threadCount(0), [&](std::size_t i) {
        ForgeCad::recalculateCurve(*curveJobs[i], tessellationQuality_);
    });
    const int totalBodies = qMax(1, int(extrusions_.size()));
    if (progress) progress(0, totalBodies, QStringLiteral("Preparazione degli schizzi..."));
    regenerateAll(progress);
    if (progress && extrusions_.isEmpty()) progress(1, 1, QStringLiteral("Preparazione della scena..."));
    restoreDocument(documentState());
    fitAll();
}

void CadViewport::loadPreviewDocument(DocumentState state) {
    activeSketch_ = -1;
    selection_ = {};
    adoptDocumentIdentity(state.version, state.baseCheckinId);
    if (state.orientationSet) camera_.orientation_ = state.orientation;
    lengthUnit_ = state.lengthUnitSet ? state.lengthUnit : LengthUnit::Millimeter;
    ForgeCad::setDisplayLengthUnit(lengthUnit_, this);
    ForgeCad::normalizePatternResultOrder(state.sketches, state.extrusions);
    ForgeCad::normalizeModelHistory(state);
    sketches_ = std::move(state.sketches);
    storyPosition_ = -1;
    insertionSketchSourceSize_ = -1;
    historyPosition_ = -1;
    insertionSourceSize_ = -1;
    extrusions_ = std::move(state.extrusions);
    modelBodies_ = std::move(state.modelBodies);
    for (SketchObject &sketch : sketches_)
        for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, 0);
    for (ExtrusionObject &body : extrusions_) {
        if (body.forgeBody) {
            body.solid = !body.forgeBody->isSheet();
            if (body.display.quality != 0) ForgeCad::tessellateGeometry(body, 0, body.display);
        } else {
            body.display = {};
        }
    }
    sceneBoundsDirty_ = true;
    setViewPreset(4);
    fitAll();
    update();
}

QVector<ForgeCad::ExportBody> CadViewport::exportableBodies() const {
    QVector<ForgeCad::ExportBody> result;
    for (int index = 0; index < extrusions_.size(); ++index) {
        const ExtrusionObject &body = extrusions_.at(index);
        if (body.visible && body.error.isEmpty() && (body.forgeBody || body.curve))
            result.append({body.name, body.forgeBody, body.curve, meshColorForFeature(index)});
        // Curve proiettate: le curve oltre la prima come voci a parte.
        for (int k = 1; body.visible && body.error.isEmpty() && k < body.curves.size(); ++k)
            result.append({QStringLiteral("%1 (%2)").arg(body.name).arg(k + 1), nullptr, body.curves.at(k), meshColorForFeature(index)});
    }
    return result;
}

ForgeCad::StlExportOptions CadViewport::suggestedStlOptions() const {
    QVector3D minimum, maximum;
    bool found = false;
    const auto add = [&](const QVector3D &point) {
        if (!found) minimum = maximum = point, found = true;
        else {
            minimum.setX(qMin(minimum.x(), point.x())); minimum.setY(qMin(minimum.y(), point.y())); minimum.setZ(qMin(minimum.z(), point.z()));
            maximum.setX(qMax(maximum.x(), point.x())); maximum.setY(qMax(maximum.y(), point.y())); maximum.setZ(qMax(maximum.z(), point.z()));
        }
    };
    for (const ExtrusionObject &body : extrusions_) {
        if (!body.visible || !body.error.isEmpty() || !body.forgeBody) continue;
        for (const QVector3D &point : body.display.vertices) add(point);
        for (const QVector<QVector3D> &edge : body.display.edges)
            for (const QVector3D &point : edge) add(point);
    }
    const double diagonal = found ? qMax(1e-6, double((maximum - minimum).length())) : 100.0;
    ForgeCad::StlExportOptions options;
    options.maxEdgeLength = diagonal / 50.0;
    options.deflection = diagonal / 2000.0;
    options.angle = 10.0;
    return options;
}

int CadViewport::displayMode() const { return displayMode_; }

bool CadViewport::hiddenEdgesVisible() const { return hiddenEdgesVisible_; }

int CadViewport::lightingPreset() const { return lightingPreset_; }

int CadViewport::tessellationQuality() const { return tessellationQuality_; }

bool CadViewport::wheelZoomEnabled() const { return wheelZoomEnabled_; }

bool CadViewport::referencePlanesVisible() const { return referencePlanesVisible_; }

bool CadViewport::gridVisible() const { return gridVisible_; }

const QVector<SketchObject> &CadViewport::sketches() const { return sketches_; }

const QVector<ExtrusionObject> &CadViewport::extrusions() const { return extrusions_; }

const QVector<ModelBody> &CadViewport::modelBodies() const { return modelBodies_; }

int CadViewport::historyPosition() const { return historyPosition_ < 0 ? int(extrusions_.size()) : historyPosition_; }

bool CadViewport::isBodyDrawn(int index, bool includeSketchResults) const {
    if(index<0 || index>=extrusions_.size())return false;
    bool drawn = extrusions_.at(index).visible;
    // La scelta in modifica usa la scena prima della feature, anche dopo
    // Azzera/anteprima fallita. Non altera i flag salvati del documento.
    if (refPicking_ && refPickOwner_ >= 0 && ForgeCad::isShapeBody(extrusions_.at(index)))
        drawn = refPickContextBodies_.contains(index);
    if(sketchMode_ && sketchContextEnd_>=0) {
        const auto &bodies = includeSketchResults ? sketchDisplayBodies_ : sketchContextBodies_;
        drawn = index<bodies.size() && bodies.at(index);
    }
    return drawn;
}

// Le mesh richieste vengono calcolate su copie separate; solo il thread GUI
// pubblica le cache. La vista e gli snapshot restano leggibili durante i worker.
void CadViewport::ensureBodyDisplay(int index) const {
    ensureBodyDisplays({index});
}

void CadViewport::ensureVisibleBodyDisplays() const {
    QVector<int> requested;
    for (int index=0;index<extrusions_.size();++index)
        if (isBodyDrawn(index)) requested.append(index);
    ensureBodyDisplays(requested);
}

void CadViewport::ensureBodyDisplays(const QVector<int> &indices) const {
    auto *self=const_cast<CadViewport *>(this);
    // Un paint durante il worker usa la scena precedente senza avviare altro lavoro.
    if (self->responsiveWork_) return;
    QSet<int> requested;
    const auto collect = [&](auto &&visit, int index) -> void {
        if (index<0 || index>=extrusions_.size() || requested.contains(index)) return;
        const auto &body=extrusions_.at(index);
        if (body.display.quality==tessellationQuality_ || (!body.forgeBody && !body.curve)) return;
        requested.insert(index);
        // Le ripetizioni possono usare la mesh della base anche quando e' nascosta.
        if (body.feature==BodyFeature::Pattern && body.firstBody>=0 && body.firstBody<index)
            visit(visit,body.firstBody);
    };
    for (int index:indices) collect(collect,index);
    if (requested.isEmpty()) return;
    auto ordered=requested.values();
    std::sort(ordered.begin(),ordered.end());
    struct MeshWork {
        int index;
        ExtrusionObject source;
        BodyDisplay display;
    };
    std::vector<MeshWork> pending;
    pending.reserve(ordered.size());
    bool replacesDisplay=false;
    for (int index:ordered) {
        const auto &old=extrusions_.at(index).display;
        replacesDisplay=replacesDisplay || !old.vertices.isEmpty() || !old.edges.isEmpty() || bool(old.instancedBase);
        pending.push_back({index,extrusions_.at(index),{}});
        // Non trattenere una seconda copia delle vecchie mesh fino alla fine del gruppo.
        pending.back().source.display={};
    }
    const int quality=tessellationQuality_;
    const auto cancellation=ForgeCad::Kernel::currentCancellation;
    const auto cores=std::max(1u,ForgeCad::Kernel::threadCount(0));
    // Le mesh nuove sono memoria finale della scena: coda continua per non
    // attendere il corpo piu' lento a ogni gruppo. Nel cambio qualita' i gruppi
    // limitano invece la sovrapposizione delle vecchie cache con le nuove.
    // Il limite e' in risultati in attesa, non in byte.
    const std::size_t wave=replacesDisplay ? std::size_t(cores)*4 : pending.size();
    DeferredWork work(self,QStringLiteral("Preparazione di %1 mesh...").arg(pending.size()));
    QElapsedTimer timer;timer.start();
    for (std::size_t offset=0;offset<pending.size();offset+=wave) {
        const auto count=std::min(wave,pending.size()-offset);
        // Con pochi corpi resta disponibile il parallelismo interno sulle facce.
        const unsigned threads=count>=cores ? cores : 1u;
        self->runWhileResponsive([&] {
            ForgeCad::Kernel::CancellationScope scope(cancellation);
            ForgeCad::Kernel::parallelFor(count,threads,[&](std::size_t k) {
                auto &item=pending[offset+k];
                ForgeCad::tessellateGeometry(item.source,quality,item.display);
            });
        });
        if (self->tessellationQuality_!=quality) return;
        // La base di una ripetizione ha indice minore: pubblicarla per prima.
        for (std::size_t k=0;k<count;++k) {
            auto &item=pending[offset+k];
            if (item.index>=self->extrusions_.size()) continue;
            const auto &current=self->extrusions_.at(item.index);
            if (current.featureId!=item.source.featureId || current.forgeBody!=item.source.forgeBody
                || current.curve!=item.source.curve || current.curves!=item.source.curves) continue;
            self->extrusions_[item.index].display=std::move(item.display);
            ForgeCad::configurePatternInstances(self->extrusions_[item.index],item.index,self->sketches_,self->extrusions_);
        }
    }
    if (qEnvironmentVariableIsSet("FORGECAD_PROFILE"))
        std::fprintf(stderr,"PROFILO mesh richieste: %zu corpi, %u worker, %lld ms\n",pending.size(),cores,static_cast<long long>(timer.elapsed()));
}

void CadViewport::refreshSketchContext() {
    sketchContextEnd_=-1;sketchContextBodies_.clear();sketchContextSketches_.clear();sketchDisplayBodies_.clear();
    if(!sketchMode_ || activeSketch_<0 || activeSketch_>=sketches_.size())return;
    const auto steps = storyboard();
    const int stop = std::min(storyboardPosition(), storyboardIndex(true, activeSketch_));
    const int end = ForgeCad::storyFeatureLimit(extrusions_, steps, stop);
    sketchContextSketches_.fill(false, sketches_.size());
    sketchContextSketches_[activeSketch_] = true;
    for (int p = 0; p < stop; ++p) {
        if (steps.at(p).sketch) sketchContextSketches_[steps.at(p).index] = true;
    }
    sketchContextEnd_ = end;
    sketchContextBodies_.fill(false, extrusions_.size());
    for (int i = 0; i < end; ++i) {
        const auto &feature = extrusions_.at(i);
        if (!ForgeCad::isShapeBody(feature)) sketchContextBodies_[i] = feature.visible && !feature.suppressed && feature.error.isEmpty();
    }
    // Gli stadi precedenti esistono gia nel kernel: ripristinare solo la loro
    // visibilita di scena, senza spostare il cursore o modificare preferenze/undo.
    for(int i:resultBodiesBefore(end)) {
        bool visible=true;
        for(const auto &body:modelBodies_)if(body.id==extrusions_.at(i).modelBodyId){visible=body.visible;break;}
        sketchContextBodies_[i]=visible;
    }
    sketchDisplayBodies_ = sketchContextBodies_;
    if (!sketchSessionFeatures_.isEmpty()) {
        // Le conferme della sessione sostituiscono gli stadi di base; le
        // altre lavorazioni future restano escluse, anche dopo Undo/Redo.
        for (const auto &feature : extrusions_)
            if (sketchSessionFeatures_.contains(feature.featureId)
                && !ForgeCad::sketchesOf(feature).contains(activeSketch_))
                sketchSessionFeatures_.remove(feature.featureId);
        for (int i=0;i<extrusions_.size();++i)
            if (ForgeCad::isShapeBody(extrusions_.at(i))) sketchDisplayBodies_[i]=false;
        for (int i : resultBodiesBefore(end, sketchSessionFeatures_)) {
            bool visible=true;
            for (const auto &body : modelBodies_) if (body.id==extrusions_.at(i).modelBodyId) {visible=body.visible;break;}
            sketchDisplayBodies_[i]=visible;
        }
    }
    ensureVisibleBodyDisplays();
}

void CadViewport::applyHistoryPosition() {
    if (historyPosition_ >= 0) {
        QVector<ExtrusionObject> prefix = extrusions_.mid(0, historyPosition());
        QVector<ModelBody> bodies = modelBodies_;
        ForgeCad::normalizeModelHistory(prefix, bodies);
        for (int i = 0; i < extrusions_.size(); ++i)
            extrusions_[i].visible = i < prefix.size() && prefix.at(i).visible;
    }
    // Un operando consumato resta nella storia, ma non e' un corpo
    // indipendente nella posizione corrente. Nasconderlo anche se l'utente
    // lo aveva mostrato durante il rollback, senza perdere la preferenza.
    const auto roots = storyboardRoots();
    consumedBodies_.clear();
    for (ExtrusionObject &feature : extrusions_)
        if (feature.modelBodyId && roots.value(feature.modelBodyId, feature.modelBodyId) != feature.modelBodyId) {
            consumedBodies_.insert(feature.modelBodyId);
            feature.visible = false;
        }
}

void CadViewport::setHistoryPosition(int position) {
    const auto steps = storyboard();
    int p = steps.size();
    for (int i = 0; i < steps.size(); ++i) if (!steps.at(i).sketch && steps.at(i).index >= position) {
        p = i;
        while (p > 0 && steps.at(p-1).sketch) --p;
        break;
    }
    setStoryboardPosition(position >= extrusions_.size() ? steps.size() : p);
}

QVector<StoryStep> CadViewport::storyboard() const { return ForgeCad::modelStory(sketches_, extrusions_); }

int CadViewport::storyboardPosition() const { return storyPosition_ < 0 ? storyboard().size() : storyPosition_; }

int CadViewport::storyboardIndex(bool sketch, int index) const {
    if (!sketch) index = ForgeCad::patternSourceIndex(extrusions_, index);
    const auto steps = storyboard();
    for (int p = 0; p < steps.size(); ++p) if (steps.at(p).sketch == sketch && steps.at(p).index == index) return p;
    return -1;
}

void CadViewport::setStoryboardPosition(int position) {
    const auto steps = storyboard();
    storyPosition_ = position >= steps.size() ? -1 : qBound(0, position, int(steps.size()));
    historyPosition_ = -1;
    if (storyPosition_ >= 0) historyPosition_ = ForgeCad::storyFeatureLimit(extrusions_, steps, storyPosition_);
    for (ExtrusionObject &feature : extrusions_) for (const ModelBody &body : modelBodies_)
        if (feature.modelBodyId == body.id) feature.visible = feature.featureId == body.tipFeatureId && body.visible;
    selection_ = {};
    documentChanged();
}

QString CadViewport::moveStoryboardStep(int from, int to) {
    if (documentReadOnly_) return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (sketchMode_) return QStringLiteral("Termina la modifica dello schizzo prima di riordinare la storia.");
    auto steps = storyboard();
    QVector<int> previousFeatures;
    for (const auto &step : steps) if (!step.sketch) previousFeatures.append(step.index);
    if (from < 0 || to < 0 || from >= steps.size() || to >= steps.size()) return QStringLiteral("Posizione della storia non valida.");
    if (from == to) return {};
    const StoryStep moved = steps.takeAt(from);
    steps.insert(to, moved);
    const QString error = ForgeCad::storySketchDependencyError(sketches_, extrusions_, steps);
    if (!error.isEmpty()) return error;
    if (!moved.sketch) {
        QVector<int> featureOrder;
        for (const auto &step : steps) if (!step.sketch) featureOrder.append(step.index);
        const int destination = featureOrder.indexOf(moved.index);
        const int original = previousFeatures.indexOf(moved.index);
        if (destination != original) {
            const int target = featureOrder.at(destination < original ? destination + 1 : destination - 1);
            return reorderFeatureTo(moved.index, target, steps);
        }
    }
    recordUndo();
    ForgeCad::placeStorySketches(sketches_, extrusions_, steps);
    storyPosition_ = historyPosition_ = -1;
    selection_ = {moved.sketch ? SceneObjectKind::Sketch : SceneObjectKind::Extrusion, moved.index, -1};
    documentChanged();
    return {};
}

QString CadViewport::changeSketchSupport(int index, GeometryRef reference) {
    if (documentReadOnly_) return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (sketchMode_) return QStringLiteral("Termina la modifica dello schizzo prima di sostituire il piano.");
    if (index < 0 || index >= sketches_.size() || (reference.kind != 1 && reference.kind != 5 && reference.kind != 8))
        return QStringLiteral("Scegli un piano di origine, un piano di costruzione o una faccia piana.");
    if (ForgeCad::isBodyRef(reference)) {
        if (reference.index < 0 || reference.index >= extrusions_.size()) return QStringLiteral("Riferimento non valido.");
        reference.featureId = extrusions_.at(reference.index).featureId;
        if (storyboardIndex(false, reference.index) >= storyboardIndex(true, index))
            return QStringLiteral("Il piano di riferimento deve precedere lo schizzo nella storia.");
    }
    ForgeCad::ResolvedRef resolved;
    QString error;
    if (!ForgeCad::resolveGeometryRef(reference, extrusions_.size(), sketches_, extrusions_, resolved, &error) || !resolved.hasPlane)
        return error.isEmpty() ? QStringLiteral("Il riferimento non e' un piano valido.") : error;
    SketchObject next = sketches_.at(index);
    next.plane = reference.kind == 1 ? reference.index : kFacePlane;
    next.datumPlane = reference.kind == 8 ? reference.index : -1;
    next.support = reference.kind != 1 ? std::make_shared<GeometryRef>(reference) : nullptr;
    next.supportError.clear();
    next.customFrame = true;
    if (reference.kind == 1) next.frame = ForgeCad::referenceSketchFrame(reference.index, camera_.orientation_);
    else {
        SketchFrame plane;
        for (int k = 0; k < 3; ++k) { plane.origin[k] = resolved.point[k]; plane.normal[k] = resolved.direction[k]; }
        if (reference.kind == 8) plane = extrusions_.at(reference.index).datumFrame;
        else plane = ForgeCad::faceSketchFrame(resolved.point, resolved.direction, ForgeCad::Kernel::Vec3(0,0,1));
        next.frame = datumSketchFrame(plane, &next.frame);
    }
    next.faceSource = reference.kind == 1 ? QString() : extrusions_.at(reference.index).name;
    recordUndo();
    sketches_[index] = std::move(next);
    regenerateFrom(0, {}, QSet<int>{index});
    selection_ = {SceneObjectKind::Sketch, index, -1};
    documentChanged();
    return {};
}

QVector<int> CadViewport::resultBodiesBefore(int position, const QSet<quint64> &extraFeatures) const {
    const int end = position < 0 || !extraFeatures.isEmpty() ? historyPosition() : std::min(position, historyPosition());
    QHash<quint64, int> last;
    QSet<quint64> consumed;
    for (int index = 0; index < end; ++index) {
        const ExtrusionObject &feature = extrusions_.at(index);
        if (position >= 0 && index >= position && !extraFeatures.contains(feature.featureId)) continue;
        if (!feature.modelBodyId || feature.suppressed || !feature.error.isEmpty() || !feature.forgeBody) continue;
        for (int operand : ForgeCad::hiddenOperands(feature))
            if (operand >= 0 && operand < extrusions_.size() && extrusions_.at(operand).modelBodyId != feature.modelBodyId)
                consumed.insert(extrusions_.at(operand).modelBodyId);
        last.insert(feature.modelBodyId, index);
    }
    QVector<int> result;
    for (auto it = last.cbegin(); it != last.cend(); ++it)
        if (!consumed.contains(it.key()) && extrusions_.at(it.value()).forgeBody && ForgeCad::isShapeBody(extrusions_.at(it.value())))
            result.append(it.value());
    std::sort(result.begin(), result.end());
    return result;
}

QHash<quint64, quint64> CadViewport::storyboardRoots() const {
    QHash<quint64, quint64> parent;
    for (int index = 0; index < historyPosition(); ++index) {
        const ExtrusionObject &feature = extrusions_.at(index);
        if (!feature.modelBodyId || feature.suppressed || !feature.error.isEmpty() || !feature.forgeBody) continue;
        for (int operand : ForgeCad::hiddenOperands(feature)) {
            if (operand < 0 || operand >= extrusions_.size()) continue;
            const quint64 consumed = extrusions_.at(operand).modelBodyId;
            if (consumed && consumed != feature.modelBodyId && !parent.contains(consumed)) parent.insert(consumed, feature.modelBodyId);
        }
    }
    QHash<quint64, quint64> roots;
    for (const ModelBody &body : modelBodies_) {
        quint64 root = body.id;
        for (int guard = 0; parent.contains(root) && guard <= modelBodies_.size(); ++guard) root = parent.value(root);
        roots.insert(body.id, root);
    }
    return roots;
}

QVector<int> CadViewport::featureSketches(int index) const {
    return index >= 0 && index < extrusions_.size() ? ForgeCad::sketchesOf(extrusions_.at(index)) : QVector<int>();
}

void CadViewport::setModelBodyVisible(int index, bool visible) {
    if(documentReadOnly_)return;
    if (index < 0 || index >= modelBodies_.size() || modelBodies_.at(index).visible == visible) return;
    recordUndo();
    ModelBody &model = modelBodies_[index];
    model.visible = visible;
    for (ExtrusionObject &feature : extrusions_)
        if (feature.modelBodyId == model.id) feature.visible = feature.featureId == model.tipFeatureId && visible;
    if (!visible && selection_.kind == SceneObjectKind::Extrusion
        && selection_.index >= 0 && selection_.index < extrusions_.size()
        && extrusions_.at(selection_.index).modelBodyId == model.id) selection_ = {};
    documentChanged();
}

QColor CadViewport::modelBodyMeshColor(int index) const {
    if (index < 0 || index >= modelBodies_.size()) return QColor();
    const ModelBody &model = modelBodies_.at(index);
    if (model.meshColor.isValid()) return model.meshColor;
    const int tip = modelBodyTip(index);
    return tip >= 0 && !extrusions_.at(tip).solid ? QColor::fromRgbF(0.20, 0.80, 0.95)
                                                  : QColor::fromRgbF(0.25, 0.65, 0.90);
}

void CadViewport::setModelBodyMeshColor(int index, const QColor &color) {
    if(documentReadOnly_)return;
    if (index < 0 || index >= modelBodies_.size() || !color.isValid()) return;
    const QColor opaque(color.red(), color.green(), color.blue());
    if (modelBodies_.at(index).meshColor == opaque) return;
    recordUndo();
    modelBodies_[index].meshColor = opaque;
    documentChanged();
}

int CadViewport::modelBodyIndexForFeature(int feature) const {
    if (feature < 0 || feature >= extrusions_.size()) return -1;
    const quint64 id = extrusions_.at(feature).modelBodyId;
    for (int index = 0; index < modelBodies_.size(); ++index)
        if (modelBodies_.at(index).id == id) return index;
    return -1;
}

QColor CadViewport::meshColorForFeature(int feature) const {
    const int model = modelBodyIndexForFeature(feature);
    if (model >= 0) return modelBodyMeshColor(model);
    return feature >= 0 && feature < extrusions_.size() && !extrusions_.at(feature).solid
        ? QColor::fromRgbF(0.20, 0.80, 0.95) : QColor::fromRgbF(0.25, 0.65, 0.90);
}

QString CadViewport::renameModelBody(int index, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const QString clean = name.trimmed();
    if (index < 0 || index >= modelBodies_.size()) return QStringLiteral("Corpo non valido.");
    if (clean.isEmpty()) return QStringLiteral("Il nome non puo' essere vuoto.");
    if (modelBodies_.at(index).name == clean) return {};
    recordUndo();
    modelBodies_[index].name = clean;
    documentChanged();
    return {};
}

int CadViewport::modelBodyTip(int index) const {
    if (index < 0 || index >= modelBodies_.size()) return -1;
    if (historyPosition_ >= 0) {
        const quint64 bodyId = modelBodies_.at(index).id;
        for (int feature = historyPosition() - 1; feature >= 0; --feature) {
            const ExtrusionObject &stage = extrusions_.at(feature);
            if (stage.modelBodyId == bodyId && !stage.suppressed && stage.error.isEmpty() && stage.forgeBody) return feature;
        }
        return -1;
    }
    const quint64 featureId = modelBodies_.at(index).tipFeatureId;
    for (int feature = 0; feature < extrusions_.size(); ++feature)
        if (extrusions_.at(feature).featureId == featureId) return feature;
    return -1;
}

void CadViewport::deleteModelBody(int index) {
    if (index < 0 || index >= modelBodies_.size()) return;
    const quint64 id = modelBodies_.at(index).id;
    QVector<SceneSelection> features;
    for (int feature = 0; feature < extrusions_.size(); ++feature)
        if (extrusions_.at(feature).modelBodyId == id) features.append({SceneObjectKind::Extrusion, feature, -1});
    deleteObjects(features);
}

QString CadViewport::setFeatureSuppressed(int index, bool suppressed) {
    if (index < 0 || index >= extrusions_.size()) return QStringLiteral("Feature non valida.");
    if (!extrusions_.at(index).modelBodyId) return QStringLiteral("Questa geometria di riferimento non appartiene a una storyboard.");
    return setFeaturesSuppressed({index}, suppressed);
}

QString CadViewport::setFeaturesSuppressed(const QVector<int> &indices, bool suppressed) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    QVector<int> list;
    for (int selected : indices) {
        const int root = ForgeCad::patternSourceIndex(extrusions_, selected);
        for (int index = 0; index < extrusions_.size(); ++index)
            if (ForgeCad::patternSourceIndex(extrusions_, index) == root && extrusions_.at(index).modelBodyId
                && extrusions_.at(index).suppressed != suppressed && !list.contains(index)) list.append(index);
    }
    if (list.isEmpty()) return {};
    std::sort(list.begin(), list.end());
    QHash<quint64, bool> bodyVisible;
    for (int index : list) {
        const quint64 bodyId = extrusions_.at(index).modelBodyId;
        bool visible = true;
        for (const ModelBody &body : modelBodies_)
            if (body.id == bodyId) { visible = body.visible; break; }
        bodyVisible.insert(bodyId, visible);
    }
    recordUndo();
    for (int index : list) {
        ExtrusionObject &feature = extrusions_[index];
        const quint64 bodyId = feature.modelBodyId;
        const QVector<int> consumed = ForgeCad::hiddenOperands(feature);
        feature.suppressed = suppressed;
        for (int operand : consumed) {
            if (operand < 0 || operand >= extrusions_.size() || extrusions_.at(operand).modelBodyId == bodyId) continue;
            bool consumedElsewhere = false;
            if (suppressed) {
                for (int other = 0; other < extrusions_.size() && !consumedElsewhere; ++other)
                    consumedElsewhere = other != index && !extrusions_.at(other).suppressed
                                     && ForgeCad::hiddenOperands(extrusions_.at(other)).contains(operand);
            }
            extrusions_[operand].visible = suppressed && !consumedElsewhere;
        }
    }
    if (list.size() == 1) {
        rebuildBody(extrusions_[list.first()], list.first());
        regenerateAfter(list.first());
    } else {
        regenerateAll();
    }
    int lastTip = -1;
    for (auto it = bodyVisible.cbegin(); it != bodyVisible.cend(); ++it) {
        int tip = -1;
        for (int candidate = extrusions_.size() - 1; candidate >= 0; --candidate)
            if (extrusions_.at(candidate).modelBodyId == it.key() && !extrusions_.at(candidate).suppressed) { tip = candidate; break; }
        for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
            if (extrusions_.at(candidate).modelBodyId == it.key()) extrusions_[candidate].visible = candidate == tip && it.value();
        if (tip >= 0) lastTip = tip;
    }
    selection_ = lastTip >= 0 ? SceneSelection{SceneObjectKind::Extrusion, lastTip, -1} : SceneSelection{};
    documentChanged();
    return {};
}

void CadViewport::deleteFeature(int index) { deleteFeatures({index}); }

void CadViewport::deleteFeatures(const QVector<int> &indices) {
    if(documentReadOnly_)return;
    QVector<int> list;
    for (int selected : indices) {
        const int root = ForgeCad::patternSourceIndex(extrusions_, selected);
        for (int index = 0; index < extrusions_.size(); ++index)
            if (ForgeCad::patternSourceIndex(extrusions_, index) == root && extrusions_.at(index).modelBodyId
                && !list.contains(index)) list.append(index);
    }
    if (list.isEmpty()) return;
    std::sort(list.begin(), list.end(), std::greater<int>());  // dall'ultima: gli indici prima non cambiano
    QHash<quint64, bool> bodyVisible;
    for (int index : list) {
        const quint64 bodyId = extrusions_.at(index).modelBodyId;
        bool visible = true;
        for (const ModelBody &body : modelBodies_)
            if (body.id == bodyId) { visible = body.visible; break; }
        bodyVisible.insert(bodyId, visible);
    }
    recordUndo();
    for (int index : list) {
        const quint64 bodyId = extrusions_.at(index).modelBodyId;
        int previous = -1;
        for (int candidate = index - 1; candidate >= 0; --candidate)
            if (extrusions_.at(candidate).modelBodyId == bodyId) { previous = candidate; break; }
        // La base implicita del ramo prosegue dallo stadio precedente. I
        // riferimenti geometrici espliciti restano invece invalidi e vengono
        // segnalati dalla rigenerazione, senza cancellare le feature dipendenti.
        for (int candidate = index + 1; candidate < extrusions_.size(); ++candidate) {
            ExtrusionObject &dependent = extrusions_[candidate];
            if (dependent.modelBodyId != bodyId) continue;
            if (dependent.firstBody == index) dependent.firstBody = previous;
            for (int &merged : dependent.mergeBodies)
                if (merged == index) merged = previous;
        }
        removeBodies({index});
    }
    for (auto it = bodyVisible.cbegin(); it != bodyVisible.cend(); ++it) {
        int tip = -1;
        for (int candidate = extrusions_.size() - 1; candidate >= 0; --candidate)
            if (extrusions_.at(candidate).modelBodyId == it.key() && !extrusions_.at(candidate).suppressed) { tip = candidate; break; }
        for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
            if (extrusions_.at(candidate).modelBodyId == it.key()) extrusions_[candidate].visible = candidate == tip && it.value();
    }
    regenerateAll();
    selection_ = {};
    documentChanged();
}

QString CadViewport::moveFeature(int index, int direction) {
    index = ForgeCad::patternSourceIndex(extrusions_, index);
    QVector<int> features;
    for (int f = 0; f < extrusions_.size(); ++f)
        if (extrusions_.at(f).modelBodyId && ForgeCad::patternSourceIndex(extrusions_, f) == f) features.append(f);
    const int from = features.indexOf(index), to = from + (direction < 0 ? -1 : 1);
    if (from < 0 || to < 0 || to >= features.size()) return QStringLiteral("Non ci sono altre feature in quella direzione.");
    return moveFeatureTo(index, features.at(to));
}

QString CadViewport::moveFeatureTo(int index, int target) {
    return moveStoryboardStep(storyboardIndex(false, index), storyboardIndex(false, target));
}

QString CadViewport::reorderFeatureTo(int index, int target, const QVector<StoryStep> &steps) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    // La storia e' una sola sequenza: il bersaglio puo' essere una feature
    // di un altro corpo, conta solo che le dipendenze restino prima.
    if (index < 0 || index >= extrusions_.size() || target < 0 || target >= extrusions_.size()
        || !extrusions_.at(index).modelBodyId)
        return QStringLiteral("Feature non valida.");
    if (index == target) return {};
    const quint64 bodyId = extrusions_.at(index).modelBodyId;
    int first = -1;
    for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
        if (extrusions_.at(candidate).modelBodyId == bodyId) { first = candidate; break; }
    if (index == first) return QStringLiteral("La feature che crea il corpo non si sposta: sposta le feature che la seguono.");
    bool bodyVisible = true;
    for (const ModelBody &body : modelBodies_)
        if (body.id == bodyId) { bodyVisible = body.visible; break; }

    QVector<int> order;
    order.reserve(extrusions_.size());
    QVector<int> group;
    for (int old = 0; old < extrusions_.size(); ++old)
        if (ForgeCad::patternSourceIndex(extrusions_, old) == index) group.append(old);
        else order.append(old);
    // `target` e' una feature, non una posizione nel vettore: gli stadi
    // di corpi diversi possono essere intercalati. Salendo si inserisce
    // prima del bersaglio, scendendo dopo, come indica il drag nell'albero.
    int insertion = order.indexOf(target);
    if (insertion < 0) return QStringLiteral("La feature di destinazione non esiste piu'.");
    if (index < target) {
        ++insertion;
        while (insertion < order.size() && ForgeCad::patternSourceIndex(extrusions_, order.at(insertion)) == target) ++insertion;
    }
    for (int part : group) order.insert(insertion++, part);
    QVector<int> map(extrusions_.size(), -1);
    QVector<ExtrusionObject> reordered;
    reordered.reserve(extrusions_.size());
    for (int old : order) {
        map[old] = reordered.size();
        reordered.append(extrusions_.at(old));
    }
    const auto mapped = [&](int old) { return old >= 0 ? map.value(old, -1) : old; };
    ForgeCad::remapFeatureBodies(reordered, mapped);

    for (const ExtrusionObject &feature : reordered)
        if (feature.modelBodyId == bodyId) {
            if (feature.featureId != extrusions_.at(first).featureId)
                return QStringLiteral("Spostamento non consentito: \"%1\" verrebbe prima di \"%2\", che crea il suo corpo.")
                    .arg(extrusions_.at(index).name, extrusions_.at(first).name);
            break;
        }
    // Le basi interne del corpo sono implicite nella storyboard: dopo il
    // riordino ogni modificatore prende lo stadio immediatamente precedente.
    int previous = -1;
    for (int current = 0; current < reordered.size(); ++current) {
        ExtrusionObject &feature = reordered[current];
        if (feature.modelBodyId != bodyId) continue;
        if (previous >= 0) {
            if (feature.operation >= 0 || feature.feature == BodyFeature::Blend || feature.feature == BodyFeature::SheetTrim
                || feature.feature == BodyFeature::SheetExtend || (feature.feature==BodyFeature::SurfaceTools&&feature.surfaceTool.mode!=1) || feature.feature == BodyFeature::Scale
                || (feature.feature == BodyFeature::Transform && !feature.move.copy) || feature.feature == BodyFeature::Pattern
                || feature.feature == BodyFeature::Sew || feature.feature == BodyFeature::DeleteFace
                || feature.feature == BodyFeature::Draft || feature.feature == BodyFeature::Shell || feature.feature == BodyFeature::Thread
                || (feature.feature == BodyFeature::Thicken && feature.offsetFaces.isEmpty())) {
                feature.firstBody = previous;
            } else if (ForgeCad::mergingFeature(feature) && feature.mergeOperation != 0) {
                bool replaced = false;
                for (int &merged : feature.mergeBodies)
                    if (merged >= 0 && reordered.at(merged).modelBodyId == bodyId) { merged = previous; replaced = true; break; }
                if (!replaced) feature.mergeBodies.prepend(previous);
            }
        }
        previous = current;
    }
    for (int current = 0; current < reordered.size(); ++current)
        for (int operand : ForgeCad::bodyOperands(reordered.at(current)))
            if (operand >= current)
                return QStringLiteral("Spostamento non consentito: \"%1\" deve restare dopo una feature da cui dipende.")
                    .arg(reordered.at(current).name);

    recordUndo();
    ForgeCad::placeStorySketches(sketches_, extrusions_, steps);
    storyPosition_ = historyPosition_ = -1;
    extrusions_ = std::move(reordered);
    int tip = -1;
    for (int candidate = extrusions_.size() - 1; candidate >= 0; --candidate)
        if (extrusions_.at(candidate).modelBodyId == bodyId && !extrusions_.at(candidate).suppressed) { tip = candidate; break; }
    for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
        if (extrusions_.at(candidate).modelBodyId == bodyId) extrusions_[candidate].visible = candidate == tip && bodyVisible;
    ForgeCad::remapSketchSupports(sketches_, mapped);
    regenerateAll();
    const int moved = map.value(index, -1);
    selection_ = moved >= 0 ? SceneSelection{SceneObjectKind::Extrusion, moved, -1} : SceneSelection{};
    documentChanged();
    return {};
}

bool CadViewport::canUndo() const { return !documentReadOnly_&&history_.canUndo(); }

bool CadViewport::canRedo() const { return !documentReadOnly_&&history_.canRedo(); }

void CadViewport::undo() {
    if(documentReadOnly_)return;
    ScopedWork work(workCallback_, QStringLiteral("Ripristino dello stato precedente..."));
    DocumentState state = documentState();
    if (history_.undo(state)) {state.version=documentVersion_;state.baseCheckinId=documentBaseCheckinId_;restoreDocument(std::move(state));}
}

void CadViewport::redo() {
    if(documentReadOnly_)return;
    ScopedWork work(workCallback_, QStringLiteral("Ripristino dello stato successivo..."));
    DocumentState state = documentState();
    if (history_.redo(state)) {state.version=documentVersion_;state.baseCheckinId=documentBaseCheckinId_;restoreDocument(std::move(state));}
}

bool CadViewport::isObjectVisible(SceneObjectKind kind, int index) const {
    if (kind == SceneObjectKind::Sketch && index >= 0 && index < sketches_.size())
        return sketches_.at(index).visible;
    if (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size())
        return extrusions_.at(index).visible;
    return false;
}

void CadViewport::setObjectVisible(SceneObjectKind kind, int index, bool visible) {
    if(documentReadOnly_)return;
    if (isObjectVisible(kind, index) == visible) return;
    if (kind != SceneObjectKind::Sketch && kind != SceneObjectKind::Extrusion) return;
    if ((kind == SceneObjectKind::Sketch && (index < 0 || index >= sketches_.size()))
        || (kind == SceneObjectKind::Extrusion && (index < 0 || index >= extrusions_.size()))) return;
    recordUndo();
    if (kind == SceneObjectKind::Sketch) sketches_[index].visible = visible;
    else extrusions_[index].visible = visible;
    if (!visible) {
        if (selection_ == SceneSelection{kind, index, -1}) selection_ = {};
        if (hover_ == SceneSelection{kind, index, -1}) hover_ = {};
    }
    documentChanged();
}

void CadViewport::setObjectsVisible(const QVector<SceneSelection> &objects, bool visible) {
    if(documentReadOnly_)return;
    QVector<SceneSelection> changed;
    for (const SceneSelection &object : objects)
        if ((object.kind == SceneObjectKind::Sketch && object.index >= 0 && object.index < sketches_.size())
            || (object.kind == SceneObjectKind::Extrusion && object.index >= 0 && object.index < extrusions_.size()))
            if (isObjectVisible(object.kind, object.index) != visible) changed.append(object);
    if (changed.isEmpty()) return;
    recordUndo();
    for (const SceneSelection &object : changed) {
        if (object.kind == SceneObjectKind::Sketch) sketches_[object.index].visible = visible;
        else extrusions_[object.index].visible = visible;
    }
    if (!visible) {
        if (changed.contains(selection_)) selection_ = {};
        if (changed.contains(hover_)) hover_ = {};
    }
    documentChanged();
}

void CadViewport::showAllObjects() {
    if(documentReadOnly_)return;
    bool anyHidden = false;
    for (const SketchObject &sketch : sketches_) anyHidden = anyHidden || !sketch.visible;
    for (const ExtrusionObject &extrusion : extrusions_) anyHidden = anyHidden || !extrusion.visible;
    if (!anyHidden) return;
    recordUndo();
    for (SketchObject &sketch : sketches_) sketch.visible = true;
    for (ExtrusionObject &extrusion : extrusions_) extrusion.visible = true;
    documentChanged();
}

QStringList CadViewport::extrusionNames() const {
    QStringList names;
    for (const ExtrusionObject &extrusion : extrusions_) names.append(extrusion.name);
    return names;
}

QString CadViewport::createBoolean(BooleanOperation operation, int firstIndex, const QVector<int> &tools, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (firstIndex < 0 || firstIndex >= extrusions_.size() || tools.isEmpty()) return QStringLiteral("Scegli il primo oggetto e almeno uno strumento.");
    for (int tool : tools) {
        if (tool < 0 || tool >= extrusions_.size()) return QStringLiteral("Oggetti non validi.");
        if (tool == firstIndex) return QStringLiteral("Il primo oggetto non puo' essere anche uno strumento.");
        if (tools.count(tool) > 1) return QStringLiteral("Uno strumento e' scelto due volte.");
    }
    const ExtrusionObject &first = extrusions_.at(firstIndex);
    for (int tool : tools) {
        const QString invalid = booleanOperandsError(operation, first, extrusions_.at(tool));
        if (!invalid.isEmpty()) return invalid;
    }
    ExtrusionObject result;
    result.name = name;
    result.plane = first.plane;
    result.operation = int(operation);
    result.firstBody = firstIndex;
    result.secondBody = tools.first();
    result.booleanTools = tools.mid(1);
    rebuildBody(result, int(extrusions_.size()));
    if (!ForgeCad::hasGeometry(result)) return result.error;
    recordUndo();
    extrusions_[firstIndex].visible = false;
    for (int tool : tools) extrusions_[tool].visible = false;
    extrusions_.append(result);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QVector<int> CadViewport::mergeCandidates(int index) const {
    QSet<quint64> hidden;
    for (const ModelBody &model : modelBodies_)
        if (!model.visible) hidden.insert(model.id);
    QVector<int> candidates;
    for (int body : resultBodiesBefore(index)) {
        const ExtrusionObject &other = extrusions_.at(body);
        if (!other.solid) continue;
        if (other.modelBodyId ? hidden.contains(other.modelBodyId) : !other.visible) continue;
        candidates.append(body);
    }
    return candidates;
}

ExtrusionObject CadViewport::withMergeCandidates(ExtrusionObject definition, int index) const {
    definition.mergeProbe = false;
    if (ForgeCad::mergingFeature(definition) && definition.mergeOperation != 0 && definition.mergeAuto) {
        definition.mergeBodies = mergeCandidates(index);
        definition.mergeProbe = true;
    }
    return definition;
}

bool CadViewport::unchangedFeature(const ExtrusionObject &definition, int index) const {
    if (index < 0 || index >= extrusions_.size()) return false;
    const ExtrusionObject &existing = extrusions_.at(index);
    if (existing.suppressed || !existing.error.isEmpty() || !existing.forgeBody || ForgeCad::isCurveBody(existing) || ForgeCad::isDatumBody(existing))
        return false;
    ExtrusionObject normalized = definition;
    if (normalized.mergeProbe) normalized.mergeBodies = existing.mergeBodies;
    normalized.mergeProbe = false;
    ExtrusionObject reference = existing;
    reference.mergeProbe = false;
    if (previewKey(normalized, index) == previewKey(reference, index)) return true;
    // Raccordi: gli spigoli scelti di nuovo nella vista hanno punti diversi
    // da quelli salvati anche quando sono gli stessi spigoli; conta lo
    // spigolo (e la faccia) che indicano sulla base.
    if (definition.operation >= 0 || definition.feature != BodyFeature::Blend || existing.feature != BodyFeature::Blend) return false;
    normalized.blendEdges.clear();
    reference.blendEdges.clear();
    if (previewKey(normalized, index) != previewKey(reference, index)) return false;
    return sameBlendEdges(definition.blendEdges, existing);
}

bool CadViewport::sameBlendEdges(const QVector<EdgePoint> &edges, const ExtrusionObject &blend) const {
    if (blend.firstBody < 0 || blend.firstBody >= extrusions_.size() || !extrusions_.at(blend.firstBody).forgeBody) return false;
    const ExtrusionObject &base = extrusions_.at(blend.firstBody);
    ForgeCad::Kernel::Box box;
    for (ForgeCad::Kernel::VertexId v : base.forgeBody->vertices()) box.add(base.forgeBody->vertex(v).point);
    const double reach = 1e-3 * std::max(1.0, box.diagonal());
    const bool sameState = blend.blendBaseFeature != 0 && blend.blendBaseFeature == base.featureId;
    std::vector<ForgeCad::Kernel::EdgeId> picked, stored;
    if (!ForgeCad::resolveBlendEdges(*base.forgeBody, edges, reach, picked, ForgeCad::ReferenceState::Same)
        || !ForgeCad::resolveBlendEdges(*base.forgeBody, blend.blendEdges, reach, stored,
                                        sameState ? ForgeCad::ReferenceState::Same : ForgeCad::ReferenceState::Other))
        return false;
    const auto indices = [](const std::vector<ForgeCad::Kernel::EdgeId> &ids) {
        std::set<int> result;
        for (ForgeCad::Kernel::EdgeId id : ids) result.insert(id.index);
        return result;
    };
    return !picked.empty() && indices(picked) == indices(stored);
}

QString CadViewport::booleanOperandsError(BooleanOperation operation, const ExtrusionObject &first, const ExtrusionObject &second) {
    if (first.solid && second.solid) return {};
    if (!first.solid && !second.solid) return QStringLiteral("Serve almeno un solido chiuso: tra due superfici non si fanno booleane.");
    if (operation == BooleanOperation::Union) return QStringLiteral("L'unione richiede due solidi chiusi.");
    if (operation == BooleanOperation::Difference && first.solid)
        return QStringLiteral("Da un solido si puo' sottrarre solo un solido (una superficie non ha volume).");
    return {};
}

QString CadViewport::updateBody(int index, const ExtrusionObject &definition) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const QString positionError = sketchPositionError(*this, definition, storyboardIndex(false, index));
    if (!positionError.isEmpty()) return positionError;
    if (index < 0 || index >= extrusions_.size()) return QStringLiteral("Corpo non valido.");
    if (definition.operation >= 0) {
        if (definition.firstBody < 0 || definition.secondBody < 0 || definition.firstBody >= index || definition.secondBody >= index)
            return QStringLiteral("Operandi della booleana non validi.");
        for (int tool : QVector<int>{definition.secondBody} + definition.booleanTools) {
            if (tool < 0 || tool >= index || tool == definition.firstBody) return QStringLiteral("Operandi della booleana non validi.");
            const QString invalid = booleanOperandsError(BooleanOperation(definition.operation), extrusions_.at(definition.firstBody), extrusions_.at(tool));
            if (!invalid.isEmpty()) return invalid;
        }
    }
    // Un risultato derivato si modifica attraverso la sua ripetizione madre.
    if (extrusions_.at(index).feature == BodyFeature::Pattern && extrusions_.at(index).patternComponent > 0) {
        const int source = extrusions_.at(index).firstBody;
        if (source < 0 || source >= index) return QStringLiteral("Ripetizione madre non valida.");
        ExtrusionObject changed = extrusions_.at(source);
        changed.pattern = definition.pattern;
        return updateBody(source, changed);
    }
    ExtrusionObject candidate = withMergeCandidates(definition, index);
    if (candidate.feature == BodyFeature::Pattern && candidate.operation < 0)
        candidate.patternComponent = extrusions_.at(index).patternComponent < 0 ? -1 : 0;
    candidate.visible = extrusions_.at(index).visible;
    const bool sameBlendPreview = candidate.operation < 0 && candidate.feature == BodyFeature::Blend
        && preview_.key == previewKey(candidate, index);
    if (sameBlendPreview && !unchangedFeature(candidate, index) && (!preview_.valid || !preview_.geometry))
        return preview_.error.isEmpty() ? QStringLiteral("Attendi che l'anteprima sia pronta.") : preview_.error;
    const bool unchanged = unchangedFeature(candidate, index);
    if (unchanged) {
        // Nessun valore cambiato: il corpo e quelli che ne dipendono restano come sono.
        const ExtrusionObject &existing = extrusions_.at(index);
        candidate.mergeBodies = existing.mergeBodies;
        candidate.forgeBody = existing.forgeBody;
        candidate.solid = existing.solid;
        candidate.display = existing.display;
        candidate.error.clear();
    } else if (sameBlendPreview) {
        candidate.forgeBody = preview_.geometry;
        candidate.solid = true;
        // Il candidato e' una copia della feature: se prima era fallita ne
        // conserverebbe il messaggio, e la storyboard continuerebbe a
        // mostrare lo stadio precedente come risultato del corpo.
        candidate.error.clear();
        candidate.display = preview_.resultDisplay;
        if (candidate.display.vertices.isEmpty() && candidate.display.edges.isEmpty())
            ForgeCad::tessellateGeometry(candidate, tessellationQuality_, candidate.display);
        candidate.display.constructionCurves.clear();
    } else {
        // La stessa definizione dell'anteprima (guscio, offset...): il suo risultato.
        QString previewError;
        if (candidate.feature != BodyFeature::Pattern && takePreviewResult(candidate, index, &previewError)) {
            if (!previewError.isEmpty()) return previewError;
        } else {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            rebuildBody(candidate, index);
            QApplication::restoreOverrideCursor();
        }
    }
    candidate.mergeProbe = false;
    if (!ForgeCad::hasGeometry(candidate)) return candidate.error;
    recordUndo();
    // Gli operandi che la funzione non usa piu' tornano visibili, quelli nuovi si nascondono.
    const QVector<int> before = ForgeCad::hiddenOperands(extrusions_.at(index)), after = ForgeCad::hiddenOperands(candidate);
    for (int body : before)
        if (!after.contains(body) && body >= 0 && body < extrusions_.size()) extrusions_[body].visible = true;
    for (int body : after)
        if (!before.contains(body) && body >= 0 && body < index) extrusions_[body].visible = false;
    // La modifica stacca la feature dal corpo di cui era uno stadio (una
    // sweep o un'estrusione che non si fonde piu', una cucitura con
    // un'altra prima superficie): il corpo logico si riassegna, altrimenti
    // il corpo di prima resterebbe nascosto come stadio intermedio.
    {
        const quint64 before = ForgeCad::inheritedModelBody(extrusions_, extrusions_.at(index));
        if (before != 0 && before != ForgeCad::inheritedModelBody(extrusions_, candidate)) candidate.modelBodyId = 0;
    }
    extrusions_[index] = candidate;
    if (!unchanged) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        regenerateAfter(index);
        QApplication::restoreOverrideCursor();
    }
    synchronizePatternResults();
    selection_ = {SceneObjectKind::Extrusion, index, -1};
    documentChanged();
    return {};
}

QSet<int> CadViewport::withDependentBodies(QSet<int> removed) const {
    for (int index = 0; index < extrusions_.size(); ++index) {
        const ExtrusionObject &body = extrusions_.at(index);
        bool depends = false;
        for (int operand : ForgeCad::bodyOperands(body)) depends = depends || removed.contains(operand);
        if (depends) removed.insert(index);
    }
    return removed;
}

void CadViewport::deleteSelection() {
    if(documentReadOnly_)return;
    if (sketchMode_) {
        if (!selectedConstraints_.isEmpty()) deleteConstraints(selectedConstraints_);
        else deleteSketchElements();
        return;
    }
    // Piu' oggetti scelti con il riquadro (o Maiusc/Ctrl+clic): tutti insieme.
    if (selectedObjects_.size() > 1) {
        deleteObjectsOrFeatures(selectedObjects_);
        return;
    }
    if (selection_.kind == SceneObjectKind::Sketch || selection_.kind == SceneObjectKind::Extrusion)
        deleteObject(selection_.kind, selection_.index);
}

void CadViewport::deleteObjectsOrFeatures(const QVector<SceneSelection> &objects) {
    if(documentReadOnly_)return;
    QVector<int> features;
    bool allFeatures = !objects.isEmpty();
    for (const SceneSelection &object : objects) {
        if (object.kind == SceneObjectKind::Extrusion && object.index >= 0 && object.index < extrusions_.size()
            && extrusions_.at(object.index).modelBodyId != 0)
            features.append(object.index);
        else allFeatures = false;
    }
    if (allFeatures) deleteFeatures(features);
    else deleteObjects(objects);
}

void CadViewport::deleteObject(SceneObjectKind kind, int index) {
    if (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size()
        && extrusions_.at(index).modelBodyId != 0) {
        deleteFeature(index);
        return;
    }
    deleteObjects({{kind, index, -1}});
}

void CadViewport::deleteObjects(const QVector<SceneSelection> &objects) {
    if(documentReadOnly_)return;
    QSet<int> sketchSet, chosenBodies;
    QStringList chosenNames;
    for (const SceneSelection &object : objects) {
        if (object.kind == SceneObjectKind::Sketch && object.index >= 0 && object.index < sketches_.size()) {
            if (!sketchSet.contains(object.index)) chosenNames.append(sketches_.at(object.index).name);
            sketchSet.insert(object.index);
        } else if (object.kind == SceneObjectKind::Extrusion && object.index >= 0 && object.index < extrusions_.size()) {
            if (!chosenBodies.contains(object.index)) chosenNames.append(extrusions_.at(object.index).name);
            chosenBodies.insert(object.index);
        }
    }
    if (sketchSet.isEmpty() && chosenBodies.isEmpty()) return;
    QSet<int> bodies = chosenBodies;
    for (int body = 0; body < extrusions_.size(); ++body)
        for (int sketch : ForgeCad::sketchesOf(extrusions_.at(body)))
            if (sketchSet.contains(sketch)) bodies.insert(body);
    bodies = withDependentBodies(bodies);
    QSet<int> dependents = bodies;
    for (int body : chosenBodies) dependents.remove(body);
    if (!dependents.isEmpty()) {
        QList<int> sorted(dependents.begin(), dependents.end());
        std::sort(sorted.begin(), sorted.end());
        QStringList names;
        for (int body : sorted) names.append(QStringLiteral("  \u2022 ") + extrusions_.at(body).name);
        const QString what = chosenNames.size() == 1 ? QStringLiteral("\"%1\"").arg(chosenNames.first())
                                                     : QStringLiteral("%1 oggetti scelti").arg(chosenNames.size());
        const auto answer = QMessageBox::question(this, QStringLiteral("Elimina"),
            QStringLiteral("Da %1 dipendono:\n%2\n\nEliminare anche questi?").arg(what, names.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }
    if (sketchMode_ && sketchSet.contains(activeSketch_)) endSketchMode();
    recordUndo();
    removeBodies(bodies);
    // Gli schizzi dall'ultimo: gli indici di quelli prima non cambiano.
    QList<int> sketchList(sketchSet.begin(), sketchSet.end());
    std::sort(sketchList.begin(), sketchList.end(), std::greater<int>());
    for (int index : sketchList) removeSketchAt(index);
    selection_ = {};
    selectedObjects_.clear();
    hover_ = {};
    documentChanged();
}

void CadViewport::removeSketchAt(int index) {
    sketches_.removeAt(index);
    const auto renumber = [index](int &sketch) {
        if (sketch > index) --sketch;
        else if (sketch == index) sketch = -1;
    };
    for (ExtrusionObject &body : extrusions_) {
        renumber(body.sketchIndex);
        renumber(body.pathSketch);
        for (int &section : body.loftSketches) renumber(section);
        for (int &guide : body.loftGuides) renumber(guide);
        for (SketchPathRef &guide : body.loftGuidePaths) renumber(guide.sketch);
        for (QVector<GeometryRef> *refs : {&body.datum.refs, &body.pattern.refs, &body.surfaceTool.targets, &body.ruledFirst, &body.ruledSecond, &body.planarRefs, &body.loftStartFaces, &body.loftEndFaces, &body.loftSections, &body.loftEdgeGuides})
            for (GeometryRef &ref : *refs)
                if (ForgeCad::isSketchRef(ref)) renumber(ref.index);
        if (ForgeCad::isSketchRef(body.extentRef)) renumber(body.extentRef.index);
        if (ForgeCad::isSketchRef(body.move.axis)) renumber(body.move.axis.index);
        if (ForgeCad::isSketchRef(body.revolveAxisRef)) renumber(body.revolveAxisRef.index);
        if (ForgeCad::isSketchRef(body.draftNeutral)) renumber(body.draftNeutral.index);
        if (ForgeCad::isSketchRef(body.thickenDirection)) renumber(body.thickenDirection.index);
        for (GeometryRef *ref : {&body.sweepProfileRef, &body.sweepPathRef})
            if (ForgeCad::isSketchRef(*ref)) renumber(ref->index);
        if (ForgeCad::isSketchRef(body.curveExtraction.source)) renumber(body.curveExtraction.source.index);
        if (ForgeCad::isSketchRef(body.curveExtraction.tool)) renumber(body.curveExtraction.tool.index);
    }
    if (activeSketch_ == index) activeSketch_ = -1;
    else if (activeSketch_ > index) --activeSketch_;
}

QString CadViewport::createExtrusion(double distance, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    ExtrusionObject extrusion;
    extrusion.distance = distance;
    extrusion.sketchIndex = activeSketch_;
    return createExtrusion(extrusion, name);
}

QString CadViewport::createExtrusion(const ExtrusionObject &definition, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const int sketch = definition.sketchIndex >= 0 ? definition.sketchIndex : activeSketch_;
    if (sketch < 0 || sketch >= sketches_.size())
        return QStringLiteral("Nessuno schizzo attivo.");
    ExtrusionObject extrusion = withMergeCandidates(definition, int(extrusions_.size()));
    extrusion.operation = -1;
    extrusion.feature = BodyFeature::Extrusion;
    extrusion.name = name;
    extrusion.sketchIndex = sketch;
    const QString positionError = sketchPositionError(*this, extrusion);
    if (!positionError.isEmpty()) return positionError;
    extrusion.plane = sketches_.at(sketch).plane;
    rebuildBody(extrusion, int(extrusions_.size()));
    extrusion.mergeProbe = false;
    if (!ForgeCad::hasGeometry(extrusion)) return extrusion.error;
    recordUndo();
    for (int body : ForgeCad::hiddenOperands(extrusion))
        if (body >= 0 && body < extrusions_.size()) extrusions_[body].visible = false;
    extrusions_.append(extrusion);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    documentChanged();
    return {};
}

QString CadViewport::createRevolution(int sketchIndex, int axis, double angle, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (sketchIndex < 0 || sketchIndex >= sketches_.size()) return QStringLiteral("Nessuno schizzo scelto.");
    ExtrusionObject revolution;
    revolution.name = name;
    revolution.feature = BodyFeature::Revolution;
    revolution.sketchIndex = sketchIndex;
    const QString positionError = sketchPositionError(*this, revolution);
    if (!positionError.isEmpty()) return positionError;
    revolution.plane = sketches_.at(sketchIndex).plane;
    revolution.revolveAxis = axis;
    revolution.revolveAngle = angle;
    rebuildBody(revolution, int(extrusions_.size()));
    if (!ForgeCad::hasGeometry(revolution)) return revolution.error;
    recordUndo();
    extrusions_.append(revolution);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    documentChanged();
    return {};
}

QString CadViewport::createPrimitive(const PrimitiveParameters &parameters, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    ExtrusionObject primitive;
    primitive.name = name;
    primitive.feature = BodyFeature::Primitive;
    primitive.plane = parameters.plane;
    primitive.primitive = parameters;
    rebuildBody(primitive, int(extrusions_.size()));
    if (!ForgeCad::hasGeometry(primitive)) return primitive.error;
    recordUndo();
    extrusions_.append(primitive);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    documentChanged();
    return {};
}

QString CadViewport::createBlend(int baseIndex, const QVector<EdgePoint> &edges, double size, bool chamfer, const QString &name, const ChamferSpec &spec ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (baseIndex < 0 || baseIndex >= extrusions_.size()) return QStringLiteral("Corpo non valido.");
    ExtrusionObject blend;
    blend.featureId = 1;
    for (const auto &feature : extrusions_) blend.featureId = std::max(blend.featureId, feature.featureId + 1);
    blend.name = name;
    blend.feature = BodyFeature::Blend;
    blend.plane = extrusions_.at(baseIndex).plane;
    blend.firstBody = baseIndex;
    blend.blendChamfer = chamfer;
    blend.blendSize = size;
    blend.blendEdges = edges;
    blend.blendBaseFeature = extrusions_.at(baseIndex).featureId;
    blend.chamferSpec = spec;
    // La stessa definizione e' gia' stata costruita dall'anteprima: il
    // B-rep immutabile si promuove direttamente. Durante l'anteprima si
    // tassella soltanto la patch locale; la mesh completa si prepara una
    // sola volta, quando l'utente conferma.
    const QString key = previewKey(blend, -1);
    if (preview_.key == key) {
        if (!preview_.valid || !preview_.geometry)
            return preview_.error.isEmpty() ? QStringLiteral("Attendi che l'anteprima sia pronta.") : preview_.error;
        blend.forgeBody = preview_.geometry;
        blend.solid = !blend.forgeBody->isSheet();
        blend.display = preview_.resultDisplay;
        if (blend.display.vertices.isEmpty() && blend.display.edges.isEmpty())
            ForgeCad::tessellateGeometry(blend, tessellationQuality_, blend.display);
        blend.display.constructionCurves.clear();
    } else {
        rebuildBody(blend, int(extrusions_.size()));
        if (!ForgeCad::hasGeometry(blend)) return blend.error;
    }
    // Anche il B-rep promosso dall'anteprima riceve il tag persistente,
    // conservando le dichiarazioni gia' preparate dal kernel.
    ForgeCad::registerFeatureGenealogy(blend, int(extrusions_.size()), sketches_, extrusions_);
    recordUndo();
    extrusions_[baseIndex].visible = false;
    extrusions_.append(blend);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QString CadViewport::createSheetTrim(int sheet, int tool, int plane, const EdgePoint &keep, const QString &name, int sketch ,
                            bool both , const EdgePoint &toolKeep ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    ExtrusionObject definition;
    definition.firstBody = sheet;
    definition.secondBody = tool;
    definition.trimPlane = plane;
    definition.sketchIndex = sketch;
    definition.trimKeep = keep;
    definition.trimBoth = both;
    definition.trimToolKeep = toolKeep;
    return createSheetTrim(definition, name);
}

QString CadViewport::createSheetTrim(const ExtrusionObject &definition, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const QString positionError = sketchPositionError(*this, definition);
    if (!positionError.isEmpty()) return positionError;
    const int sheet = definition.firstBody, tool = definition.secondBody;
    const bool both = definition.trimBoth;
    if (sheet < 0 || sheet >= extrusions_.size() || tool >= extrusions_.size() || tool == sheet)
        return QStringLiteral("Corpo da tagliare o strumento non validi.");
    ExtrusionObject body;
    body.name = name;
    body.feature = BodyFeature::SheetTrim;
    body.plane = extrusions_.at(sheet).plane;
    body.firstBody = sheet;
    body.secondBody = tool;
    body.trimPlane = definition.trimPlane;
    body.sketchIndex = definition.sketchIndex;
    body.trimKeep = definition.trimKeep;
    body.trimBoth = both;
    body.trimToolKeep = definition.trimToolKeep;
    body.trimProject = definition.trimProject && tool < 0 && definition.sketchIndex >= 0;
    body.projectionMode = definition.projectionMode;
    body.projectionReverse = definition.projectionReverse;
    const QString key = previewKey(body, -1);
    if (preview_.key == key && preview_.valid && preview_.geometry) {
        body.forgeBody = preview_.geometry;
        body.display = preview_.display;
        body.solid = !preview_.geometry->isSheet();
        body.notice = preview_.notice;
    } else {
        rebuildBody(body, int(extrusions_.size()));
    }
    if (!ForgeCad::hasGeometry(body)) return body.error;
    clearPreview();
    recordUndo();
    extrusions_[sheet].visible = false;
    if (both && tool >= 0) extrusions_[tool].visible = false;
    extrusions_.append(body);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QVector<SheetPiece> CadViewport::sheetPieces(int sheet, int tool, int plane, int sketch, QString *error) const {
    ScopedWork work(workCallback_, QStringLiteral("Calcolo delle parti della superficie..."));
    if (sheet < 0 || sheet >= extrusions_.size() || tool >= extrusions_.size() || tool == sheet) {
        if (error) *error = QStringLiteral("Superficie o strumento non validi.");
        return {};
    }
    const ExtrusionObject &target = extrusions_.at(sheet);
    const ExtrusionObject *cutter = tool >= 0 ? &extrusions_.at(tool) : nullptr;
    ForgeCad::ForgeBody geometry;
    if (cutter && cutter->forgeBody) geometry = cutter->forgeBody;
    else if (cutter && cutter->feature == BodyFeature::DatumPlane && cutter->datumValid) {
        const SketchFrame &f = cutter->datumFrame;
        const ForgeCad::Kernel::Frame3 frame(
            ForgeCad::Kernel::Vec3(f.origin[0], f.origin[1], f.origin[2]),
            ForgeCad::Kernel::Vec3(f.normal[0], f.normal[1], f.normal[2]),
            ForgeCad::Kernel::Vec3(f.xAxis[0], f.xAxis[1], f.xAxis[2]));
        geometry = ForgeCad::forgeTrimPlaneTool(target.forgeBody, frame, error);
    }
    else if (sketch >= 0 && sketch < sketches_.size())
        geometry = ForgeCad::forgeTrimSketchTool(target.forgeBody, sketches_.at(sketch), error);
    if ((cutter || sketch >= 0) && !geometry) return {};
    return ForgeCad::forgeSheetPieces(target.forgeBody, geometry, plane, error);
}

QString CadViewport::createSheetExtend(int sheet, const QVector<EdgePoint> &edges, double distance, bool linear, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (sheet < 0 || sheet >= extrusions_.size()) return QStringLiteral("Superficie non valida.");
    ExtrusionObject body;
    body.name = name;
    body.feature = BodyFeature::SheetExtend;
    body.plane = extrusions_.at(sheet).plane;
    body.firstBody = sheet;
    body.blendEdges = edges;
    body.blendSize = distance;
    body.extendLinear = linear;
    // Se il pannello sta mostrando proprio questa estensione, promuove il
    // B-rep e la mesh dell'anteprima. In questo modo non resta un fotogramma
    // in cui la sorgente e' gia' nascosta ma il risultato e' ancora escluso
    // dal disegno perche' appartiene all'anteprima attiva.
    const QString key = previewKey(body, -1);
    if (preview_.key == key) {
        if (!preview_.valid || !preview_.geometry)
            return preview_.error.isEmpty() ? QStringLiteral("Attendi che l'anteprima sia pronta.") : preview_.error;
        body.forgeBody = preview_.geometry;
        body.display = preview_.display;
        body.solid = false;
        if (body.display.vertices.isEmpty() && body.display.edges.isEmpty())
            ForgeCad::tessellateGeometry(body, tessellationQuality_, body.display);
    } else {
        rebuildBody(body, int(extrusions_.size()));
    }
    if (!ForgeCad::hasGeometry(body)) return body.error;
    clearPreview();
    recordUndo();
    extrusions_[sheet].visible = false;
    extrusions_.append(body);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QString CadViewport::createScale(int base, double factor, int mode, const EdgePoint &center, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (base < 0 || base >= extrusions_.size()) return QStringLiteral("Corpo non valido.");
    ExtrusionObject body;
    body.name = name;
    body.feature = BodyFeature::Scale;
    body.plane = extrusions_.at(base).plane;
    body.firstBody = base;
    body.scaleFactor = factor;
    body.scaleCenterMode = mode;
    body.scaleCenter = center;
    rebuildBody(body, int(extrusions_.size()));
    if (!ForgeCad::hasGeometry(body)) return body.error;
    recordUndo();
    extrusions_[base].visible = false;
    extrusions_.append(body);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QString CadViewport::createBody(ExtrusionObject body, const QVector<int> &hide ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const QString positionError = sketchPositionError(*this, body);
    if (!positionError.isEmpty()) return positionError;
    body = withMergeCandidates(body, int(extrusions_.size()));
    if (body.feature == BodyFeature::Pattern && body.operation < 0) body.patternComponent = 0;
    QString previewError;
    if (body.feature != BodyFeature::Pattern && takePreviewResult(body, -1, &previewError)) {
        if (!previewError.isEmpty()) return previewError;
    } else {
        rebuildBody(body, int(extrusions_.size()));
    }
    body.mergeProbe = false;
    if (!ForgeCad::hasGeometry(body)) return body.error;
    recordUndo();
    QVector<int> hidden = hide + ForgeCad::hiddenOperands(body);
    for (int index : hidden)
        if (index >= 0 && index < extrusions_.size()) extrusions_[index].visible = false;
    extrusions_.append(body);
    const int created = extrusions_.size() - 1;
    synchronizePatternResults();
    selection_ = {SceneObjectKind::Extrusion, created, -1};
    hover_ = {};
    documentChanged();
    return {};
}

// Espone ogni componente come corpo selezionabile. Le dipendenze dei risultati
// derivati puntano alla madre: una sola booleana e nessun ricalcolo per copia.
void CadViewport::synchronizePatternResults() {
    QVector<ExtrusionObject> additions;
    QSet<int> removed;
    quint64 nextBody = 1;
    for (const auto &body : extrusions_) nextBody = qMax(nextBody, body.modelBodyId + 1);
    for (const auto &body : modelBodies_) nextBody = qMax(nextBody, body.id + 1);
    for (int root = 0; root < extrusions_.size(); ++root) {
        const auto source = extrusions_.at(root);
        if (source.feature != BodyFeature::Pattern || source.patternComponent != 0
            || source.suppressed || !source.error.isEmpty() || source.patternResults.isEmpty()) continue;
        QSet<int> present;
        for (int i = root + 1; i < extrusions_.size(); ++i) {
            const auto &part = extrusions_.at(i);
            if (part.feature != BodyFeature::Pattern || part.patternComponent <= 0 || part.firstBody != root) continue;
            const int slot = part.patternComponent - 1;
            if (slot < source.patternResults.size() && source.patternResults.at(slot)
                && source.patternResults.at(slot) != source.forgeBody) { present.insert(slot); continue; }
            bool referenced = false;
            for (int j = i + 1; j < extrusions_.size(); ++j)
                if (ForgeCad::bodyOperands(extrusions_.at(j)).contains(i)) referenced = true;
            for (const auto &sketch : sketches_)
                if (sketch.datumPlane == i || (sketch.support && (sketch.support->featureId == part.featureId || sketch.support->index == i))) referenced = true;
            // Mantiene un errore esplicito se una lavorazione usa una componente
            // scomparsa; nessun riferimento viene riassegnato a un'altra copia.
            if (!referenced) removed.insert(i);
        }
        for (int component = 0; component < source.patternResults.size(); ++component) {
            if (!source.patternResults.at(component) || source.patternResults.at(component) == source.forgeBody || present.contains(component)) continue;
            ExtrusionObject part;
            part.feature = BodyFeature::Pattern;
            part.patternComponent = component + 1;
            part.firstBody = root;
            part.modelBodyId = nextBody++;
            part.name = QStringLiteral("%1 — corpo %2").arg(source.name).arg(component + 1);
            part.forgeBody = source.patternResults.at(component);
            part.solid = !part.forgeBody->isSheet();
            tessellateBody(part, extrusions_.size() + additions.size());
            additions.append(std::move(part));
        }
    }
    extrusions_ += additions;
    if (!removed.isEmpty()) removeBodies(removed);
    ForgeCad::normalizeModelHistory(extrusions_, modelBodies_);
    for (int i = 0; i < extrusions_.size(); ++i)
        if (extrusions_.at(i).feature == BodyFeature::Pattern && extrusions_.at(i).patternComponent > 0)
            ForgeCad::registerFeatureGenealogy(extrusions_.at(i), i, sketches_, extrusions_);
}

QString CadViewport::createDeleteFaces(ExtrusionObject definition) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (definition.firstBody < 0 || definition.firstBody >= extrusions_.size()
        || !extrusions_.at(definition.firstBody).forgeBody)
        return QStringLiteral("Il corpo di partenza non ha geometria.");
    QString error;
    const QVector<ForgeCad::ForgeBody> components = ForgeCad::forgeDeleteFacesSeparated(
        extrusions_.at(definition.firstBody).forgeBody, definition.offsetFaces, &error);
    if (components.isEmpty()) return error;

    recordUndo();
    quint64 nextBodyId = 1;
    for (const ExtrusionObject &feature : extrusions_) nextBodyId = qMax(nextBodyId, feature.modelBodyId + 1);
    for (const ModelBody &model : modelBodies_) nextBodyId = qMax(nextBodyId, model.id + 1);
    const quint64 inherited = extrusions_.at(definition.firstBody).modelBodyId;
    const QString commonName = definition.name;
    const int firstNew = extrusions_.size();
    for (int component = 0; component < components.size(); ++component) {
        ExtrusionObject result = definition;
        result.deleteComponent = components.size() > 1 ? component : -1;
        result.forgeBody = components.at(component);
        result.solid = false;
        result.error.clear();
        result.cachedGeometry = false;
        result.featureId = 0;
        result.modelBodyId = component == 0 && inherited ? inherited : nextBodyId++;
        if (components.size() > 1) result.name = QStringLiteral("%1 (%2)").arg(commonName).arg(component + 1);
        tessellateBody(result, extrusions_.size());
        extrusions_.append(std::move(result));
    }
    selection_ = {SceneObjectKind::Extrusion, firstNew, -1};
    hover_ = {};
    documentChanged();
    return {};
}

bool CadViewport::documentReadOnly() const { return documentReadOnly_; }

const ForgeCad::DocumentVersion &CadViewport::documentVersion() const { return documentVersion_; }

const QUuid &CadViewport::documentBaseCheckinId() const { return documentBaseCheckinId_; }

void CadViewport::setDocumentReadOnly(bool readOnly) {
    if(documentReadOnly_==readOnly)return;
    if(readOnly&&sketchMode_)endSketchMode();
    documentReadOnly_=readOnly;update();
}

bool CadViewport::previewDatum(const DatumParameters &parameters, int index, SketchFrame &frame, QString *error) const {
    return ForgeCad::computeDatum(parameters, index < 0 ? int(extrusions_.size()) : index, sketches_, extrusions_, frame, error);
}

int CadViewport::createDatumSketch(int body, const QString &name) {
    if(documentReadOnly_)return -1;
    if (body < 0 || body >= historyPosition() || body >= extrusions_.size() || !ForgeCad::isDatumBody(extrusions_.at(body)) || !extrusions_.at(body).datumValid) return -1;
    if (sketchMode_) endSketchMode();
    if (edgePicking_) cancelEdgePick();
    SketchObject sketch;
    sketch.name = name;
    sketch.plane = kFacePlane;
    sketch.frame = datumSketchFrame(extrusions_.at(body).datumFrame, nullptr);
    sketch.faceSource = extrusions_.at(body).name;
    sketch.datumPlane = body;
    recordUndo();
    sketches_.append(sketch);
    activeSketch_ = sketches_.size() - 1;
    selection_ = {};
    documentChanged();
    beginSketchMode(kFacePlane);
    return activeSketch_;
}

QString CadViewport::readImportFile(const QString &path, QVector<ForgeCad::ImportedPart> &parts, QStringList &notes) {
    QString error;
    runWhileResponsive([&] { error = ForgeCad::importCadFile(path, parts, &notes); });
    return error;
}

QStringList CadViewport::importParts(const QVector<ForgeCad::ImportedPart> &parts, const QString &source,
                            const std::function<void(int, int)> &progress ) {
    if(documentReadOnly_)return {QStringLiteral("Documento in sola lettura: eseguire il check-out.")};
    ScopedWork work(workCallback_, QStringLiteral("Preparazione dei corpi importati..."));
    QStringList failures;
    if (parts.isEmpty()) return failures;
    QVector<ExtrusionObject> prepared;
    prepared.reserve(parts.size());
    const int quality = tessellationQuality_;
    quint64 nextFeatureId=1;
    for(const auto &existing:extrusions_)nextFeatureId=std::max(nextFeatureId,existing.featureId+1);
    std::atomic<int> completed{0};
    QTimer reporter;
    if (progress) {
        progress(0, int(parts.size()));
        QObject::connect(&reporter, &QTimer::timeout, this, [&] { progress(completed.load(), int(parts.size())); });
        reporter.start(100);
    }
    runWhileResponsive([&] {
        for (const ForgeCad::ImportedPart &part : parts) {
            ExtrusionObject body;
            body.name = part.name;
            body.feature = BodyFeature::Imported;
            body.featureId=nextFeatureId++;
            body.importData = part.data;
            body.importSource = source;
            body.forgeBody = part.body;
            body.solid = part.solid;
            if (!body.forgeBody) body.error = QStringLiteral("Il corpo non ha geometria.");
            else {
                // Un secondo inserimento delle stesse parti deve avere
                // nomi propri; la registrazione dei B-rep e' immutabile.
                if(ForgeCad::featureHistoryOf(*body.forgeBody))
                    body.forgeBody=std::make_shared<const ForgeCad::Kernel::Body>(*body.forgeBody);
                ForgeCad::registerImportedHistory(body.forgeBody);
                ForgeCad::registerFeatureGenealogy(body,int(extrusions_.size()+prepared.size()),sketches_,extrusions_);
            }
            ForgeCad::tessellateGeometry(body, quality, body.display);
            prepared.append(std::move(body));
            ++completed;
        }
    });
    reporter.stop();
    if (progress) progress(int(parts.size()), int(parts.size()));
    recordUndo();
    for (ExtrusionObject &body : prepared) {
        if (!body.error.isEmpty()) failures.append(QStringLiteral("%1: %2").arg(body.name, body.error));
        extrusions_.append(std::move(body));
    }
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    fitAll();
    return failures;
}

ForgeCad::MassReport CadViewport::massReport(int index) const {
    ScopedWork work(workCallback_, QStringLiteral("Calcolo delle proprieta' di massa..."));
    ForgeCad::MassReport report;
    if (index < 0 || index >= extrusions_.size() || !ForgeCad::hasGeometry(extrusions_.at(index))) {
        report.error = QStringLiteral("il corpo non ha geometria");
        return report;
    }
    const ExtrusionObject &body = extrusions_.at(index);
    if (ForgeCad::isDatumBody(body)) {
        report.error = QStringLiteral("e' un piano di costruzione");
        return report;
    }
    if (body.curve) return ForgeCad::curveMassProperties(*body.curve);
    return ForgeCad::forgeMassProperties(*body.forgeBody);
}

QPair<int, int> CadViewport::bodyComponents(int index) const {
    if (index < 0 || index >= extrusions_.size() || !ForgeCad::isShapeBody(extrusions_.at(index))) return {0, 0};
    const ForgeCad::ForgeBody &body = extrusions_.at(index).forgeBody;
    const auto found = componentCache_.find(body.get());
    if (found != componentCache_.end() && found->second.first.lock() == body) return found->second.second;
    int solids = 0, sheets = 0;
    ForgeCad::forgeComponentCounts(body, solids, sheets);
    if (componentCache_.size() > 512) componentCache_.clear();
    componentCache_[body.get()] = {body, {solids, sheets}};
    return {solids, sheets};
}

QString CadViewport::renameObject(SceneObjectKind kind, int index, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("Il nome non puo' essere vuoto.");
    if (kind == SceneObjectKind::Sketch && index >= 0 && index < sketches_.size()) {
        if (sketches_.at(index).name == trimmed) return {};
        recordUndo();
        sketches_[index].name = trimmed;
    } else if (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size()) {
        if (extrusions_.at(index).name == trimmed) return {};
        recordUndo();
        extrusions_[index].name = trimmed;
    } else {
        return QStringLiteral("Oggetto non valido.");
    }
    documentChanged();
    return {};
}

DocumentState CadViewport::documentState(bool normalizeVisibility ) const {
    DocumentState state(documentVersion_, documentBaseCheckinId_);
    state.sketches = sketches_;
    state.extrusions = extrusions_;
    state.modelBodies = modelBodies_;
    state.pendingSketchChanges=deferredSketches_.values();
    // La visibilita' del documento conserva la preferenza del corpo;
    // il filtro dei corpi consumati riguarda soltanto la scena corrente.
    if (normalizeVisibility) for (ExtrusionObject &feature : state.extrusions)
        for (const ModelBody &body : state.modelBodies)
            if (feature.modelBodyId == body.id) feature.visible = feature.featureId == body.tipFeatureId && body.visible;
    state.orientation = camera_.orientation_;
    state.orientationSet = true;
    state.lengthUnit = lengthUnit_;
    state.lengthUnitSet = true;
    return state;
}

void CadViewport::selectionChanged() {
    if (sketchOffsetSelectionCallback_) sketchOffsetSelectionCallback_();
    if (constraintPanelCallback_) constraintPanelCallback_();
}

void CadViewport::recordUndo() {
    insertionSourceSize_ = extrusions_.size();
    insertionSketchSourceSize_ = sketches_.size();
    history_.record(documentState());
}

void CadViewport::documentChanged() {
    const int insertionPosition = storyPosition_;
    const int addedFeatures = insertionSourceSize_ < 0 ? 0 : int(extrusions_.size()) - insertionSourceSize_;
    const int addedSketches = insertionSketchSourceSize_ < 0 ? 0 : int(sketches_.size()) - insertionSketchSourceSize_;
    QSet<quint64> newFeatureIds;
    if (addedFeatures > 0) {
        ForgeCad::normalizeModelHistory(extrusions_, modelBodies_);
        for (int i = insertionSourceSize_; i < extrusions_.size(); ++i) newFeatureIds.insert(extrusions_.at(i).featureId);
    }

    for (ExtrusionObject &feature : extrusions_)
        if (historyPosition_ >= 0 || (consumedBodies_.contains(feature.modelBodyId) && !feature.visible))
            for (const ModelBody &body : modelBodies_)
                if (feature.modelBodyId == body.id && feature.featureId == body.tipFeatureId) feature.visible = body.visible;
    // Creation paths append their results. Insert that entire batch at the
    // rollback cursor, preserving persistent IDs and remapping vector indices.
    bool addedOperation = false;
    if (addedFeatures > 0)
        for (int i = insertionSourceSize_; i < extrusions_.size(); ++i)
            if (ForgeCad::patternSourceIndex(extrusions_, i) == i) addedOperation = true;
    if (historyPosition_ >= 0 && insertionSourceSize_ >= 0 && addedOperation) {
        const int start = qMin(historyPosition_, insertionSourceSize_);
        const int added = extrusions_.size() - insertionSourceSize_;
        ForgeCad::normalizeModelHistory(extrusions_, modelBodies_);
        QVector<int> order;
        for (int i = 0; i < start; ++i) order.append(i);
        for (int i = insertionSourceSize_; i < extrusions_.size(); ++i) order.append(i);
        for (int i = start; i < insertionSourceSize_; ++i) order.append(i);
        QVector<int> map(extrusions_.size(), -1);
        QVector<ExtrusionObject> reordered;
        for (int old : order) { map[old] = reordered.size(); reordered.append(extrusions_.at(old)); }
        const auto mapped = [&](int old) { return old >= 0 ? map.value(old, -1) : old; };
        ForgeCad::remapFeatureBodies(reordered, mapped);
        QSet<quint64> changedBodies;
        for (int i = start; i < start + added; ++i) changedBodies.insert(reordered.at(i).modelBodyId);
        for (quint64 bodyId : changedBodies) {
            if (!bodyId) continue;
            int previous = -1;
            for (int current = 0; current < reordered.size(); ++current) {
                ExtrusionObject &feature = reordered[current];
                if (feature.modelBodyId != bodyId) continue;
                if (previous >= 0) {
                    if (feature.operation >= 0 || feature.feature == BodyFeature::Blend || feature.feature == BodyFeature::SheetTrim
                        || feature.feature == BodyFeature::SheetExtend || (feature.feature==BodyFeature::SurfaceTools&&feature.surfaceTool.mode!=1) || feature.feature == BodyFeature::Scale
                        || (feature.feature == BodyFeature::Transform && !feature.move.copy) || feature.feature == BodyFeature::Pattern
                        || feature.feature == BodyFeature::Sew || feature.feature == BodyFeature::DeleteFace
                        || feature.feature == BodyFeature::Draft || feature.feature == BodyFeature::Shell || feature.feature == BodyFeature::Thread
                        || (feature.feature == BodyFeature::Thicken && feature.offsetFaces.isEmpty())) {
                        feature.firstBody = previous;
                    } else if (ForgeCad::mergingFeature(feature) && feature.mergeOperation != 0) {
                        bool replaced = false;
                        for (int &merged : feature.mergeBodies)
                            if (merged >= 0 && reordered.at(merged).modelBodyId == bodyId) { merged = previous; replaced = true; break; }
                        if (!replaced) feature.mergeBodies.prepend(previous);
                    }
                }
                previous = current;
            }
        }
        ForgeCad::remapSketchSupports(sketches_, mapped);
        if (selection_.kind == SceneObjectKind::Extrusion) selection_.index = mapped(selection_.index);
        extrusions_ = std::move(reordered);
        historyPosition_ = start + added;
        regenerateAll();
    }
    insertionSourceSize_ = -1;
    const auto outputMap = ForgeCad::normalizePatternResultOrder(sketches_, extrusions_);
    if (!outputMap.isEmpty() && selection_.kind == SceneObjectKind::Extrusion)
        selection_.index = outputMap.value(selection_.index, -1);
    if (historyPosition_ > extrusions_.size()) historyPosition_ = extrusions_.size();
    ForgeCad::normalizeModelHistory(extrusions_, modelBodies_);
    ForgeCad::normalizeSketchHistory(sketches_, extrusions_);
    if (addedFeatures > 0 || addedSketches > 0) {
        auto steps = storyboard();
        QVector<StoryStep> old, added;
        for (const auto &step : steps) {
            const bool fresh = step.sketch ? addedSketches > 0 && step.index >= insertionSketchSourceSize_
                                           : newFeatureIds.contains(extrusions_.at(step.index).featureId);
            (fresh ? added : old).append(step);
        }
        int at = insertionPosition < 0 ? int(old.size()) : qBound(0, insertionPosition, int(old.size()));
        for (const auto &step : added) old.insert(at++, step);
        ForgeCad::placeStorySketches(sketches_, extrusions_, old);
        if (insertionPosition >= 0) {
            storyPosition_ = at;
            historyPosition_ = ForgeCad::storyFeatureLimit(extrusions_, old, at);
        }
    }
    insertionSketchSourceSize_ = -1;
    const auto currentStory = storyboard();
    if (storyPosition_ >= currentStory.size()) storyPosition_ = historyPosition_ = -1;
    else if (storyPosition_ >= 0) {
        // Anche eliminazioni e riordini possono cambiare quanti corpi
        // precedono il cursore: il prefisso geometrico deriva dalla storia.
        historyPosition_ = ForgeCad::storyFeatureLimit(extrusions_, currentStory, storyPosition_);
    }
    applyHistoryPosition();
    if (sketchMode_ && activeSketch_>=0 && activeSketch_<sketches_.size())
        for (const auto &feature : extrusions_)
            if (newFeatureIds.contains(feature.featureId) && ForgeCad::isShapeBody(feature)
                && ForgeCad::sketchesOf(feature).contains(activeSketch_))
                sketchSessionFeatures_.insert(feature.featureId);
    refreshSketchContext();
    ensureVisibleBodyDisplays();
    sceneBoundsDirty_ = true;
    raySelectionCache_.clear();
    projectedEdges_.clear();
    if (sectionRefresh_) sectionRefresh_();
    analysisDirty_ = true;
    if (constraintPanelCallback_) constraintPanelCallback_();
    selectedFace_ = {};  // la geometria (e la numerazione delle facce) puo' essere cambiata
    selectedObjects_.clear();  // gli indici possono essere cambiati
    clearFeatureHighlights();
    warmPickCaches();
    if (documentChangedCallback_) documentChangedCallback_();
    update();
}

void CadViewport::regenerateDependents(int sketchIndex) {
    if (sketchIndex < 0 || sketchIndex >= sketches_.size()) return;
    regenerateFrom(0, QVector<bool>(extrusions_.size(), false), {sketchIndex});
}

bool CadViewport::hasSurfacePierceSource(int index) const {
    for (const auto &sketch : sketches_) for (const auto &c : sketch.geometricConstraints)
        if (c.type==ConstraintType::Pierce && c.pierce && pierceSourceIndex(c.pierce->surface)==index) return true;
    return false;
}

void CadViewport::refreshSurfacePierceSketches(int sourceIndex,QSet<int> &changed,int targetSketch) {
    for (int index=0;index<sketches_.size();++index) {
        if (targetSketch>=0 && index!=targetSketch) continue;
        SketchObject work=sketches_.at(index);
        bool touched=false;
        for (auto &c : work.geometricConstraints) {
            if (c.type!=ConstraintType::Pierce || !c.pierce) continue;
            const int owner=pierceSourceIndex(c.pierce->surface);
            if (sourceIndex>=0 && owner!=sourceIndex) continue;
            if (sourceIndex==-2 && owner>=0) continue;
            touched=true;
            if (owner<0 || !extrusions_.at(owner).forgeBody) {
                auto data=std::make_shared<SketchPierceData>(*c.pierce);
                data->segments.clear(); data->curves.clear();
                data->error=QStringLiteral("La superficie del vincolo Pierce non e' disponibile.");
                c.pierce=data;
                continue;
            }
            GeometryRef ref=c.pierce->surface; ref.index=owner;
            ForgeCad::refreshSurfacePierce(work,c,*extrusions_.at(owner).forgeBody,ref,extrusions_.at(owner).name);
        }
        if (!touched) continue;
        const auto solved=ForgeCad::solveSketch(work);
        if (!solved.ok) showStatus(solved.error);
        sketches_[index]=std::move(work);
        changed.insert(index);
        analysisDirty_=true;
    }
}

void CadViewport::regenerateFrom(int from, QVector<bool> dirty, QSet<int> dirtySketches, bool all ,
                        const std::function<void(int, int, const QString &)> &progress ) {
    dirty.resize(extrusions_.size());
    for (int s = 0; s < sketches_.size(); ++s) if (sketches_.at(s).support) {
        const auto &ref = *sketches_.at(s).support;
        bool found = false;
        for (int f = 0; f < extrusions_.size(); ++f)
            if (ref.featureId ? extrusions_.at(f).featureId == ref.featureId : f == ref.index) { found = true; break; }
        if (!found) {
            sketches_[s].supportError = QStringLiteral("La lavorazione della faccia di appoggio e' stata eliminata.");
            dirtySketches.insert(s);
        }
    }
    refreshSurfacePierceSketches(-2,dirtySketches);
    const QSet<int> targets=dirtySketches;
    for (int sketch : targets) refreshSurfacePierceSketches(-1,dirtySketches,sketch);
    int completed = 0;
    const int total = all ? qMax(1, int(extrusions_.size())) : 0;
    const auto advanced = [&](int count, const QString &name) {
        completed += count;
        if (progress) progress(completed, total, name);
    };
    QVector<int> pending;  // corpi da rifare fino al prossimo piano di costruzione
    const auto flush = [&] {
        rebuildParallel(pending, progress ? advanced : std::function<void(int, const QString &)>());
        pending.clear();
    };
    for (int index = qMax(0, from); index < extrusions_.size(); ++index) {
        const ExtrusionObject &body = extrusions_.at(index);
        bool depends = all;
        for (int sketch : ForgeCad::sketchesOf(body)) depends = depends || dirtySketches.contains(sketch);
        for (int operand : ForgeCad::bodyOperands(body)) depends = depends || (operand >= 0 && operand < index && dirty.at(operand));
        if (!depends) continue;
        dirty[index] = true;
        if (!ForgeCad::isDatumBody(body)) {
            pending.append(index);
            bool attached = false;
            for (const auto &sketch : sketches_) if (sketch.support && sketch.support->featureId == body.featureId) attached = true;
            if (hasSurfacePierceSource(index) || attached) {
                flush();
                followDatum(index, dirtySketches);
                refreshSurfacePierceSketches(index,dirtySketches);
            }
            continue;
        }
        flush();
        rebuildBody(extrusions_[index], index);
        advanced(1, extrusions_.at(index).name);
        followDatum(index, dirtySketches);
        const QSet<int> moved=dirtySketches;
        for (int s : moved) if (s>=0 && s<sketches_.size() && sketches_.at(s).datumPlane==index)
            refreshSurfacePierceSketches(-1,dirtySketches,s);
    }
    flush();
    synchronizePatternResults();
    ensureVisibleBodyDisplays();
}

void CadViewport::rebuildParallel(const QVector<int> &indices,
                         const std::function<void(int, const QString &)> &advanced ) {
    if (indices.isEmpty()) return;
    DeferredWork work(this, indices.size() > 1 ? QStringLiteral("Rigenerazione di %1 corpi...").arg(indices.size())
                                               : QStringLiteral("Rigenerazione del corpo..."));
    if (indices.size() <= 1) {
        for (int index : indices) {
            auto result=extrusions_.at(index);
            const auto sketches=sketches_;
            const auto bodies=extrusions_;
            const bool eager=qEnvironmentVariableIntValue("FORGECAD_EAGER_MESH")==1;
            runWhileResponsive([&] { ForgeCad::buildBody(result,index,sketches,bodies,tessellationQuality_,eager); });
            extrusions_[index]=std::move(result);
            if (advanced) advanced(1, extrusions_.at(index).name);
        }
        return;
    }
    QHash<int, int> level;
    int levels = 0;
    for (int index : indices) {
        int l = 0;
        for (int operand : ForgeCad::bodyOperands(extrusions_.at(index)))
            if (level.contains(operand)) l = qMax(l, level.value(operand) + 1);
        level.insert(index, l);
        levels = qMax(levels, l + 1);
    }
    // Copia di lavoro: i thread scrivono qui (separata qui, prima dei
    // thread), la vista intanto disegna i corpi di prima; alla fine i corpi
    // rifatti tornano nel documento.
    QVector<ExtrusionObject> next = extrusions_;
    ExtrusionObject *bodies = next.data();
    const QVector<SketchObject> sketches = sketches_;
    const QVector<ExtrusionObject> &all = next;
    const int quality = tessellationQuality_;
    const bool eager=qEnvironmentVariableIntValue("FORGECAD_EAGER_MESH")==1;
    QVector<bool> requested(extrusions_.size(),false);
    for (int index:indices)
        requested[index]=isBodyDrawn(index) && extrusions_.at(index).feature!=BodyFeature::Pattern;
    const auto cores=ForgeCad::Kernel::threadCount(0);
    for (int l = 0; l < levels; ++l) {
        std::vector<int> batch;
        for (int index : indices)
            if (level.value(index) == l) batch.push_back(index);
        const std::size_t wave = advanced ? std::max<std::size_t>(1, ForgeCad::Kernel::threadCount(0)) : batch.size();
        for (std::size_t offset = 0; offset < batch.size(); offset += wave) {
            const std::size_t count = std::min(wave, batch.size() - offset);
            runWhileResponsive([&] {
                ForgeCad::Kernel::parallelFor(count, ForgeCad::Kernel::threadCount(0), [&](std::size_t k) {
                    const int index = batch[offset + k];
                    // Nei gruppi grandi la mesh gia' richiesta usa lo stesso
                    // worker della geometria: nessuna barriera globale B-rep/mesh.
                    // Le ripetizioni aspettano la coda che prepara anche le basi.
                    const bool display=eager || (count>=cores && requested.at(index));
                    ForgeCad::buildBody(bodies[index], index, sketches, all, quality, display);
                });
            });
            if (advanced) advanced(int(count), next.at(batch[offset + count - 1]).name);
        }
    }
    for (int index : indices) extrusions_[index] = std::move(next[index]);
}

void CadViewport::followDatum(int index, QSet<int> &changed) {
    if (index < 0 || index >= extrusions_.size()) return;
    for (int s = 0; s < sketches_.size(); ++s) {
        SketchObject &sketch = sketches_[s];
        const bool datum = sketch.datumPlane == index;
        const bool face = sketch.support && (sketch.support->featureId ? sketch.support->featureId == extrusions_.at(index).featureId
                                                                      : sketch.support->index == index);
        if ((!datum && !face) || sketch.plane != kFacePlane) continue;
        SketchFrame plane;
        QString error;
        if (datum) {
            if (!extrusions_.at(index).datumValid || !extrusions_.at(index).error.isEmpty()) error = QStringLiteral("Piano di costruzione non valido.");
            else plane = extrusions_.at(index).datumFrame;
        } else {
            ForgeCad::ResolvedRef resolved;
            if (!ForgeCad::resolveGeometryRef(*sketch.support, extrusions_.size(), sketches_, extrusions_, resolved, &error) || !resolved.hasPlane)
                error = error.isEmpty() ? QStringLiteral("La faccia di appoggio non e' piu' un piano risolvibile.") : error;
            else plane = ForgeCad::faceSketchFrame(resolved.point, resolved.direction, ForgeCad::Kernel::Vec3(0,0,1));
        }
        const SketchFrame before = sketch.frame;
        const QString beforeError = sketch.supportError;
        sketch.supportError = error;
        if (error.isEmpty()) sketch.frame = datumSketchFrame(plane, &before);
        bool same = beforeError == error;
        for (int k = 0; k < 3; ++k)
            same = same && sketch.frame.origin[k] == before.origin[k] && sketch.frame.xAxis[k] == before.xAxis[k]
                        && sketch.frame.normal[k] == before.normal[k];
        if (!same) changed.insert(s);
    }
}

SketchFrame CadViewport::datumSketchFrame(const SketchFrame &datum, const SketchFrame *previous) {
    SketchFrame frame;
    double n[3], c[3];
    for (int k = 0; k < 3; ++k) n[k] = datum.normal[k], c[k] = datum.origin[k];
    const auto dot3 = [](const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    double x[3] = {datum.xAxis[0], datum.xAxis[1], datum.xAxis[2]};
    double o[3] = {0.0, 0.0, 0.0};
    if (previous) {
        const double along = dot3(previous->xAxis, n);
        double projected[3], length = 0.0;
        for (int k = 0; k < 3; ++k) projected[k] = previous->xAxis[k] - along * n[k], length += projected[k] * projected[k];
        length = std::sqrt(length);
        if (length > 1e-3)
            for (int k = 0; k < 3; ++k) x[k] = projected[k] / length;
        for (int k = 0; k < 3; ++k) o[k] = previous->origin[k];
    }
    // Origine: il punto di partenza (origine precedente o del modello) proiettato sul piano.
    double offset[3];
    for (int k = 0; k < 3; ++k) offset[k] = o[k] - c[k];
    const double h = dot3(offset, n);
    for (int k = 0; k < 3; ++k) {
        frame.origin[k] = o[k] - h * n[k];
        frame.xAxis[k] = x[k];
        frame.normal[k] = n[k];
    }
    return frame;
}

void CadViewport::removeBodies(const QSet<int> &removed) {
    // La rigenerazione puo' ridisegnare la vista prima di documentChanged().
    // Gli indici della scelta spigoli appartengono ancora alla vecchia lista:
    // annullarla prima di eliminare o rinumerare i corpi, anche se la base
    // selezionata sopravvive con un indice diverso.
    if (!removed.isEmpty() && edgePicking_) cancelEdgePick();
    // Eliminare il passo seguente non deve trascinare lo schizzo alla fine:
    // conserva il suo intervallo e l'ordine rispetto agli altri schizzi.
    const auto previousStory = storyboard();
    for (int p = 0; p < previousStory.size(); ++p)
        if (previousStory.at(p).sketch) sketches_[previousStory.at(p).index].storyOrder = p;
    QHash<quint64, quint64> successors;
    quint64 nextFeature = 0;
    for (int f = extrusions_.size()-1; f >= 0; --f) {
        if (removed.contains(f)) successors.insert(extrusions_.at(f).featureId, nextFeature);
        else nextFeature = extrusions_.at(f).featureId;
    }
    for (auto &sketch : sketches_)
        if (successors.contains(sketch.beforeFeatureId)) sketch.beforeFeatureId = successors.value(sketch.beforeFeatureId);
    QVector<int> map(extrusions_.size(), -1);
    QVector<ExtrusionObject> kept;
    for (int index = 0; index < extrusions_.size(); ++index) {
        if (removed.contains(index)) {
            const ExtrusionObject &body = extrusions_.at(index);
            QVector<int> hidden{body.firstBody, body.secondBody};
            hidden += body.booleanTools;
            hidden += ForgeCad::hiddenOperands(body);
            for (int operand : hidden)
                if (operand >= 0 && operand < extrusions_.size() && !removed.contains(operand)) extrusions_[operand].visible = true;
            continue;
        }
        map[index] = kept.size();
        kept.append(extrusions_.at(index));
    }
    for (ExtrusionObject &body : kept) {
        if (body.firstBody >= 0) body.firstBody = map.value(body.firstBody, -1);
        if (body.secondBody >= 0) body.secondBody = map.value(body.secondBody, -1);
        for (QVector<GeometryRef> *refs : {&body.datum.refs, &body.pattern.refs, &body.surfaceTool.targets, &body.ruledFirst, &body.ruledSecond, &body.planarRefs, &body.loftStartFaces, &body.loftEndFaces, &body.loftSections, &body.loftEdgeGuides})
            for (GeometryRef &ref : *refs)
                if (ForgeCad::isBodyRef(ref)) ref.index = map.value(ref.index, -1);
        if (ForgeCad::isBodyRef(body.extentRef)) body.extentRef.index = map.value(body.extentRef.index, -1);
        if (ForgeCad::isBodyRef(body.move.axis)) body.move.axis.index = map.value(body.move.axis.index, -1);
        if (ForgeCad::isBodyRef(body.revolveAxisRef)) body.revolveAxisRef.index = map.value(body.revolveAxisRef.index, -1);
        if (ForgeCad::isBodyRef(body.draftNeutral)) body.draftNeutral.index = map.value(body.draftNeutral.index, -1);
        for (GeometryRef *ref : {&body.sweepProfileRef, &body.sweepPathRef})
            if (ForgeCad::isBodyRef(*ref)) ref->index = map.value(ref->index, -1);
        if (ForgeCad::isBodyRef(body.thickenDirection)) body.thickenDirection.index = map.value(body.thickenDirection.index, -1);
        if (ForgeCad::isBodyRef(body.curveExtraction.source)) body.curveExtraction.source.index = map.value(body.curveExtraction.source.index, -1);
        if (ForgeCad::isBodyRef(body.curveExtraction.tool)) body.curveExtraction.tool.index = map.value(body.curveExtraction.tool.index, -1);
        // Strumenti e corpi fusi eliminati: escono dall'elenco.
        for (QVector<int> *list : {&body.booleanTools, &body.mergeBodies}) {
            QVector<int> kept2;
            for (int other : *list)
                if (map.value(other, -1) >= 0) kept2.append(map.value(other, -1));
            *list = kept2;
        }
    }
    // Gli schizzi su un piano di costruzione eliminato restano dove sono (piano fisso).
    ForgeCad::remapSketchSupports(sketches_, [&](int old) { return old < 0 ? old : map.value(old, -1); });
    extrusions_ = std::move(kept);
    QSet<int> changed;
    refreshSurfacePierceSketches(-1,changed);
    if (!changed.isEmpty()) regenerateFrom(0,QVector<bool>(extrusions_.size(),false),changed);
}

void CadViewport::regenerateAfter(int changed) {
    QVector<bool> dirty(extrusions_.size(), false);
    QSet<int> sketches;
    if (changed >= 0 && changed < dirty.size()) {
        dirty[changed] = true;
        followDatum(changed, sketches);
        refreshSurfacePierceSketches(ForgeCad::isDatumBody(extrusions_.at(changed)) ? -1 : changed, sketches);
    }
    regenerateFrom(changed + 1, dirty, sketches);
}

void CadViewport::regenerateAll(const std::function<void(int, int, const QString &)> &progress ) {
    regenerateFrom(0, {}, {}, true, progress);
}

void CadViewport::rebuildBody(ExtrusionObject &body, int index) {
    DeferredWork work(this, body.name.isEmpty() ? QStringLiteral("Calcolo della geometria...")
                                                 : QStringLiteral("Calcolo di %1...").arg(body.name));
    ExtrusionObject result = body;
    const QVector<SketchObject> sketches = sketches_;
    const QVector<ExtrusionObject> bodies = extrusions_;
    const int quality = tessellationQuality_;
    runWhileResponsive([&] { ForgeCad::buildBody(result, index, sketches, bodies, quality); });
    body = std::move(result);
}

void CadViewport::runWhileResponsive(const std::function<void()> &task) {
    if (!qApp || QThread::currentThread() != qApp->thread() || !window() || !window()->isVisible()) {
        task();
        return;
    }
    ++responsiveWork_;
    QEventLoop loop;
    // La finestra di avanzamento compare solo se il calcolo dura: i
    // ricalcoli brevi (rilascio di un trascinamento) non lampeggiano.
    QTimer reveal;
    reveal.setSingleShot(true);
    QObject::connect(&reveal, &QTimer::timeout, &loop, [this] {
        for (DeferredWork *work = deferredWork_; work; work = work->outer)
            if (!work->shown && workCallback_) {
                work->shown = true;
                workCallback_(true, work->message, false);
                break;
            }
    });
    if (deferredWork_ && !deferredWork_->shown) reveal.start(kDeferredWorkDelayMs);
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            task();
        } catch (...) {
            failure = std::current_exception();
        }
        QMetaObject::invokeMethod(&loop, "quit", Qt::QueuedConnection);
    });
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    reveal.stop();
    worker.join();
    --responsiveWork_;
    if (failure) std::rethrow_exception(failure);
}

void CadViewport::tessellateBody(ExtrusionObject &body, int index ) const {
    ForgeCad::tessellateGeometry(body, tessellationQuality_, body.display);
    if (index >= 0) ForgeCad::configurePatternInstances(body, index, sketches_, extrusions_);
}

void CadViewport::restoreDocument(DocumentState state) {
    if (state.lengthUnitSet) lengthUnit_ = state.lengthUnit;
    ForgeCad::setDisplayLengthUnit(lengthUnit_, this);
    deferredSketches_.clear();for(int index:state.pendingSketchChanges)if(index>=0&&index<state.sketches.size())deferredSketches_.insert(index);
    adoptDocumentIdentity(state.version, state.baseCheckinId);
    dimensionPlacing_ = -1;
    ForgeCad::normalizePatternResultOrder(state.sketches, state.extrusions);
    ForgeCad::normalizeModelHistory(state);
    sketches_ = std::move(state.sketches);
    storyPosition_ = -1;
    insertionSketchSourceSize_ = -1;
    historyPosition_ = -1;
    insertionSourceSize_ = -1;
    extrusions_ = std::move(state.extrusions);
    modelBodies_ = std::move(state.modelBodies);
    for (SketchObject &sketch : sketches_) {
        for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
    }
    if(qEnvironmentVariableIntValue("FORGECAD_EAGER_MESH")==1) {
        QVector<int> requested;
        for (int index=0;index<extrusions_.size();++index) requested.append(index);
        ensureBodyDisplays(requested);
    }
    hasPendingPoint_ = false;
    curveControlPoints_.clear();
    sketchSelections_.clear();
    selectedPoints_.clear();
    selectedConstraints_.clear();
    constraintHover_ = -1;
    sketchHover_ = {};
    hover_ = {};
    draggingControlPoint_ = false;
    circleRadiusDrag_=-1;
    dragRecorded_ = false;
    if (edgePicking_) cancelEdgePick();
    clearBlendPreview();
    if (selection_.kind == SceneObjectKind::Sketch || selection_.kind == SceneObjectKind::Extrusion)
        selection_ = {};
    if (activeSketch_ >= sketches_.size()) {
        activeSketch_ = -1;
        if (sketchMode_) endSketchMode();
    }
    if(!sketchMode_&&!deferredSketches_.isEmpty()){
        const auto changed=deferredSketches_;deferredSketches_.clear();
        regenerateFrom(0,QVector<bool>(extrusions_.size(),false),changed);
    }
    documentChanged();
}
