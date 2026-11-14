#pragma once
#include "cad_drawing_preparation.h"
#include <QPainterPath>

namespace ForgeCad {
QPainterPath drawingLocalSectionBoundary(const DrawingView &view);
// Quote dei vertici B-rep per suggerire un piano iniziale modificabile; fallback conservativo sui corpi senza
// profondita fra vertici.
Kernel::Interval drawingLocalSectionDepthRange(const DrawingModelSnapshot &model, const DrawingView &view);
PreparedDrawingView prepareDrawingLocalSection(const DrawingModelSnapshot &source, const DrawingView &view,
                                               const DrawingViewLayout &layout, DrawingSilhouetteCache seeds,
                                               const std::function<void(int)> &phase);
} // namespace ForgeCad
