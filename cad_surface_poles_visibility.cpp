#include "cad_surface_poles_visibility.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <algorithm>
#include <cmath>

namespace ForgeCad::Gui {
void SurfacePolesVisibility::clearResources() {
    buffer_.reset();
    std::vector<float>().swap(depth_);
    occluders_.clear(); points_.clear(); grids_.clear(); visible_.clear(); lines_.clear();
    depthValid_ = layoutValid_ = false;
}

bool SurfacePolesVisibility::readDepth(const QSize &pixels, const std::function<void()> &draw) {
    auto *context = QOpenGLContext::currentContext();
    if (!context || qint64(pixels.width()) * pixels.height() > 64 * 1024 * 1024) return false;
    auto *gl = context->functions();
    // Gli errori di passate precedenti non indicano un fallimento della
    // lettura di profondita corrente. Controllare soltanto le nostre chiamate.
    for (int i=0;i<16;++i) if (gl->glGetError()==GL_NO_ERROR) break;
    // Il callback viene eseguito prima di QPainter. Conservare anche i binding
    // separati read/draw del widget e lo stato pack: niente letture nel FBO MSAA.
    GLint read = 0, write = 0, viewport[4], program = 0, depthFunc = 0, pack = 0;
    GLint alignment = 0, rowLength = 0, skipRows = 0, skipPixels = 0;
    GLboolean colorMask[4], depthMask;
    GLfloat clearDepth = 1;
    gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read); gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &write);
    gl->glGetIntegerv(GL_VIEWPORT, viewport); gl->glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    gl->glGetIntegerv(GL_DEPTH_FUNC, &depthFunc); gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    gl->glGetBooleanv(GL_COLOR_WRITEMASK, colorMask); gl->glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    gl->glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack); gl->glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    gl->glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength); gl->glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    gl->glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    const GLenum capabilities[] = {GL_DEPTH_TEST, GL_BLEND, GL_SCISSOR_TEST, GL_STENCIL_TEST,
                                  GL_CULL_FACE, GL_POLYGON_OFFSET_FILL, GL_CLIP_DISTANCE0};
    GLboolean enabled[7];
    for (int i = 0; i < 7; ++i) enabled[i] = gl->glIsEnabled(capabilities[i]);
    const auto restore = [&] {
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(read)); gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(write));
        gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]); gl->glUseProgram(GLuint(program));
        gl->glDepthFunc(GLenum(depthFunc)); gl->glDepthMask(depthMask);
        gl->glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]); gl->glClearDepthf(clearDepth);
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(pack)); gl->glPixelStorei(GL_PACK_ALIGNMENT, alignment);
        gl->glPixelStorei(GL_PACK_ROW_LENGTH, rowLength); gl->glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
        gl->glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
        for (int i = 0; i < 7; ++i) { if (enabled[i]) gl->glEnable(capabilities[i]); else gl->glDisable(capabilities[i]); }
    };
    if (!buffer_ || buffer_->size() != pixels) {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        buffer_ = std::make_unique<QOpenGLFramebufferObject>(pixels, format);
    }
    if (!buffer_->isValid() || !buffer_->bind()) { restore(); return false; }
    gl->glViewport(0, 0, pixels.width(), pixels.height());
    for (GLenum cap : capabilities) gl->glDisable(cap);
    gl->glEnable(GL_DEPTH_TEST); gl->glDepthFunc(GL_LEQUAL); gl->glDepthMask(GL_TRUE);
    gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); gl->glClearDepthf(1);
    gl->glClear(GL_DEPTH_BUFFER_BIT);
    draw();
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0); gl->glPixelStorei(GL_PACK_SKIP_ROWS, 0); gl->glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    depth_.resize(std::size_t(pixels.width()) * std::size_t(pixels.height()));
    gl->glReadPixels(0, 0, pixels.width(), pixels.height(), GL_DEPTH_COMPONENT, GL_FLOAT, depth_.data());
    const bool success = gl->glGetError() == GL_NO_ERROR;
    restore();
    if (success) ++depthPasses_;
    return success;
}

bool SurfacePolesVisibility::exposed(const QVector3D &p) const {
    if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z())
        || p.x() < -1 || p.x() > 1 || p.y() < -1 || p.y() > 1 || p.z() < -1 || p.z() > 1) return false;
    if (depth_.empty()) return true; // test offscreen o contesto GL non disponibile
    const int x = qBound(0, int((p.x() + 1) * .5f * pixels_.width()), pixels_.width() - 1);
    const int y = qBound(0, int((p.y() + 1) * .5f * pixels_.height()), pixels_.height() - 1);
    // Un pixel di margine gestisce pendenze e silhouette. Il bias e' soltanto
    // l'arrotondamento del depth buffer24bit, mai una tolleranza geometrica.
    float depth = 0;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
        depth = std::max(depth, depth_[std::size_t(qBound(0,y+dy,pixels_.height()-1))*pixels_.width()
                                     + qBound(0,x+dx,pixels_.width()-1)]);
    return (p.z() + 1) * .5f <= depth + 8.f / 16777215.f;
}

