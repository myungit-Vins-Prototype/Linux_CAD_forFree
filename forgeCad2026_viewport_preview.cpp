#include "cad_surface_mapping.h"
#include "forgeCad2026_viewport_internal.h"
#include "cad_loft_diagnostics.h"


bool CadViewport::preparePreviewCacheContext() {
    const bool changed = previewCacheContextValid_
        && (previewCacheSketches_.constData()!=sketches_.constData()
            || previewCacheBodies_.constData()!=extrusions_.constData()
            || previewCacheQuality_!=tessellationQuality_);
    if (!previewCacheContextValid_ || changed) {
        previewCache_.clear();
        previewCacheSketches_=sketches_;
        previewCacheBodies_=extrusions_;
        previewCacheQuality_=tessellationQuality_;
        previewCacheContextValid_=true;
    }
    return changed;
}

void CadViewport::refreshPreviewDragSource() {
    if (preview_.valid && preview_.definition.feature==BodyFeature::Extrusion
        && preview_.definition.mergeOperation==0 && preview_.definition.extent==0) {
        previewDragSource_=std::make_shared<BodyDisplay>(preview_.display);
        previewDragDefinition_=preview_.definition;
        previewDragKey_=preview_.key;
    } else previewDragSource_.reset();
}

void CadViewport::rememberPreview() {
    if (!preview_.valid || !preview_.geometry) return;
    const auto bytes=[](const BodyDisplay &display) {
        std::size_t points=std::size_t(display.vertices.size()+display.normals.size());
        for (const auto &line:display.edges) points+=std::size_t(line.size());
        for (const auto &line:display.constructionCurves) points+=std::size_t(line.size());
        return points*sizeof(QVector3D);
    };
    const auto size=[&](const Preview &preview) {
        std::size_t total = bytes(preview.display)+bytes(preview.retainedDisplay)+bytes(preview.resultDisplay);
        for (const auto &display : preview.additionalDisplays) total += bytes(display);
        return total;
    };
    constexpr std::size_t budget=64*1024*1024;
    if (size(preview_)>budget) return;
    previewCache_.erase(std::remove_if(previewCache_.begin(),previewCache_.end(),
        [&](const Preview &entry){return entry.key==preview_.key;}),previewCache_.end());
    previewCache_.push_back(preview_);
    std::size_t total=0;for(const auto &entry:previewCache_)total+=size(entry);
    while(previewCache_.size()>4 || total>budget) {
        total-=size(previewCache_.front());previewCache_.erase(previewCache_.begin());
    }
}

void CadViewport::updateDragPreview(const ExtrusionObject &definition, int index) {
    if (!featureHandleDragging_) return;
    if (preview_.valid) {
        previewDragDisplay_ = preview_.display;
        previewDragVisible_ = true;
    }
    if (!previewDragSource_ || definition.feature != BodyFeature::Extrusion
        || definition.operation >= 0 || definition.extent != 0
        || definition.mergeOperation != 0 || definition.extrudeSides == 2
        || definition.sketchIndex < 0 || definition.sketchIndex >= sketches_.size()
        || std::abs(definition.distance) < 1e-7
        || std::abs(previewDragDefinition_.distance) < 1e-7) return;
    auto same = definition;
    same.distance = previewDragDefinition_.distance;
    if (previewKey(same, index) != previewDragKey_) return;
    const auto &sketch = sketches_.at(definition.sketchIndex);
    const auto base = ForgeCad::sketchToDisplay(QPointF(), sketch);
    const auto vector = ForgeCad::extrusionVector(sketch, 1.0);
    const QVector3D axis = QVector3D(vector.x(), vector.y(), vector.z()).normalized();
    const float ratio = float(definition.distance / previewDragDefinition_.distance);
    previewDragDisplay_ = *previewDragSource_;
    const auto transform = [&](QVector3D &point) {
        point += axis * (QVector3D::dotProduct(point - base, axis) * (ratio - 1));
    };
    for (auto &point : previewDragDisplay_.vertices) transform(point);
    for (auto &line : previewDragDisplay_.edges) for (auto &point : line) transform(point);
    for (auto &point : previewDragDisplay_.faceLabelPoints) transform(point);
    for (auto &normal : previewDragDisplay_.normals)
        normal = (normal + axis * (QVector3D::dotProduct(normal, axis) * (1 / ratio - 1))).normalized();
    if (ratio < 0) {
        for (int i = 0; i + 2 < previewDragDisplay_.vertices.size(); i += 3) {
            qSwap(previewDragDisplay_.vertices[i+1], previewDragDisplay_.vertices[i+2]);
            if (i + 2 < previewDragDisplay_.normals.size())
                qSwap(previewDragDisplay_.normals[i+1], previewDragDisplay_.normals[i+2]);
        }
    }
    for (auto &line : previewDragDisplay_.constructionCurves) for (auto &point : line) transform(point);
    previewDragDisplay_.rayIndex.reset();
    previewDragVisible_ = true;
    update();
}

