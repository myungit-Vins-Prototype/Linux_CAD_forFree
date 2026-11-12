#include "forgeCad2026_viewport_internal.h"
#include "cad_memory.h"

// Nuovo documento e anteprima file sostituiscono il proprietario dei dati.
// Undo/Redo usa restoreDocument e non passa da questo reset.
void CadViewport::resetDocumentForLoad() {
    if (sketchMode_) endSketchMode();
    if (pickCacheCancellation_) pickCacheCancellation_->store(true, std::memory_order_relaxed);
    pickCacheCancellation_.reset();
    warmedBodies_.clear();
    history_.clear();
    dragSnapshot_ = {};
    planeResizeSnapshot_ = {};
    dragRecorded_ = pointDragActive_ = draggingControlPoint_ = planeResizing_ = false;
    dimensionDrag_ = bodyDragSegment_ = circleRadiusDrag_ = -1;
    draggingCurveIndex_ = draggingControlIndex_ = -1;
    sketchReferenceCache_ = {};
    sketchReferenceCacheOwner_ = -1;
    sketchReferenceCacheValid_ = false;
    sketchOffsetOriginal_.reset();
    sketchOffsetOriginalSelection_.clear();
    sketchCombCache_.clear();
    sketchCombPreview_ = {};
    sketchCombPreviewIndex_ = -1;
    analysis_ = {};
    analysisSketch_ = -1;
    analysisDirty_ = true;
    clearPreview();
    clearFeatureHandle();
    clearFilletRadiusHandles();
    clearFeatureHighlights();
    clearShapeAnalysis();
    clearExportMeshPreview();
    clearDatumPreview();
    trimPartPickDisplays_.clear();
    trimPartPickBodies_.clear();
    trimPartPickSides_.clear();
    trimPartPickIndices_.clear();
    trimPartHover_ = -1;
    raySelectionCache_.clear();
    projectedEdges_.clear();
    // Anche un viewport nascosto puo' possedere le mesh del vecchio file:
    // non attendere un paintGL futuro per eliminarle. Risorse GL soltanto
    // con il loro contesto corrente; la geometria degli altri viewport vive.
    if (context() && isValid()) {
        makeCurrent();
        displayCache_.clear();
        doneCurrent();
    }
}

void CadViewport::releaseReplacedDocumentMemory() {
    if (documentMemoryReleasePending_) return;
    documentMemoryReleasePending_ = true;
    // Il prossimo evento vede distrutti anche i temporanei del caricamento.
    // Non conserva uno snapshot e viene annullato se il viewport scompare.
    QTimer::singleShot(0, this, [this] {
        documentMemoryReleasePending_ = false;
        ForgeCad::releaseUnusedHeapMemory();
    });
}
