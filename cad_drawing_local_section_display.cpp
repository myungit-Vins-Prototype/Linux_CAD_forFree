#include "cad_drawing_local_section_display.h"
#include "cad_drawing_break.h"
#include "cad_drawing_detail.h"
#include "cad_drawing_local_section.h"
#include "fk_classify.h"
#include "fk_transform.h"
#include <QPainter>
#include <limits>
#include <stdexcept>

namespace ForgeCad {
QPainterPath drawingLocalSectionBoundary(const DrawingView &view) {
    QPainterPath path;
    if (!view.localSection)
        return path;
    const auto &points = view.localSection->outline;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const QPointF p(points[i].x() * view.scale, -points[i].y() * view.scale);
        if (i)
            path.lineTo(p);
        else
            path.moveTo(p);
    }
    path.closeSubpath();
    return path;
}
Kernel::Interval drawingLocalSectionDepthRange(const DrawingModelSnapshot &model, const DrawingView &view) {
    double low = std::numeric_limits<double>::infinity(), high = -low;
    // Il suggerimento usa quote di vertici reali: i box conservativi dei cerchi
    // possono superare di molto il pezzo e spostare il piano iniziale nel vuoto.
    for (const auto &source : model.bodies)
        if (source.body)
            for (auto vertex : source.body->vertices()) {
                const double z = view.plane.toLocal(source.body->vertex(vertex).point).z();
                low = std::min(low, z);
                high = std::max(high, z);
            }
    if (!std::isfinite(low) || high - low <= view.tolerance) {
        low = std::numeric_limits<double>::infinity();
        high = -low;
        // Superfici chiuse senza vertici distribuiti in profondita': solo
        // un suggerimento modificabile, mai un piano topologico associato.
        for (const auto &source : model.bodies)
            if (source.body) {
                const auto local =
                    Kernel::transformBody(*source.body, Kernel::Transform3::fromFrame(view.plane).inverted());
                for (auto face : local.faces()) {
                    const auto box = Kernel::faceBox(local, face);
                    low = std::min(low, box.lo.z());
                    high = std::max(high, box.hi.z());
                }
            }
    }
    if (!std::isfinite(low) || !std::isfinite(high) || high - low <= view.tolerance)
        throw std::runtime_error("Il modello non ha una profondita' misurabile nella vista.");
    return {low, high};
}
namespace {
QRectF displayBounds(const DrawingViewVisibility &display) {
    QRectF bounds;
    for (const auto *paths : {&display.curves, &display.silhouettes})
        for (const auto &p : *paths) {
            bounds = bounds.united(p.visible.boundingRect());
            bounds = bounds.united(p.hidden.boundingRect());
        }
    return bounds.united(display.shadedBounds).united(display.sectionMaterial.boundingRect());
}
void clipDisplay(DrawingViewVisibility &display, const QPainterPath &window) {
    const auto clip = [&](QPainterPath &path) { path = clipDrawingDetailPath(path, window); };
    for (auto *paths : {&display.curves, &display.silhouettes})
        for (auto &p : *paths) {
            clip(p.visible);
            clip(p.hidden);
        }
    for (auto *p : {&display.sectionHatch, &display.visibleCylinderAxes, &display.allCylinderAxes,
                    &display.visibleCenters.axes, &display.visibleCenters.crosses, &display.allCenters.axes,
                    &display.allCenters.crosses})
        clip(*p);
    display.sectionMaterial = display.sectionMaterial.intersected(window);
}
void append(QPainterPath &target, const QPainterPath &source) { target.addPath(source); }
void composeShading(DrawingViewVisibility &result, const DrawingViewVisibility &outside,
                    const DrawingViewVisibility &inside, const QPainterPath &window) {
    const auto bounds = outside.shadedBounds.united(inside.shadedBounds);
    if (bounds.isEmpty())
        return;
    double density = 1;
    for (const auto *display : {&outside, &inside})
        if (!display->shadedImage.isNull() && display->shadedBounds.width() > 0)
            density = std::max(density, display->shadedImage.width() / display->shadedBounds.width());
    density = std::min(density, 4096 / std::max(bounds.width(), bounds.height()));
    QImage image(std::max(1, int(std::ceil(bounds.width() * density))),
                 std::max(1, int(std::ceil(bounds.height() * density))), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(density, density);
    painter.translate(-bounds.topLeft());
    QPainterPath all;
    all.addRect(bounds.adjusted(-1, -1, 1, 1));
    for (const auto *display : {&outside, &inside})
        if (!display->shadedImage.isNull()) {
            painter.save();
            painter.setClipPath(display == &outside ? all.subtracted(window) : window);
            painter.drawImage(display->shadedBounds, display->shadedImage);
            painter.restore();
        }
    painter.end();
    result.shadedImage = std::move(image);
    result.shadedBounds = bounds;
}
} // namespace
PreparedDrawingView prepareDrawingLocalSection(const DrawingModelSnapshot &source, const DrawingView &view,
                                               const DrawingViewLayout &layout, DrawingSilhouetteCache seeds,
                                               const std::function<void(int)> &phase) {
    const auto valid = validateDrawingLocalSection(view);
    if (!valid.isEmpty())
        throw std::runtime_error(valid.toStdString());
    auto ordinary = view;
    ordinary.localSection.reset();
    auto unbroken = layout;
    unbroken.interruption.reset();
    auto outside = prepareDrawingView(source, ordinary, unbroken, std::move(seeds), [](int) {});
    phase(1);
    auto inside = prepareDrawingView(source, drawingLocalSectionPlane(view), unbroken,
                                     std::move(outside.cache), [](int) {});
    phase(2);
    const auto window = drawingLocalSectionBoundary(view);
    QPainterPath all;
    all.addRect(displayBounds(*outside.display)
                    .united(displayBounds(*inside.display))
                    .united(window.boundingRect())
                    .adjusted(-10, -10, 10, 10));
    const auto boundary = clipDrawingDetailPath(window, inside.display->sectionMaterial);
    const auto rawOutside = *outside.display, rawInside = *inside.display;
    clipDisplay(*outside.display, all.subtracted(window));
    clipDisplay(*inside.display, window);
    phase(3);
    const auto mapping = mergeDrawingLocalCurves(view, outside.curves, inside.curves);
    outside.display->curves.resize(outside.curves.size());
    for (std::size_t i = 0; i < inside.curves.size(); ++i) {
        auto &p = outside.display->curves[mapping[i]];
        append(p.visible, inside.display->curves[i].visible);
        append(p.hidden, inside.display->curves[i].hidden);
    }
    outside.contours.paths.insert(outside.contours.paths.end(), inside.contours.paths.begin(),
                                  inside.contours.paths.end());
    outside.display->silhouettes.insert(outside.display->silhouettes.end(),
                                        inside.display->silhouettes.begin(),
                                        inside.display->silhouettes.end());
    append(outside.display->visibleCylinderAxes, inside.display->visibleCylinderAxes);
    append(outside.display->allCylinderAxes, inside.display->allCylinderAxes);
    append(outside.display->visibleCenters.axes, inside.display->visibleCenters.axes);
    append(outside.display->visibleCenters.crosses, inside.display->visibleCenters.crosses);
    append(outside.display->allCenters.axes, inside.display->allCenters.axes);
    append(outside.display->allCenters.crosses, inside.display->allCenters.crosses);
    outside.display->sectionHatch = std::move(inside.display->sectionHatch);
    outside.display->sectionMaterial = std::move(inside.display->sectionMaterial);
    outside.display->localBoundary = boundary;
    if (outside.display->sectionMaterial.isEmpty() && boundary.isEmpty())
        outside.display->warnings.append(
            QStringLiteral("Il piano di taglio non attraversa materiale nel contorno locale. Verificare "
                           "piano iniziale e profondita'."));
    outside.display->warnings.append(inside.display->warnings);
    if (layout.showMesh)
        composeShading(*outside.display, rawOutside, rawInside, window);
    outside.cache = std::move(inside.cache);
    phase(4);
    if (!source.bodies.empty()) {
        const auto error = drawingBreakFits(view, layout, drawingBreakBounds(*outside.display));
        if (!error.isEmpty())
            throw std::runtime_error(error.toStdString());
        applyDrawingBreak(view, layout, *outside.display);
    }
    validateDrawingViewPaths(view, layout, outside.curves, outside.contours);
    phase(5);
    return outside;
}
} // namespace ForgeCad
