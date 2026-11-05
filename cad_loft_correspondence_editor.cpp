#include "cad_loft_correspondence_editor.h"
#include "cad_datum.h"
#include "cad_forge.h"
#include "cad_loft_diagnostics.h"
#include "forgeCad2026_viewport.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace ForgeCad {
using namespace Kernel;
class LoftCorrespondenceOverlay final : public QWidget {
  public:
    explicit LoftCorrespondenceOverlay(LoftCorrespondenceEditor *editor, QWidget *viewport)
        : QWidget(viewport), editor_(editor) {
        setObjectName(QStringLiteral("loftCorrespondenceOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(viewport->rect());
        lower();
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            if (isVisible())
                update();
        });
        timer->start(50);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto &rows = editor_->points_;
        if (rows.empty())
            return;
        for (int c = 0; c < int(rows.front().size()); ++c) {
            const QColor color = QColor::fromHsv((c * 83 + 45) % 360, 170, 255);
            p.setPen(QPen(color, 1.6, Qt::DashLine));
            QPainterPath line;
            for (int r = 0; r < int(rows.size()); ++r) {
                const auto point = editor_->screen(rows[r][c]);
                if (r == 0)
                    line.moveTo(point);
                else
                    line.lineTo(point);
            }
            p.drawPath(line);
            for (int r = 0; r < int(rows.size()); ++r) {
                const auto point = editor_->screen(rows[r][c]);
                const bool selected =
                    r == editor_->section_->currentIndex() && c == editor_->connector_->currentIndex();
                p.setPen(QPen(selected ? Qt::white : Qt::black, selected ? 2 : 1));
                p.setBrush(color);
                p.drawEllipse(point, selected ? 7 : 5, selected ? 7 : 5);
                p.setPen(Qt::white);
                p.drawText(point + QPointF(9, -8), QStringLiteral("S%1 · C%2").arg(r + 1).arg(c + 1));
            }
        }
    }

  private:
    LoftCorrespondenceEditor *editor_;
};