void CadViewport::updateOffsetDirection(const ExtrusionObject &definition, int index) {
    using namespace ForgeCad::Kernel;
    if (offsetDirection_) update();
    offsetDirection_.reset();
    const bool thicken = definition.feature == BodyFeature::Thicken;
    if (definition.operation >= 0 || (!thicken && definition.feature != BodyFeature::SurfaceOffset)
        || !std::isfinite(definition.distance) || definition.distance == 0.0) return;
    const int base = definition.firstBody;
    if (base < 0 || base >= extrusions_.size()) return;
    const auto &source = extrusions_.at(base);
    if (!source.forgeBody) return;
    const Body &shape = *source.forgeBody;
    try {
        PrecisionScope precision(calculationAccuracy_);
        FaceId face;
        Vec3 pick;
        if (!definition.offsetFaces.isEmpty()) {
            const EdgePoint &last = definition.offsetFaces.last();
            Box bounds;
            for (VertexId vertex : shape.vertices()) bounds.add(shape.vertex(vertex).point);
            face = ForgeCad::resolveFaceReference(shape, last, 1e-3 * std::max(1.0, bounds.diagonal()));
            if (!shape.contains(face)) return;
            pick = Vec3(last.x, last.y, last.z);
        } else {
            // Per tutto il corpo preferisci una faccia in vista, verso l'osservatore.
            const auto &display = source.display;
            const auto camera = camera_.modelViewMatrix();
            bool inView = false;
            float depth = -std::numeric_limits<float>::infinity();
            for (qsizetype i = 0; i < std::min(display.faceIds.size(), display.faceLabelPoints.size()); ++i) {
                const FaceId candidate{display.faceIds.at(i)};
                if (!shape.contains(candidate)) continue;
                const auto &point = display.faceLabelPoints.at(i);
                const bool visible = QRectF(rect()).contains(projectWorldPoint(point));
                const float z = camera.map(point).z();
                if (!face.valid() || (visible && !inView) || (visible == inView && z > depth)) {
                    face = candidate;
                    depth = z;
                    inView = visible;
                }
            }
            if (!face.valid()) {
                const auto faces = shape.faces();
                if (faces.empty()) return;
                face = faces.front();
            }
            if (!faceLabelPoint(base, face.index, pick)) return;
        }
        const Face &data = shape.face(face);
        if (!data.surface) return;
        auto on = projectPoint(*data.surface, pick);
        // Un vecchio riferimento può essere ancora valido per nome dopo il rifilo.
        if (!definition.offsetFaces.isEmpty()
            && classifyPointOnFace(shape, face, on.point, linearResolution()) == PointLocation::Outside) {
            if (!faceLabelPoint(base, face.index, pick)) return;
            on = projectPoint(*data.surface, pick);
        }
        Vec3 axis;
        if (thicken && definition.thickenDirection.kind >= 0) {
            ForgeCad::ResolvedRef ref;
            if (!ForgeCad::resolveGeometryRef(definition.thickenDirection, index < 0 ? int(extrusions_.size()) : index,
                                               sketches_, extrusions_, ref, nullptr) || !(ref.hasLine || ref.hasPlane)) return;
            axis = ref.direction;
        } else {
            axis = normalAt(*data.surface, on.u, on.v) * (data.sense ? 1.0 : -1.0);
        }
        if (thicken ? definition.thickenSide == 1 : definition.distance < 0.0) axis = -axis;
        const QVector3D origin(on.point.x(), on.point.y(), on.point.z()), direction(axis.x(), axis.y(), axis.z());
        if (!qIsFinite(origin.lengthSquared()) || !qIsFinite(direction.lengthSquared()) || direction.isNull()) return;
        offsetDirection_ = OffsetDirection{origin, direction.normalized(), thicken && definition.thickenSide == 2};
        update();
    } catch (const std::exception &) {
        // Un riferimento perso o singolare resta gestito dalla diagnostica della funzione.
    }
}

