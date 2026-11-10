#include "forgeCad2026_viewport_internal.h"

void CadViewport::setSurfacePoleHiddenVisible(bool visible) {
    surfacePoleHiddenVisible_ = visible;
    update();
}

const QBitArray &CadViewport::visibleSurfacePoles() const {
    if (surfacePoleOverlay_.isEmpty()) return surfacePoleVisibility_.visible();
    QVector<BodyDisplay> occluders;
    QVector<int> solids;
    for (int i=0;i<extrusions_.size();++i)
        if (isBodyVisibleInScene(i) && ForgeCad::isShapeBody(extrusions_[i])) {
            if (extrusions_[i].solid) solids.append(occluders.size());
            occluders.append(extrusions_[i].display);
        }
    const bool previewing = (!sketchMode_ || sketchViewUnlocked_) && !preview_.key.isEmpty();
    const bool pickingFace = refPicking_ && refPickOwner_>=0 && (refPickRoles_ & ForgeCad::DatumRoleFace);
    if (previewing && (preview_.valid || previewDragVisible_) && !pickingFace) {
        if (preview_.geometry && !preview_.geometry->isSheet()) solids.append(occluders.size());
        occluders.append(previewDragVisible_ ? previewDragDisplay_ : preview_.display);
        occluders.append(preview_.retainedDisplay);
    }
    QVector4D plane;
    if (section_.enabled) plane = QVector4D(-section_.normal,QVector3D::dotProduct(section_.normal,section_.point));
    auto *self = const_cast<CadViewport *>(this);
    auto *previous = QOpenGLContext::currentContext();
    auto *previousSurface = previous ? previous->surface() : nullptr;
    const bool usable = context() && context()->isValid();
    if (usable && previous != context()) self->makeCurrent();
    if (!usable || QOpenGLContext::currentContext()!=context()) { occluders.clear(); solids.clear(); }
    const auto projection = sceneProjectionMatrix(), modelView = sceneModelViewMatrix();
    surfacePoleVisibility_.update(surfacePoleOverlay_,surfacePoleGrids_,occluders,projection,modelView,size(),devicePixelRatioF(),
        plane,section_.enabled,surfacePoleHiddenVisible_,[&] {
            self->displayCache_.setMatrices(projection,modelView);
            self->enableSectionClip();
            self->displayCache_.setLightingEnabled(false);
            for (const auto &display : occluders) self->displayCache_.displayFaces(display);
            self->disableSectionClip();
            if (section_.enabled && section_.caps && !solids.isEmpty()) {
                QVector<const BodyDisplay *> caps;
                for (int index : solids) caps.append(&occluders[index]);
                const auto n = section_.normal;
                self->sceneRenderer_.drawSectionCaps(self->displayCache_,self->overlayRenderer_,caps,sectionQuad(),
                    QVector4D(-n,QVector3D::dotProduct(n,section_.point)));
            }
        });
    if (usable && previous != context()) {
        self->doneCurrent();
        if (previous && previousSurface) previous->makeCurrent(previousSurface);
    }
    return surfacePoleVisibility_.visible();
}