LoftCorrespondenceEditor::LoftCorrespondenceEditor(CadViewport *viewport, int owner, QWidget *parent)
    : QWidget(parent), viewport_(viewport), owner_(owner) {
    setObjectName(QStringLiteral("loftCorrespondences"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    enabled_ = new QCheckBox(QStringLiteral("Modifica corrispondenze fra sezioni"), this);
    enabled_->setObjectName(QStringLiteral("loftCorrespondenceEnabled"));
    layout->addWidget(enabled_);
    auto *row = new QHBoxLayout;
    section_ = new QComboBox(this);
    connector_ = new QComboBox(this);
    section_->setObjectName(QStringLiteral("loftCorrespondenceSection"));
    connector_->setObjectName(QStringLiteral("loftCorrespondenceConnector"));
    row->addWidget(section_, 1);
    row->addWidget(connector_, 1);
    layout->addLayout(row);
    auto *positions = new QHBoxLayout;
    position_ = new QDoubleSpinBox(this);
    position_->setRange(0, 100);
    position_->setDecimals(5);
    position_->setSuffix(QStringLiteral(" %"));
    position_->setObjectName(QStringLiteral("loftCorrespondencePosition"));
    position_->setKeyboardTracking(false);
    position_->setToolTip(
        QStringLiteral("Posizione lungo la sezione originale; trascina anche il punto nel viewport."));
    reverse_ = new QPushButton(QStringLiteral("Inverti verso sezione"), this);
    reverse_->setCheckable(true);
    reverse_->setObjectName(QStringLiteral("loftCorrespondenceReverse"));
    positions->addWidget(position_);
    positions->addWidget(reverse_);
    layout->addLayout(positions);
    auto *buttons = new QHBoxLayout;
    add_ = new QPushButton(QStringLiteral("Aggiungi collegamento"), this);
    remove_ = new QPushButton(QStringLiteral("Rimuovi collegamento"), this);
    auto *reset = new QPushButton(QStringLiteral("Automatico"), this);
    add_->setObjectName(QStringLiteral("loftCorrespondenceAdd"));
    remove_->setObjectName(QStringLiteral("loftCorrespondenceRemove"));
    reset->setObjectName(QStringLiteral("loftCorrespondenceReset"));
    buttons->addWidget(add_);
    buttons->addWidget(remove_);
    buttons->addWidget(reset);
    layout->addLayout(buttons);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    overlay_ = new LoftCorrespondenceOverlay(this, viewport_);
    overlay_->hide();
    viewport_->installEventFilter(this);
    connect(enabled_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        if (on)
            viewport_->cancelReferencePick();
        rebuild();
        notifyChange();
    });
    connect(section_, &QComboBox::currentIndexChanged, this, [this] {
        if (!updating_)
            updateWidgets();
    });
    connect(connector_, &QComboBox::currentIndexChanged, this, [this] {
        if (!updating_)
            updateWidgets();
    });
    connect(position_, &QDoubleSpinBox::valueChanged, this, [this](double p) {
        if (!updating_) {
            setPosition(p / 100);
            notifyChange();
        }
    });
    connect(reverse_, &QPushButton::clicked, this, [this] {
        const int r = section_->currentIndex();
        if (r < 0 || r >= keys_.size())
            return;
        auto &value = values_[keys_[r]];
        value.reverse = !value.reverse;
        // La cucitura resta sullo stesso punto; gli altri collegamenti seguono il verso scelto.
        std::reverse(value.positions.begin() + (closed_ ? 1 : 0), value.positions.end());
        rebuild();
        notifyChange();
    });
    connect(add_, &QPushButton::clicked, this, [this] {
        int column = connector_->currentIndex();
        if (column < 0 || keys_.isEmpty())
            return;
        const int count = values_[keys_.front()].positions.size();
        if (count >= 64)
            return;
        if (!closed_ && column == count - 1)
            --column;
        for (int r = 0; r < keys_.size(); ++r) {
            auto &positions = values_[keys_[r]].positions;
            const double a = positions[column], b = positions[(column + 1) % count];
            double span = b - a;
            if (closed_) {
                if (directions_[r]) {
                    if (span >= 0)
                        span -= 1;
                } else if (span <= 0)
                    span += 1;
            }
            double point = a + span * 0.5;
            if (point < 0)
                point += 1;
            if (point >= 1)
                point -= 1;
            positions.insert(column + 1, point);
        }
        rebuild();
        connector_->setCurrentIndex(column + 1);
        notifyChange();
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        const int c = connector_->currentIndex();
        if (c < 0 || keys_.isEmpty() || locked(0, c))
            return;
        if (values_[keys_.front()].positions.size() <= (closed_ ? 1 : 2))
            return;
        for (int key : keys_)
            values_[key].positions.removeAt(c);
        rebuild();
        notifyChange();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        values_.clear();
        enabled_->setChecked(false);
        rebuild();
        notifyChange();
    });
    updateWidgets();
}
LoftCorrespondenceEditor::~LoftCorrespondenceEditor() {
    stop();
    delete overlay_;
}
void LoftCorrespondenceEditor::stop() {
    if (stopped_)
        return;
    stopped_ = true;
    if (dragging_)
        viewport_->releaseMouse();
    dragging_ = false;
    viewport_->removeEventFilter(this);
    overlay_->hide();
}
void LoftCorrespondenceEditor::initialize(const QVector<int> &keys, const QVector<LoftSectionCorrespondence> &rows) {
    if (keys.size() != rows.size())
        return;
    for (int i = 0; i < keys.size(); ++i)
        values_[keys[i]] = rows[i];
    const QSignalBlocker blocker(enabled_);
    enabled_->setChecked(!rows.isEmpty());
}
void LoftCorrespondenceEditor::copyTo(ExtrusionObject &d, const QVector<int> &keys) const {
    d.loftCorrespondences.clear();
    if (!enabled_->isChecked())
        return;
    for (int key : keys) {
        if (!values_.contains(key)) {
            d.loftCorrespondences.clear();
            return;
        }
        d.loftCorrespondences.append(values_[key]);
    }
}
void LoftCorrespondenceEditor::synchronize(const ExtrusionObject &d, const QVector<int> &keys) {
    if (updating_ || stopped_)
        return;
    definition_ = d;
    keys_ = keys;
    rebuild();
}
QPointF LoftCorrespondenceEditor::screen(const Vec3 &p) const {
    return viewport_->loftControlScreen(QVector3D(p.x(), p.y(), p.z()));
}
void LoftCorrespondenceEditor::rebuild() {
    updating_ = true;
    points_.clear();
    sections_.clear();
    paths_.clear();
    directions_.clear();
    const int selected = section_->currentIndex(), column = connector_->currentIndex();
    section_->clear();
    connector_->clear();
    try {
        if (enabled_->isChecked() && keys_.size() >= 2) {
            sections_ = resolveLoftSections(definition_, owner_, viewport_->sketches(), viewport_->extrusions());
            for (const auto &s : sections_)
                paths_.push_back(loftSectionPath(s));
            closed_ = distance(paths_.front().front().curve->point(paths_.front().front().range.lo),
                               paths_.front().back().curve->point(paths_.front().back().range.hi)) < 1e-6;
            LoftOptions options;
            for (int i = 0; i < loftGuideSequence(definition_).size(); ++i) {
                const auto paths =
                    loftDiagnosticPaths({i, -1}, definition_, owner_, viewport_->sketches(), viewport_->extrusions());
                if (!paths.isEmpty())
                    options.guides.push_back(paths.front().path);
            }
            bool missing = false;
            for (int key : keys_)
                if (!values_.contains(key) || values_[key].positions.size() != values_[keys_.front()].positions.size())
                    missing = true;
            if (missing) {
                const auto defaults = loftCorrespondenceDefaults(sections_, options);
                values_.clear();
                for (int r = 0; r < keys_.size(); ++r) {
                    LoftSectionCorrespondence row;
                    for (const auto &point : defaults[r])
                        row.positions.append(loftSectionFraction(sections_[r], point));
                    values_[keys_[r]] = row;
                }
            }
            // Le direzioni sono quelle automatiche, prima della cucitura manuale.
            for (int key : keys_)
                options.reverseSections.push_back(values_[key].reverse);
            loftCorrespondenceDefaults(sections_, options, &directions_);
            for (int i = 0; i < keys_.size(); ++i) {
                const auto ref = definition_.loftSections.value(i);
                section_->addItem(QStringLiteral("S%1 — %2")
                                      .arg(i + 1)
                                      .arg(geometryRefText(ref, viewport_->sketches(), viewport_->extrusions())));
            }
            for (int i = 0; i < values_[keys_.front()].positions.size(); ++i)
                connector_->addItem(QStringLiteral("Collegamento %1").arg(i + 1));
            section_->setCurrentIndex(qBound(0, selected, section_->count() - 1));
            connector_->setCurrentIndex(qBound(0, column, connector_->count() - 1));
            updatePoints();
            status_->setText(QStringLiteral(
                "Trascina i punti S · C sui profili. Ogni colore collega punti corrispondenti. Gli estremi delle "
                "sezioni aperte restano fissi; Esc annulla il trascinamento; al rilascio si aggiorna l’anteprima."));
        } else
            status_->setText(
                QStringLiteral("Attiva i collegamenti per correggere cucitura, verso e corrispondenza dei profili."));
    } catch (const std::exception &e) {
        status_->setText(QString::fromUtf8(e.what()));
        points_.clear();
    }
    updating_ = false;
    updateWidgets();
    overlay_->setVisible(!stopped_ && !points_.empty());
    overlay_->lower();
    overlay_->update();
}
void LoftCorrespondenceEditor::updatePoints() {
    points_.clear();
    for (int i = 0; i < int(sections_.size()); ++i) {
        std::vector<Vec3> row;
        for (double value : values_[keys_[i]].positions)
            row.push_back(loftSectionPoint(sections_[i], value));
        points_.push_back(std::move(row));
    }
    overlay_->update();
}
bool LoftCorrespondenceEditor::locked(int r, int c) const {
    return !closed_ && r >= 0 && r < keys_.size() && (c == 0 || c == values_[keys_[r]].positions.size() - 1);
}
void LoftCorrespondenceEditor::updateWidgets() {
    const int r = section_->currentIndex(), c = connector_->currentIndex();
    const bool valid = !points_.empty() && r >= 0 && r < keys_.size() && c >= 0;
    const QSignalBlocker block(position_), blockReverse(reverse_);
    section_->setEnabled(valid);
    connector_->setEnabled(valid);
    position_->setEnabled(valid && !locked(r, c));
    reverse_->setEnabled(valid);
    add_->setEnabled(valid && connector_->count() < 64);
    remove_->setEnabled(valid && !locked(r, c) && connector_->count() > (closed_ ? 1 : 2));
    if (valid) {
        position_->setValue(100 * values_[keys_[r]].positions[c]);
        reverse_->setChecked(values_[keys_[r]].reverse);
    }
    overlay_->update();
}
void LoftCorrespondenceEditor::setPosition(double value) {
    const int r = section_->currentIndex(), c = connector_->currentIndex();
    if (r < 0 || c < 0 || r >= keys_.size() || locked(r, c))
        return;
    values_[keys_[r]].positions[c] = value;
    try {
        updatePoints();
        updateWidgets();
    } catch (const std::exception &e) {
        status_->setText(QString::fromUtf8(e.what()));
    }
}
void LoftCorrespondenceEditor::notifyChange() {
    if (changed && !updating_ && !stopped_)
        changed();
}
bool LoftCorrespondenceEditor::eventFilter(QObject *object, QEvent *event) {
    if (object != viewport_ || stopped_)
        return false;
    if (event->type() == QEvent::Resize) {
        overlay_->setGeometry(viewport_->rect());
        return false;
    }
    if (event->type() == QEvent::KeyPress && dragging_ && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        values_[keys_[dragSection_]].positions[dragColumn_] = dragOriginal_;
        dragging_ = false;
        viewport_->releaseMouse();
        updatePoints();
        updateWidgets();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress && enabled_->isChecked() && !viewport_->referencePicking()) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        double best = 12 * 12;
        int section = -1, column = -1;
        for (int r = 0; r < int(points_.size()); ++r)
            for (int c = 0; c < int(points_[r].size()); ++c) {
                const auto delta = screen(points_[r][c]) - mouse->position();
                const double score = QPointF::dotProduct(delta, delta);
                if (score < best) {
                    best = score;
                    section = r;
                    column = c;
                }
            }
        if (section < 0)
            return false;
        section_->setCurrentIndex(section);
        connector_->setCurrentIndex(column);
        if (locked(section, column))
            return true;
        dragging_ = true;
        dragSection_ = section;
        dragColumn_ = column;
        dragOriginal_ = values_[keys_[section]].positions[column];
        viewport_->grabMouse();
        return true;
    }
    if (event->type() == QEvent::MouseMove && dragging_) {
        const auto target = static_cast<QMouseEvent *>(event)->position();
        try {
            double best = 1e300;
            Vec3 point;
            for (const auto &piece : paths_[dragSection_]) {
                const auto score = [&](double t) {
                    auto d = screen(piece.curve->point(t)) - target;
                    return QPointF::dotProduct(d, d);
                };
                int sample = 0;
                double local = 1e300;
                constexpr int samples = 96;
                for (int i = 0; i <= samples; ++i) {
                    double v = score(piece.range.lo + piece.range.length() * i / samples);
                    if (v < local) {
                        local = v;
                        sample = i;
                    }
                }
                double lo = piece.range.lo + piece.range.length() * std::max(0, sample - 1) / samples;
                double hi = piece.range.lo + piece.range.length() * std::min(samples, sample + 1) / samples;
                for (int i = 0; i < 48; ++i) {
                    double a = lo + (hi - lo) * 0.38196601125, b = hi - (hi - lo) * 0.38196601125;
                    if (score(a) < score(b))
                        hi = b;
                    else
                        lo = a;
                }
                for (double t : {piece.range.lo, (lo + hi) / 2, piece.range.hi}) {
                    double v = score(t);
                    if (v < best) {
                        best = v;
                        point = piece.curve->point(t);
                    }
                }
            }
            // Campioni solo per il picking; il dato persistito viene dalla curva esatta.
            setPosition(loftSectionFraction(sections_[dragSection_], point));
        } catch (const std::exception &e) {
            status_->setText(QString::fromUtf8(e.what()));
        }
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && dragging_) {
        dragging_ = false;
        viewport_->releaseMouse();
        notifyChange();
        return true;
    }
    return false;
}
} // namespace ForgeCad