void CadViewport::requestPreview(const ExtrusionObject &definition, int index ) {
    updateOffsetDirection(definition, index);
    const bool contextChanged=preparePreviewCacheContext();
    if (contextChanged) { previewDragSource_.reset();previewDragVisible_=false; }
    updateDragPreview(definition, index);
    const bool firstRequest=preview_.key.isEmpty();
    const QString key = previewKey(definition, index);
    if (!contextChanged && key == preview_.key) {
        previewHandlePending_ = false;
        if (!preview_.error.isEmpty()) preparePreviewError(preview_.error);
        else clearBuildFeedback();
        if ((preview_.valid || !preview_.error.isEmpty()) && previewCallback_) previewCallback_(previewError());
        return;
    }
    clearBuildFeedback();
    // Durante il drag lascia finire il fotogramma in corso: cancellarlo
    // a ogni movimento impedirebbe di vedere qualunque risultato.
    if (!contextChanged && (featureHandleDragging_ || filletRadiusDrag_ >= 0) && previewRunning_) {
        previewHandlePending_ = true;
        previewHandleDefinition_ = definition;
        previewHandleIndex_ = index;
        return;
    }
    previewHandlePending_ = false;
    if (previewCancellation_) previewCancellation_->store(true, std::memory_order_relaxed);
    preview_.key = key;
    preview_.notice.clear();
    preview_.definition = definition;
    preview_.index = index;
    preview_.replaced.clear();
    preview_.later.clear();
    preview_.restored.clear();
    if (index >= 0 && index < extrusions_.size()) {
        preview_.later = withDependentBodies({index});
        preview_.later.remove(index);
        for (int body : preview_.later)
            for (int operand : ForgeCad::bodyOperands(extrusions_.at(body)))
                if (operand != index && !preview_.later.contains(operand) && operand >= 0 && operand < extrusions_.size()
                    && !extrusions_.at(operand).suppressed)
                    preview_.restored.insert(operand);
    }
    // Le mesh delle regioni restano disponibili al picker. Il corpo intero
    // deve sparire quando è visibile il risultato, per evitare doppie facce.
    if (definition.operation < 0 && definition.feature == BodyFeature::SheetTrim)
        preview_.replaced = ForgeCad::hiddenOperands(definition, extrusions_);
    else if (ForgeCad::isCurveBody(definition) || ForgeCad::isDatumBody(definition)) preview_.replaced.clear();  // la base dell'elica resta
    else if (definition.operation < 0 && definition.feature == BodyFeature::Blend) preview_.replaced.clear(); // la base opaca resta sotto la patch
    else if (definition.operation < 0 && definition.feature == BodyFeature::SurfaceOffset) preview_.replaced.clear();  // il corpo di partenza resta
    else if (definition.operation < 0 && (definition.feature == BodyFeature::Loft || definition.feature == BodyFeature::Ruled || definition.feature == BodyFeature::PlanarSurface
                                          || definition.feature == BodyFeature::BoundarySurface
                                          || definition.feature == BodyFeature::FillSurface))
        preview_.replaced.clear();  // curve e bordi scelti restano visibili
    else if (definition.operation < 0 && definition.feature == BodyFeature::SurfaceTools) {
        if (definition.surfaceTool.mode == 3 || definition.surfaceTool.mode == 4)
            preview_.replaced = ForgeCad::hiddenOperands(definition);
        else preview_.replaced = definition.surfaceTool.mode == 1 ? QVector<int>{} : QVector<int>{definition.firstBody};
    }
    else if (definition.mergeProbe) preview_.replaced.clear();  // i corpi fusi si sanno a calcolo finito
    else preview_.replaced = ForgeCad::bodyOperands(definition);
    // Nell'unione la base resta opaca sotto la patch aggiunta. Nella
    // sottrazione viene invece sostituita dalle facce conservate del
    // risultato, gia' forate/tagliate, mentre la superficie di taglio e'
    // la sola patch trasparente. Lo sweep conserva per ora la sostituzione
    // completa del risultato.
    if (localMergePreview(definition)) {
        preview_.replaced.clear();
        if (!definition.mergeProbe) {
            if (definition.mergeOperation == 2) preview_.replaced = definition.mergeBodies;
            else if (definition.mergeOperation == 1)
                for (int body : definition.mergeBodies) preview_.restored.insert(body);
        }
    } else if (definition.operation < 0 && definition.feature == BodyFeature::Sweep && !definition.mergeProbe) {
        preview_.replaced = definition.mergeOperation != 0 ? definition.mergeBodies : QVector<int>();
    }
    preview_.valid = false;
    preview_.sceneResult = false;
    preview_.additionalDisplays.clear();
    preview_.error.clear();
    preview_.diagnostic = {};
    preview_.geometry.reset();
    if (!previewDragVisible_) preview_.retainedDisplay = {};
    preview_.resultDisplay = {};
    ++preview_.generation;
    const auto existing = existingPreviewResults(definition, index);
    const bool ready = !existing.isEmpty() && std::all_of(existing.begin(), existing.end(), [&](const auto &body) {
        return body.display.quality == tessellationQuality_
            && (!body.display.vertices.isEmpty() || !body.display.edges.isEmpty());
    });
    if (ready) {
        preview_.geometry = existing.first().forgeBody;
        preview_.display = existing.first().display;
        preview_.notice = existing.first().notice;
        for (int i = 1; i < existing.size(); ++i) preview_.additionalDisplays.append(existing.at(i).display);
        preview_.valid = true;
        useExistingPreviewResults();
        previewDragVisible_ = false;
        refreshPreviewDragSource();
        if (previewTimer_) previewTimer_->stop();
        previewRerun_ = false;
        if (previewCallback_) previewCallback_({});
        update();
        return;
    }
    const auto cached=std::find_if(previewCache_.begin(),previewCache_.end(),
        [&](const Preview &entry){return entry.key==key;});
    if(cached!=previewCache_.end()) {
        const quint64 generation=preview_.generation;
        Preview result=*cached;
        previewCache_.erase(cached);previewCache_.push_back(result);
        preview_=std::move(result);preview_.generation=generation;
        previewDragVisible_=false;refreshPreviewDragSource();
        if(previewTimer_)previewTimer_->stop();
        if(previewCallback_)previewCallback_(preview_.error);
        update();return;
    }
    if (!previewTimer_) {
        previewTimer_ = new QTimer(this);
        previewTimer_->setSingleShot(true);
        previewTimer_->setInterval(120);
        previewTimer_->callOnTimeout([this] { startPreviewJob(); });
    }
    // Il timer scandisce il movimento: non rimandarlo a ogni evento mouse.
    if ((!featureHandleDragging_ && filletRadiusDrag_ < 0) || !previewTimer_->isActive()) previewTimer_->start(firstRequest?0:120);
    update();
}

