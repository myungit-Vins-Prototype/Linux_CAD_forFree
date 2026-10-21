#ifndef FORGECAD2026_VIEWPORT_INTERNAL_H
#define FORGECAD2026_VIEWPORT_INTERNAL_H
// Dipendenze riservate alle implementazioni per responsabilita' del viewport.
#include "forgeCad2026_viewport.h"
#include "cad_curve_extract.h"

#include "kernel/fk_cancel.h"
#include "cad_spatial_sketch.h"
#include "cuda_support.h"
#include "cad_cuda_tessellation.h"
#include "cad_constraints.h"
#include "cad_curve_solver.h"
#include "cad_document_io.h"
#include "cad_revision_client.h"
#include "cad_export.h"
#include "cad_expression.h"
#include "cad_datum.h"
#include "cad_features.h"
#include "cad_feature_dependencies.h"
#include "cad_feature_build.h"
#include "cad_import.h"
#include "cad_mass.h"
#include "cad_measure.h"
#include "cad_pattern.h"
#include "cad_extrude.h"
#include "cad_sketch_refs.h"
#include "cad_forge.h"
#include "cad_display_cache.h"
#include "cad_shape_analysis.h"
#include "fk_curve_ops.h"
#include "cad_overlay_renderer.h"
#include <QOpenGLContext>
#include <QQuaternion>
#include <array>
#include <atomic>
#include <set>
#include "fk_classify.h"
#include "fk_surface_algo.h"
#include "fk_curve_algo.h"
#include "fk_topology.h"
#include "fk_micro.h"
#include "cad_history.h"
#include "cad_model_history.h"
#include "cad_topology_ref.h"
#include "cad_element_names.h"
#include <QHelpEvent>
#include <QToolTip>
#include "cad_icons.h"
#include "cad_kernel.h"
#include "cad_sketch_edit.h"
#include "cad_sketch_offset.h"
#include "cad_snap.h"
#include "fk_parallel.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QThread>
#include <QPointer>
#include <QDoubleSpinBox>
#include <QRegularExpression>
#include <QFormLayout>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include "cad_menu.h"
#include <QMessageBox>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QThreadPool>
#include <QWheelEvent>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <exception>
#include <thread>
#include <cstdio>
#include <limits>
#include <functional>
#include <memory>
#include <optional>
#include <qnamespace.h>
#include <utility>


using ForgeCad::Gui::uiFont;
using ForgeCad::Gui::pointLength;
using ForgeCad::Gui::pointDistance;
using ForgeCad::Gui::kSelectionColor;
using ForgeCad::Gui::kHoverColor;
using ForgeCad::Gui::featureIconName;
using ForgeCad::Gui::wrappedNote;
using ForgeCad::Gui::runUntilApplied;

namespace {
// Notifica uniforme per i calcoli del viewport. Quelli sincroni vengono
// mostrati in una finestra dalla PdfWindow; quelli in background nella barra
// di stato. La copia della callback rende la guardia sicura anche se durante
// il lavoro cambia il gestore installato nel viewport.
class ScopedWork final {
public:
    ScopedWork(const std::function<void(bool, const QString &, bool)> &callback, const QString &message, bool background = false)
        : callback_(callback), message_(message), background_(background) {
        if (callback_) callback_(true, message_, background_);
    }
    ~ScopedWork() { if (callback_) callback_(false, message_, background_); }
    ScopedWork(const ScopedWork &) = delete;
    ScopedWork &operator=(const ScopedWork &) = delete;
private:
    std::function<void(bool, const QString &, bool)> callback_;
    QString message_;
    bool background_ = false;
};

}


template<class Callback>
void CadViewport::deliverResult(QPointer<CadViewport> guard, Callback callback) {
    // Il worker non usa il viewport. Solo il thread GUI puo' controllare
    // la sua esistenza e applicare il risultato, anche dopo una chiusura.
    QMetaObject::invokeMethod(qApp, [guard, callback = std::move(callback)]() mutable {
        if (guard) callback();
    }, Qt::QueuedConnection);
}
#endif
