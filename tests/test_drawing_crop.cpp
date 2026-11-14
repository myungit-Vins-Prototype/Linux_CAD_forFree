#include "cad_drawing_detail.h"
#include "cad_drawing_derived.h"
#include "cad_drawing_preparation.h"
#include "fk_primitives.h"
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
void require(bool yes, const char *why) {
    if (!yes)
        throw std::runtime_error(why);
}
void ok(const QString &e) {
    if (!e.isEmpty())
        throw std::runtime_error(e.toStdString());
}
DrawingSourceBody source(quint64 id, Body value) {
    auto b = std::make_shared<Body>(std::move(value));
    return {id, b,
            std::make_shared<ElementNames>(propagateNames(*b, makeHistory({}, *b), {}, "crop" + std::to_string(id)))};
}
QImage mask(const DrawingViewVisibility &v, const DrawingView &view, const DrawingViewLayout &layout, bool hidden) {
    const auto bounds = drawingDetailBounds(view, layout);
    QImage image(600, 600, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(560 / bounds.width(), 560 / bounds.height());
    painter.translate(-bounds.left() + bounds.width() / 28, -bounds.top() + bounds.height() / 28);
    painter.setPen(QPen(Qt::black, .03));
    for (const auto *paths : {&v.curves, &v.silhouettes})
        for (const auto &p : *paths)
            painter.drawPath(hidden ? p.hidden : p.visible);
    return image;
}
void equivalent(const QImage &a, const QImage &b) {
    int differences = 0, pixels = 0;
    for (int y = 1; y < a.height() - 1; ++y)
        for (int x = 1; x < a.width() - 1; ++x) {
            const bool av = qGray(a.pixel(x, y)) < 128, bv = qGray(b.pixel(x, y)) < 128;
            if (av || bv)
                ++pixels;
            if (av == bv)
                continue;
            bool near = false;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i)
                    if (qGray((av ? b : a).pixel(x + i, y + j)) < 128)
                        near = true;
            if (!near)
                ++differences;
        }
    require(differences <= std::max(4, pixels / 500), "ritaglio anticipato cambia bordi visibili o nascosti");
}
void checks() {
    DrawingModelSnapshot model{{QUuid::createUuid(), {}},
                               {source(1, makeBox(Frame3(Vec3(-7, -7, 1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 14, 14, 3)),
                                source(2, makeCylinder(Frame3(Vec3(0, 0, -4), Vec3(0, 0, 1), Vec3(1, 0, 0)), 10, 2)),
                                source(3, makeBox(Frame3(Vec3(40, 0, 10), Vec3(0, 0, 1), Vec3(1, 0, 0)), 5, 5, 5))}};
    for (double scale : {.5, 2., 8.})
        for (bool section : {false, true}) {
            DrawingView view;
            view.kind = section ? DrawingViewKind::Section : DrawingViewKind::ProjectedEdges;
            view.sectionStyle = section ? DrawingSectionStyle::Full : DrawingSectionStyle::Removed;
            view.plane = Frame3(Vec3(0, 0, 2), Vec3(0, 0, 1), Vec3(1, 0, 0));
            view.scale = scale;
            std::vector<DrawingCurve> curves;
            ok(drawingViewCurves(model, view, curves));
            DrawingSilhouetteCache cache;
            DrawingSilhouetteResult contours;
            ok(drawingViewSilhouettes(model, view, contours, cache));
            DrawingSectionHatch hatch;
            ok(drawingSectionHatch(model, view, curves, hatch));
            DrawingViewVisibility full;
            ok(drawingViewVisibility(model, view, curves, contours, cache, full, &hatch.material));
            for (QPointF center : {QPointF(0, 0), QPointF(7, 0), QPointF(0, -7)}) {
                DrawingViewLayout layout;
                layout.relation.kind = DrawingDerivedKind::Detail;
                layout.relation.detailCenter = center;
                layout.relation.detailRadius = 2;
                const auto box = drawingDetailBounds(view, layout).adjusted(-.1, -.1, .1, .1);
                DrawingViewVisibility cropped;
                ok(drawingViewVisibility(model, view, curves, contours, cache, cropped, &hatch.material, false, &box));
                require(full.curves.size() == cropped.curves.size() &&
                            full.silhouettes.size() == cropped.silhouettes.size(),
                        "indici dei riferimenti modificati");
                auto reference = full;
                clipDrawingDetailVisibility(view, layout, reference);
                clipDrawingDetailVisibility(view, layout, cropped);
                equivalent(mask(reference, view, layout, false), mask(cropped, view, layout, false));
                equivalent(mask(reference, view, layout, true), mask(cropped, view, layout, true));
            }
        }
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        checks();
        std::cout << "PASS crop: occludenti attraversanti, cerchi, sezioni, scale e bordi nascosti\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