void CadViewport::showTrimPreview(const ExtrusionObject &definition, const ForgeCad::ForgeBody &geometry,
                         const BodyDisplay &display, int index ) {
    requestPreview(definition, index);
    if (previewTimer_) previewTimer_->stop();
    ++preview_.generation;
    preview_.valid = bool(geometry);
    preview_.sceneResult = false;
    preview_.replaced = ForgeCad::hiddenOperands(definition, extrusions_);
    preview_.diagnostic = {};
    clearBuildFeedback();
    preview_.error = geometry ? QString() : QStringLiteral("Nessuna regione selezionata.");
    preview_.geometry = geometry;
    preview_.display = display;
    preview_.retainedDisplay = {};
    preview_.resultDisplay = {};
    if (geometry && previewCallback_) previewCallback_({});
    update();
}

void CadViewport::stageVolumeStructureResult(const ExtrusionObject &definition, const ForgeCad::ForgeBody &geometry,
                                             const BodyDisplay &display, int editIndex) {
    requestPreview(definition, editIndex);
    if (previewTimer_) previewTimer_->stop();
    ++preview_.generation;
    preview_.valid = true;
    preview_.sceneResult = false;
    preview_.error.clear();
    preview_.diagnostic = {};
    preview_.geometry = geometry;
    preview_.display = display;
    preview_.resultDisplay = display;
}

void CadViewport::requestBlendPreview(int base, const QVector<EdgePoint> &edges, double size, bool chamfer, int hidden , const ChamferSpec &spec ) {
    if (base < 0 || base >= extrusions_.size() || edges.isEmpty() || !(size > 0.0)) {
        clearPreview();
        return;
    }
    ExtrusionObject blend = hidden >= 0 && hidden < extrusions_.size() ? extrusions_.at(hidden) : ExtrusionObject();
    blend.operation = -1;
    blend.feature = BodyFeature::Blend;
    blend.firstBody = base;
    blend.blendEdges = edges;
    blend.blendSize = size;
    blend.blendChamfer = chamfer;
    blend.chamferSpec = spec;
    requestPreview(blend, hidden);
}

