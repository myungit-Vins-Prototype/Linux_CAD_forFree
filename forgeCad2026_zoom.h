#ifndef FORGECAD2026_ZOOM_H
#define FORGECAD2026_ZOOM_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QTimer>
#include <functional>
#include <utility>

class QWheelEvent;
class QNativeGestureEvent;
class QWidget;

namespace ForgeCad {
enum class ZoomInputMode { Automatic, AnimatedWheel, DirectTouchpad };
struct ZoomPreferences {
    ZoomInputMode mode = ZoomInputMode::Automatic;
    double sensitivity = 1.0; // 100%: exp(.03) per scatto angolare di 120
    double duration = 0.250; // secondi; zero significa applicazione immediata
};
ZoomPreferences loadZoomPreferences();
void saveZoomPreferences(const ZoomPreferences &preferences);
void showZoomPreferences(QWidget *parent);

// Unita' interne: incremento del logaritmo dell'ingrandimento. Non dipendono
// da camera, risoluzione del viewport o devicePixelRatio del monitor.
struct ZoomInput {
    double logDelta = 0.0;
    bool direct = false;
};
class ZoomInputNormalizer {
public:
    enum class Backend { NativePixels, X11 };
    explicit ZoomInputNormalizer(Backend backend);
    ZoomInput wheel(const QWheelEvent &event, const ZoomPreferences &preferences);
    void reset();
private:
    Backend backend_;
    QElapsedTimer lastEvent_;
    qint64 device_ = 0;
    bool latched_ = false, direct_ = false;
};

// Controller comune a viewport 3D e tavole. Gli adattatori espongono una
// scala crescente con l'ingrandimento, i limiti e l'applicazione al cursore.
class ZoomController final : public QObject {
public:
    explicit ZoomController(QObject *parent = nullptr);
    ~ZoomController() override;
    std::function<double()> scale;
    std::function<std::pair<double,double>()> limits;
    std::function<void(double,const QPointF &)> apply;
    bool wheel(const QWheelEvent &event);
    bool nativeGesture(const QNativeGestureEvent &event);
    void stop();
    void advance(double seconds);
    void setPreferences(const ZoomPreferences &preferences);
    const ZoomPreferences &preferences() const { return preferences_; }
    QTimer *timer() { return &timer_; }
    double pendingPoints() const { return pending_ / .0015; }
    double remaining() const { return remaining_; }
private:
    void cancelMotion();
    void enqueue(ZoomInput input, const QPointF &anchor);
    void applyLog(double logScale);
    ZoomPreferences preferences_;
    ZoomInputNormalizer normalizer_;
    QTimer timer_;
    QElapsedTimer clock_;
    double pending_ = 0.0, remaining_ = 0.0;
    QPointF anchor_;
};
}
#endif
