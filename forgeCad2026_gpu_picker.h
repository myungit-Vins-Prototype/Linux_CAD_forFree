#ifndef FORGECAD2026_GPU_PICKER_H
#define FORGECAD2026_GPU_PICKER_H

#include <QOpenGLFramebufferObject>
#include <QPoint>
#include <QSize>
#include <functional>
#include <limits>
#include <memory>

namespace ForgeCad::Gui {
// Buffer degli identificativi GPU, riutilizzato fino al prossimo frame.
// Richiede il contesto corrente. Non conserva widget, documento o callback.
class GpuPicker {
public:
    // Il callback disegna gli identificativi solo se il buffer va aggiornato.
    // Restituisce ID RGB - 1, -1 per lo sfondo, -2 se il buffer non e' disponibile.
    int pick(const QPoint &position, const QSize &pixels, qreal devicePixelRatio,
             quint64 frameSerial, const std::function<void()> &renderIdentifiers);
    // Rilasciare le risorse con il contesto corrente, prima di distruggerlo.
    void clearResources();
private:
    std::unique_ptr<QOpenGLFramebufferObject> pickingBuffer_;
    quint64 pickingBufferSerial_ = std::numeric_limits<quint64>::max();
};
}

#endif