void CadViewport::clearPreview() { resetPreview(true); }

void CadViewport::closeFunctionPreview() { resetPreview(!buildDiagnosticFromHistory_); }

void CadViewport::resetPreview(bool clearDiagnostic) {
    offsetDirection_.reset();
    // Il risultato viene invalidato subito; il worker esce al prossimo
    // checkpoint e solo allora libera previewRunning_ e la barra di lavoro.
    if (previewTimer_) previewTimer_->stop();
    if (previewCancellation_) previewCancellation_->store(true, std::memory_order_relaxed);
    previewRerun_ = false;
    previewHandlePending_ = false;
    previewCache_.clear();previewCacheSketches_.clear();previewCacheBodies_.clear();previewCacheContextValid_=false;
    previewDragVisible_ = false;
    previewDragSource_.reset();
    previewDragDisplay_ = {};
    previewDragKey_.clear();
    ++preview_.generation;
    preview_.key.clear();
    preview_.index = -1;
    preview_.replaced.clear();
    preview_.later.clear();
    preview_.restored.clear();
    preview_.valid = false;
    preview_.sceneResult = false;
    preview_.additionalDisplays.clear();
    preview_.error.clear();
    preview_.diagnostic = {};
    preview_.geometry.reset();
    preview_.definition = {};
    previewHandleDefinition_ = {};
    previewDragDefinition_ = {};
    if (clearDiagnostic) clearBuildFeedback();
    preview_.display = {};
    preview_.retainedDisplay = {};
    preview_.resultDisplay = {};
    update();
}

void CadViewport::clearBlendPreview() { clearPreview(); }

QString CadViewport::previewError() const { return ForgeCad::visibleLoftError(preview_.error); }
bool CadViewport::previewPending() const { return !preview_.key.isEmpty() && !preview_.valid && preview_.error.isEmpty(); }

void CadViewport::setPreviewCallback(std::function<void(const QString &)> callback) { previewCallback_ = std::move(callback); }

const QString &CadViewport::previewNotice() const { return preview_.notice; }

bool CadViewport::takePreviewResult(ExtrusionObject &body, int previewIndex, QString *error) {
    if (body.mergeProbe || preview_.key.isEmpty() || preview_.key != previewKey(body, previewIndex)) return false;
    if (!preview_.valid && preview_.error.isEmpty()) {
        ScopedWork work(workCallback_, QStringLiteral("Completamento del calcolo dell'anteprima..."));
        const quint64 generation = preview_.generation;
        while (preview_.generation == generation && !preview_.valid && preview_.error.isEmpty())
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents | QEventLoop::ExcludeUserInputEvents, 50);
        if (preview_.generation != generation) return false;
    }
    // Durante l'attesa un operando o la qualità di tessellazione possono cambiare.
    if (preparePreviewCacheContext()) {
        clearPreview();
        return false;
    }
    if (!preview_.valid || !preview_.geometry) {
        if (preview_.error.isEmpty()) return false;
        body.buildDiagnostic = preview_.diagnostic;
        const QString message = presentBuildFailure(body, previewIndex, preview_.error, body.buildDiagnostic);
        if (error) *error = message;
        return true;
    }
    body.forgeBody = preview_.geometry;
    body.solid = !preview_.geometry->isSheet();
    body.display = preview_.resultDisplay.vertices.isEmpty() && preview_.resultDisplay.edges.isEmpty() ? preview_.display : preview_.resultDisplay;
    // Raccordi ed estrusioni fuse/sottratte mostrano solo una patch locale.
    // Alla conferma si completa la mesh, conservando lo stesso B-rep.
    if (((body.operation < 0 && body.feature == BodyFeature::Blend)
         || (localMergePreview(body) && body.mergeOperation != 0))
        && preview_.resultDisplay.vertices.isEmpty() && preview_.resultDisplay.edges.isEmpty())
        ForgeCad::tessellateGeometry(body, tessellationQuality_, body.display);
    body.display.constructionCurves.clear();
    body.error.clear();
    body.buildDiagnostic = {};
    body.notice = preview_.notice;
    if (previewIndex < 0) {
        ForgeCad::assignCreationFeatureId(body, extrusions_);
        ForgeCad::registerFeatureGenealogy(body, int(extrusions_.size()), sketches_, extrusions_);
    }
    return true;
}