void SurfacePolesVisibility::appendLine(QVector3D a, QVector3D b, const QSize &size) {
    // Clip al volume della vista prima di campionare: anche un supporto enorme
    // genera al massimo un numero di campioni proporzionale ai pixel visibili.
    float lo = 0, hi = 1; const auto d = b - a;
    for (int k = 0; k < 3; ++k) {
        if (std::abs(d[k]) < 1e-20f) { if (a[k] < -1 || a[k] > 1) return; }
        else {
            float first = (-1-a[k])/d[k], last = (1-a[k])/d[k];
            if (first > last) std::swap(first,last);
            lo = std::max(lo,first); hi = std::min(hi,last); if (lo > hi) return;
        }
    }
    b = a + hi*d; a += lo*d;
    const auto screen = [&](const QVector3D &p) { return QPointF((p.x()+1)*size.width()*.5, (1-p.y())*size.height()*.5); };
    const auto first = screen(a), last = screen(b);
    if (depth_.empty()) { lines_.append(QLineF(first,last)); return; }
    const double pixels = std::hypot((b.x()-a.x())*pixels_.width()*.5, (b.y()-a.y())*pixels_.height()*.5);
    const int samples = std::max(1, int(std::ceil(pixels))); // un test per pixel, cache fra i fotogrammi
    double start = -1;
    for (int i = 0; i <= samples; ++i) {
        const double t = double(i) / samples; const bool show = exposed(a+float(t)*(b-a));
        if (show && start < 0) start = t;
        if ((!show || i == samples) && start >= 0) {
            const double end = show ? t : double(i-1)/samples;
            if (end > start) lines_.append(QLineF(first+start*(last-first), first+end*(last-first)));
            start = -1;
        }
    }
}

void SurfacePolesVisibility::update(const QVector<QVector3D> &points, const QVector<std::array<int,3>> &grids,
        const QVector<BodyDisplay> &occluders, const QMatrix4x4 &projection, const QMatrix4x4 &modelView,
        const QSize &size, qreal ratio, const QVector4D &plane, bool clip, bool through, const std::function<void()> &draw) {
    const QSize pixels(qMax(1,qRound(size.width()*ratio)),qMax(1,qRound(size.height()*ratio)));
    size_t key = qHashBits(projection.constData(),16*sizeof(float));
    key = qHashBits(modelView.constData(),16*sizeof(float),key);
    key = qHashMulti(key,pixels.width(),pixels.height(),clip,plane.x(),plane.y(),plane.z(),plane.w(),bool(QOpenGLContext::currentContext()));
    for (const auto &display : occluders) {
        const auto &base = display.instancedBase && !display.instanceTransforms.isEmpty() ? *display.instancedBase : display;
        key = qHashMulti(key,quintptr(base.vertices.constData()),base.vertices.size(),display.instanceTransforms.size());
        for (const auto &transform : display.instanceTransforms) key = qHashBits(transform.constData(),16*sizeof(float),key);
    }
    if (!through && (!depthValid_ || quint64(key) != depthKey_)) {
        depth_.clear(); pixels_ = pixels;
        if (!occluders.isEmpty() && !readDepth(pixels,draw)) depth_.clear();
        occluders_ = occluders; depthKey_ = quint64(key); depthValid_ = true; layoutValid_ = false;
    }
    const quint64 layout = quint64(qHashMulti(key,quintptr(points.constData()),points.size(),quintptr(grids.constData()),grids.size(),through,size.width(),size.height()));
    if (layoutValid_ && layoutKey_ == layout) return;
    points_ = points; grids_ = grids; visible_.fill(false,points.size()); lines_.clear();
    const auto transform = projection * modelView;
    QVector<QVector3D> ndc; ndc.reserve(points.size());
    const auto inSection = [&](const QVector3D &p) { return !clip || QVector4D::dotProduct(plane,QVector4D(p,1)) >= 0; };
    // Il comando ha una camera ortografica: la profondita NDC e' lineare.
    for (int i = 0; i < points.size(); ++i) {
        const auto p = transform.map(points[i]); ndc.append(p);
        visible_.setBit(i,inSection(points[i]) && (through ? (std::isfinite(p.x())&&std::isfinite(p.y())&&std::isfinite(p.z())
            &&p.x()>=-1&&p.x()<=1&&p.y()>=-1&&p.y()<=1&&p.z()>=-1&&p.z()<=1) : exposed(p)));
    }
    // X-ray conserva la cache di profondita, ma i segmenti non la consultano.
    std::vector<float> saved;
    if (through) saved.swap(depth_);
    for (const auto &grid : grids) {
        const int offset=grid[0],nu=grid[1],nv=grid[2];
        if (offset<0||nu<=0||nv<=0||qint64(offset)+qint64(nu)*nv>points.size()) continue;
        const auto line = [&](int a,int b) {
            if (!inSection(points[a]) || !inSection(points[b])) return;
            appendLine(ndc[a],ndc[b],size);
        };
        for (int u=0;u<nu;++u) for (int v=0;v<nv;++v) {
            const int i=offset+u*nv+v;
            if (u+1<nu) line(i,i+nv);
            if (v+1<nv) line(i,i+1);
        }
    }
    if (through) saved.swap(depth_);
    layoutKey_ = layout; layoutValid_ = true;
}
}
