#include "cad_overlay_renderer.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QImage>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    if (!context.create()) {
        std::cerr << "SKIP: contesto OpenGL non disponibile\n";
        return 77;
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!context.makeCurrent(&surface)) {
        std::cerr << "SKIP: impossibile rendere corrente il contesto OpenGL\n";
        return 77;
    }
    try {
        auto *gl = context.functions();
        ForgeCad::OverlayRenderer renderer;
        QOpenGLFramebufferObject buffer(QSize(128, 64));
        require(buffer.isValid(), "framebuffer di prova");
        QMatrix4x4 projection;
        projection.ortho(0.f, 128.f, 0.f, 64.f, -1.f, 1.f);
        renderer.setMatrices(projection, QMatrix4x4());
        const QVector<QVector3D> line{{8, 32, 0}, {120, 32, 0}};
        QMatrix4x4 translated;
        translated.translate(0, 8, 0);
        const QVector<QMatrix4x4> transforms{QMatrix4x4(), translated};
        for (bool instanced : {false, true}) {
            int solidPixels = 0;
            for (quint16 pattern : {quint16(0xFFFF), quint16(0x00FF), quint16(0)}) {
                buffer.bind();
                gl->glViewport(0, 0, 128, 64);
                gl->glDisable(GL_BLEND);
                gl->glDisable(GL_DEPTH_TEST);
                gl->glDisable(GL_SCISSOR_TEST);
                gl->glDisable(GL_MULTISAMPLE);
                gl->glClearColor(0, 0, 0, 0);
                gl->glClear(GL_COLOR_BUFFER_BIT);
                renderer.setStipple(true, pattern, 2);
                if (instanced) renderer.drawInstanced(GL_LINES, line, QVector4D(1, 1, 1, 1), transforms);
                else renderer.draw(GL_LINES, line, QVector4D(1, 1, 1, 1));
                require(gl->glGetError() == GL_NO_ERROR, "uniform uint del tratteggio senza errori OpenGL");
                const auto image = buffer.toImage();
                int pixels = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                        pixels += image.pixelColor(x, y).red() > 0;
                if (pattern == 0xFFFF) {
                    require(pixels > 50, "pattern pieno visibile");
                    solidPixels = pixels;
                } else if (pattern == 0x00FF) {
                    require(pixels > 10 && pixels < solidPixels, "pattern alternato con tratti e spazi");
                } else {
                    require(pixels == 0, "pattern vuoto realmente applicato");
                }
            }
        }
        renderer.clear();
        require(gl->glGetError() == GL_NO_ERROR, "rilascio risorse senza errori GL");
        std::cout << "PASS: uniform uint, tre pattern ordinari e istanziati, nessun errore GL\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    context.doneCurrent();
}