void CadViewport::requestExtendPreview(int base, const QVector<EdgePoint> &edges, double distance, bool linear, int hidden ) {
    if (base < 0 || base >= extrusions_.size() || edges.isEmpty() || !(distance > 0.0)) {
        clearPreview();
        return;
    }
    ExtrusionObject body = hidden >= 0 && hidden < extrusions_.size() ? extrusions_.at(hidden) : ExtrusionObject();
    body.operation = -1;
    body.feature = BodyFeature::SheetExtend;
    body.firstBody = base;
    body.blendEdges = edges;
    body.blendSize = distance;
    body.extendLinear = linear;
    requestPreview(body, hidden);
}

bool CadViewport::localMergePreview(const ExtrusionObject &body) {
    return ForgeCad::Gui::FeaturePreviewBuilder::localMergePreview(body);
}

QString CadViewport::previewKey(const ExtrusionObject &d, int index) {
    return ForgeCad::Gui::FeaturePreviewBuilder::previewKey(d, index);
}

bool CadViewport::computePreview(const PreviewInputs &in, BodyDisplay &display, BodyDisplay &retainedDisplay,
                                 BodyDisplay &resultDisplay, ForgeCad::ForgeBody &geometry,
                                 QString &error, ForgeCad::BuildDiagnostic &diagnostic, QVector<int> *merged, QString *notice,
                                 QVector<BodyDisplay> *additionalDisplays) {
    return ForgeCad::Gui::FeaturePreviewBuilder::computePreview(in, display, retainedDisplay, resultDisplay,
                                                               geometry, error, diagnostic, merged, notice, additionalDisplays);
}

QVector<ExtrusionObject> CadViewport::existingPreviewResults(const ExtrusionObject &definition, int index) const {
    if (!unchangedFeature(definition, index) || ForgeCad::isDatumBody(definition)) return {};
    QVector<ExtrusionObject> results{extrusions_.at(index)};
    if (definition.operation < 0 && definition.feature == BodyFeature::Pattern
        && definition.patternComponent < 0 && extrusions_.at(index).patternComponent == 0) {
        for (int i = index + 1; i < extrusions_.size(); ++i) {
            const auto &body = extrusions_.at(i);
            if (body.feature != BodyFeature::Pattern || body.patternComponent <= 0 || body.firstBody != index
                || body.suppressed) continue;
            if (!body.error.isEmpty() || !ForgeCad::hasGeometry(body)) return {};
            results.append(body);
        }
    }
    return results;
}

void CadViewport::useExistingPreviewResults() {
    preview_.sceneResult = true;
    // L'intero risultato pronto sostituisce gli operandi: non occorre
    // estrarre patch o curve U/V dalle superfici gia' visualizzate.
    const auto &existing = extrusions_.at(preview_.index);
    preview_.replaced = ForgeCad::hiddenOperands(existing, extrusions_);
    // I modificatori continuano lo stesso corpo logico: la scena nasconde
    // implicitamente lo stadio precedente, anche se non e' un operando
    // consumato esplicito. Un risultato completo deve sostituire anche
    // questi stadi, altrimenti raccordo e base si sovrappongono.
    for (int operand : ForgeCad::bodyOperands(existing))
        if (operand >= 0 && operand < preview_.index && existing.modelBodyId
            && extrusions_.at(operand).modelBodyId == existing.modelBodyId
            && !preview_.replaced.contains(operand)) preview_.replaced.append(operand);
    for (int body : preview_.replaced) preview_.restored.remove(body);
}

