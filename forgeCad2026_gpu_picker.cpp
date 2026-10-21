#include "forgeCad2026_gpu_picker.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>

namespace ForgeCad::Gui {
void GpuPicker::clearResources() { pickingBuffer_.reset(); }

int GpuPicker::pick(const QPoint &position, const QSize &pixels, qreal ratio,
                    quint64 renderedFrameSerial_, const std::function<void()> &renderIdentifiers) {
    auto *gl = QOpenGLContext::currentContext()->functions();
    if (!pickingBuffer_ || pickingBuffer_->size() != pixels) {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setInternalTextureFormat(GL_RGBA8);
        pickingBuffer_ = std::make_unique<QOpenGLFramebufferObject>(pixels, format);
        pickingBufferSerial_ = std::numeric_limits<quint64>::max();
    }
    if (!pickingBuffer_ || !pickingBuffer_->isValid()) {
        return -2;
    }
    if (pickingBufferSerial_ != renderedFrameSerial_) {
        GLint viewport[4] = {};
        GLfloat clearColor[4] = {};
        gl->glGetIntegerv(GL_VIEWPORT, viewport);
        gl->glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
        pickingBuffer_->bind();
        gl->glViewport(0, 0, pixels.width(), pixels.height());
        gl->glDisable(GL_BLEND);
        gl->glDisable(GL_DITHER);
        gl->glDisable(GL_MULTISAMPLE);
        gl->glDisable(GL_CULL_FACE);
        gl->glEnable(GL_DEPTH_TEST);
        gl->glDepthMask(GL_TRUE);
        gl->glDepthFunc(GL_LEQUAL);
        QOpenGLContext::currentContext()->functions()->glUseProgram(0);
        gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        renderIdentifiers();
        gl->glFlush();
        pickingBufferSerial_ = renderedFrameSerial_;
        gl->glEnable(GL_DITHER);
        gl->glEnable(GL_MULTISAMPLE);
        gl->glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    } else {
        pickingBuffer_->bind();
    }
    const int x = qBound(0, qRound(position.x() * ratio), pixels.width() - 1);
    const int y = qBound(0, pixels.height() - 1 - qRound(position.y() * ratio), pixels.height() - 1);
    GLubyte color[4] = {};
    gl->glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color);
    pickingBuffer_->release();
    const unsigned id = unsigned(color[0]) | (unsigned(color[1]) << 8) | (unsigned(color[2]) << 16);
    return int(id) - 1;
}
}