void CadViewport::startPreviewJob() {
    if (preview_.key.isEmpty() || preview_.valid || !preview_.error.isEmpty()) return;
    if (previewRunning_) {
        previewRerun_ = true;
        return;
    }
    PreviewInputs in;
    in.definition = preview_.definition;
    in.definition.forgeBody.reset();
    in.definition.curve.reset();
    in.definition.display = {};
    in.quality = tessellationQuality_;
    in.calculationAccuracy = calculationAccuracy_;
    in.index = preview_.index >= 0 ? preview_.index : int(extrusions_.size());
    in.sketches = sketches_;
    in.bodies.resize(in.index);
    in.existingResults = existingPreviewResults(preview_.definition, preview_.index);
    in.reuse = !in.existingResults.isEmpty();
    const ExtrusionObject &d = in.definition;
    QString invalid;
    // Operandi: i body e le curve del kernel sono immutabili, si condividono.
    QVector<int> operands = ForgeCad::bodyOperands(d);
    // La mappa collegata conserva il piano dell'appiattimento come
    // dipendenza indiretta: lo snapshot del worker deve contenerlo.
    if(d.operation<0&&d.feature==BodyFeature::SurfaceTools&&d.surfaceTool.mode==4&&
        d.surfaceTool.targets.size()>1&&d.surfaceTool.targets[1].kind==11) {
        auto mapping=d.surfaceTool;GeometryRef plane;
        if(!ForgeCad::resolveFlattenedMapping(mapping,in.index,extrusions_,plane,&invalid)) {
            preview_.error = presentBuildFailure(d, preview_.index, invalid);
            if(previewCallback_)previewCallback_(preview_.error);
            update();return;
        }
        if(ForgeCad::isBodyRef(plane)&&!operands.contains(plane.index))operands.append(plane.index);
    }
    // Ripetizione della funzione: servono anche gli operandi della booleana.
    if (d.operation < 0 && d.feature == BodyFeature::Pattern && d.pattern.featureOnly && d.firstBody >= 0 && d.firstBody < extrusions_.size())
        operands += ForgeCad::bodyOperands(extrusions_.at(d.firstBody));
    for (int body : operands) {
        if (body < 0 || body >= in.index || !ForgeCad::hasGeometry(extrusions_.at(body))) {
            invalid = QStringLiteral("operandi non validi");
            break;
        }
        // La definizione intera (la ripetizione di un'estrusione che si fonde la
        // ricostruisce), senza la tassellazione.
        ExtrusionObject &copy = in.bodies[body];
        copy = extrusions_.at(body);
        copy.display = {};
    }
    if (d.operation >= 0 && d.firstBody == d.secondBody) invalid = QStringLiteral("scegli due oggetti diversi");
    if (d.operation < 0 && d.feature == BodyFeature::SheetTrim && d.firstBody == d.secondBody)
        invalid = QStringLiteral("scegli uno strumento diverso dalla superficie");
    for (int sketch : ForgeCad::sketchesOf(d))
        if (sketch < 0 || sketch >= sketches_.size()) invalid = QStringLiteral("nessuno schizzo");
    const quint64 generation = preview_.generation;
    if (!invalid.isEmpty() && !in.reuse) {
        preview_.error = presentBuildFailure(d, preview_.index, invalid);
        if (edgePicking_ && edgePickStatus_) edgePickStatus_(edgePickMessage());
        if (previewCallback_) previewCallback_(preview_.error);
        update();
        return;
    }
    const QPointer<CadViewport> guard(this);
    previewRunning_ = true;
    const auto cancellation = std::make_shared<std::atomic<bool>>(false);
    previewCancellation_ = cancellation;
    const QString workText = in.reuse ? QStringLiteral("Preparazione della visualizzazione...") : QStringLiteral("Calcolo dell'anteprima...");
    if (workCallback_) workCallback_(true, workText, true);
    QThreadPool::globalInstance()->start([this, guard, generation, in, workText, cancellation] {
        ForgeCad::Kernel::CancellationScope cancellationScope(cancellation);
        BodyDisplay display;
        BodyDisplay retainedDisplay;
        BodyDisplay resultDisplay;
        QVector<BodyDisplay> additionalDisplays;
        ForgeCad::ForgeBody geometry;
        QString error, notice;
        ForgeCad::BuildDiagnostic diagnostic;
        bool ok = false;
        QVector<int> merged;
        const bool probe = in.definition.mergeProbe;
        try {
            ForgeCad::Kernel::cancellationPoint();
            ok = computePreview(in, display, retainedDisplay, resultDisplay, geometry, error, diagnostic, &merged, &notice, &additionalDisplays);
            ForgeCad::Kernel::cancellationPoint();
        } catch (const ForgeCad::Kernel::OperationCancelled &) {
            ok = false;
        } catch (const std::exception &failure) {
            error = QString::fromUtf8(failure.what());
        } catch (...) {
            error = QStringLiteral("errore imprevisto");
        }
        if (!ok && error.isEmpty()) error = QStringLiteral("costruzione non riuscita");
        deliverResult(guard, [this, generation, ok, error, notice, probe, merged, workText, reuse = in.reuse,
                                             diagnostic = std::move(diagnostic),
                                             additionalDisplays = std::move(additionalDisplays), geometry = std::move(geometry), display = std::move(display),
                                             retainedDisplay = std::move(retainedDisplay),
                                             resultDisplay = std::move(resultDisplay)]() mutable {
            previewRunning_ = false;
            previewCancellation_.reset();
            if (workCallback_) workCallback_(false, workText, true);
            if (generation == preview_.generation && (ok || !previewHandlePending_)) {
                if (probe) {
                    const bool localExtrusion = localMergePreview(preview_.definition)
                        && preview_.definition.mergeOperation != 0;
                    if (localExtrusion) {
                        if (preview_.definition.mergeOperation == 2) {
                            preview_.replaced = ok ? merged : QVector<int>();
                            for (int body : merged) preview_.restored.remove(body);
                        } else {
                            preview_.replaced.clear();
                            if (ok)
                                for (int body : merged) preview_.restored.insert(body);
                        }
                    } else {
                        preview_.replaced = ok ? merged : QVector<int>();
                    }
                }
                preview_.valid = ok;
                preview_.diagnostic = std::move(diagnostic);
                preview_.error = ok ? QString() : error;
                if (ok) clearBuildFeedback();
                else preparePreviewError(error);
                preview_.notice = ok ? notice : QString();
                preview_.geometry = std::move(geometry);
                preview_.display = std::move(display);
                previewDragVisible_ = false;
                refreshPreviewDragSource();
                preview_.retainedDisplay = std::move(retainedDisplay);
                preview_.resultDisplay = std::move(resultDisplay);
                preview_.additionalDisplays = std::move(additionalDisplays);
                if (ok && reuse) useExistingPreviewResults();
                rememberPreview();
                if (edgePicking_ && edgePickStatus_) edgePickStatus_(edgePickMessage());
                if (previewCallback_) previewCallback_(previewError());
                update();
                previewRerun_ = false;
            } else if (!previewHandlePending_ && previewPending()) {
                previewRerun_ = false;
                startPreviewJob();
            }
            if (previewHandlePending_) {
                const auto pending = previewHandleDefinition_;
                const int index = previewHandleIndex_;
                previewHandlePending_ = false;
                requestPreview(pending, index);
            }
        });
    });
}

QString CadViewport::preparePreviewError(QString error) {
    return presentBuildFailure(preview_.definition, preview_.index, std::move(error), preview_.diagnostic);
}

void CadViewport::showPanelBuildFailure(QObject *panel, const QString &error, quint64 revisionBeforeApply) {
    if (!panel || error.isEmpty()) return;
    if (revisionBeforeApply == buildDiagnosticRevision_ || error != buildDiagnosticError_) {
        clearBuildFeedback();
        showBuildDiagnostic(error, {});
    }
    buildDiagnosticPanel_ = panel;
}

void CadViewport::clearPanelBuildFailure(QObject *panel) {
    if (panel && buildDiagnosticPanel_ == panel) clearBuildFeedback();
}

void CadViewport::clearBuildFeedback() {
    clearBuildDiagnostic();
    if (previewErrorSketch_ >= 0 || !previewLoftErrors_.isEmpty()) {
        previewErrorSketch_ = -1;
        previewLoftErrors_.clear();
        update();
    }
}

QString CadViewport::presentBuildFailure(const ExtrusionObject &definition, int index, QString error,
                                         ForgeCad::BuildDiagnostic diagnostic) {
    prepareLoftDiagnostics(error, definition, index);
    previewErrorSketch_ = -1;
    const int section = ForgeCad::loftFailure(error).section;
    if (section >= 0) {
        if (!definition.loftSections.isEmpty()) {
            if (section < definition.loftSections.size() && definition.loftSections.at(section).kind == 7)
                previewErrorSketch_ = definition.loftSections.at(section).index;
        } else if (section < definition.loftSketches.size())
            previewErrorSketch_ = definition.loftSketches.at(section);
    }
    error = ForgeCad::visibleLoftError(error);
    if (diagnostic.paths.isEmpty() && diagnostic.context.isEmpty())
        diagnostic = ForgeCad::featureDiagnosticContext(definition, sketches_, extrusions_);
    showBuildDiagnostic(error, diagnostic);
    return error;
}
