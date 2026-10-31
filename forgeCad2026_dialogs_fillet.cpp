#include "cad_datum.h"
#include "cad_expression.h"
#include "cad_fillet_paths.h"
#include "cad_icons.h"
#include "forgeCad2026_dialogs.h"
#include <QHeaderView>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <cmath>

namespace {
const QColor cyan(99, 211, 255), amber(255, 192, 104), green(124, 222, 185),
    pink(218, 161, 255);
QIcon modeIcon(int mode) {
  QPixmap px(40, 40);
  px.fill(Qt::transparent);
  QPainter p(&px);
  p.setRenderHint(QPainter::Antialiasing);
  const QColor c[] = {cyan, pink, green, amber};
  p.setPen(QPen(c[mode], 2.5, Qt::SolidLine, Qt::RoundCap));
  if (mode == 0) {
    p.drawLine(7, 32, 7, 20);
    p.drawLine(20, 7, 32, 7);
    p.drawArc(QRectF(7, 7, 26, 26), 90 * 16, 90 * 16);
  }
  if (mode == 1) {
    for (int i = 0; i < 3; ++i) {
      QPainterPath a;
      a.moveTo(7, 32);
      a.cubicTo(7, 12 + i * 5, 12 + i * 5, 7, 32, 7);
      p.drawPath(a);
    }
  }
  if (mode == 2) {
    QPainterPath a;
    a.moveTo(5, 30);
    a.cubicTo(14, 2, 27, 2, 35, 30);
    p.drawPath(a);
    for (auto v : {QPointF(5, 30), QPointF(20, 9), QPointF(35, 30)}) {
      p.setBrush(c[mode]);
      p.drawEllipse(v, 2.5, 2.5);
    }
  }
  if (mode == 3) {
    p.setBrush(QColor(255, 192, 104, 40));
    p.drawPolygon(QPolygonF{{5, 28}, {5, 10}, {18, 6}, {18, 24}});
    p.setBrush(QColor(99, 211, 255, 50));
    p.drawPolygon(QPolygonF{{22, 24}, {22, 6}, {35, 10}, {35, 28}});
    p.drawArc(QRectF(12, 16, 16, 12), 0, 180 * 16);
  }
  return QIcon(px);
}

class BlendDiagram final : public QWidget {
public:
  int mode;
  int count = 5;
  double rho = .4;
  explicit BlendDiagram(int m, QWidget *parent = nullptr)
      : QWidget(parent), mode(m) {
    setMinimumHeight(211);
    setMaximumHeight(211);
  }
  QPointF project(double x, double y, double z) const {
    return {118 + 34 * x - 18 * y, 166 - 25 * z - 9 * y};
  }
  double radius(double x) const {
    return mode == 2 ? .35 + .92 * std::pow(std::sin(M_PI * x / 8), 2) : .95;
  }
  QPointF crossPoint(double x, double v) const {
    const double r = radius(x), w = mode == 1 ? rho / (1 - rho) : std::sqrt(.5);
    const double d = (1 - v) * (1 - v) + 2 * w * v * (1 - v) + v * v;
    const double y = r * v * v / d, z = (3 - r) * (1 - v) * (1 - v) / d +
                                        3 * (2 * w * v * (1 - v) + v * v) / d;
    return project(x, y, z);
  }
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    const bool light = palette().color(QPalette::Window).lightness() >= 128;
    const QColor ink = light ? QColor(64, 84, 102) : QColor(157, 176, 196);
    const QColor cyan = light ? QColor(0, 112, 155) : ::cyan;
    const QColor green = light ? QColor(12, 119, 81) : ::green;
    const QColor pink = light ? QColor(125, 57, 165) : ::pink;
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(0, 0);
    p.scale(width() / 510., 1.);
    p.setPen(Qt::NoPen);
    p.setBrush(light ? QColor(Qt::white) : QColor(21, 29, 40));
    p.drawRoundedRect(QRectF(0, 0, 510, 211), 9, 9);
    p.setPen(ink);
    p.setFont(QFont("Sans", 8));
    const QString titles[] = {
        "SEZIONE CIRCOLARE COSTANTE", "FORMA DELLA SEZIONE CONICA",
        "RAGGI AI PUNTI EQUIDISTANTI", "RACCORDO TRA DUE FACCE"};
    p.drawText(QRectF(14, 10, 480, 20), titles[mode]);
    const auto face = [&](const QPolygonF &poly, QColor color) {
      p.setPen(QPen(QColor(79, 132, 168), 1));
      p.setBrush(color);
      p.drawPolygon(poly);
    };
    QPolygonF front, top;
    front << project(0, 0, 0) << project(8, 0, 0);
    for (int i = 32; i >= 0; --i) {
      double x = i / 4.;
      front << project(x, 0, 3 - radius(x));
    }
    top << project(0, 3, 3) << project(8, 3, 3);
    for (int i = 32; i >= 0; --i) {
      double x = i / 4.;
      top << project(x, radius(x), 3);
    }
    face(front, mode == 3 ? QColor(124, 88, 48) : QColor(36, 79, 114));
    face(top, QColor(48, 108, 143));
    QPainterPath ribbon;
    ribbon.moveTo(crossPoint(0, 0));
    for (int k = 1; k <= 64; ++k)
      ribbon.lineTo(crossPoint(k / 8., 0));
    for (int k = 1; k <= 32; ++k)
      ribbon.lineTo(crossPoint(8, k / 32.));
    for (int k = 63; k >= 0; --k)
      ribbon.lineTo(crossPoint(k / 8., 1));
    for (int k = 31; k >= 0; --k)
      ribbon.lineTo(crossPoint(0, k / 32.));
    ribbon.closeSubpath();
    QLinearGradient glow(crossPoint(4, 0), crossPoint(4, 1));
    glow.setColorAt(0, mode == 3 ? QColor(49, 128, 112) : QColor(38, 126, 176));
    glow.setColorAt(.55,
                    mode == 3 ? QColor(120, 224, 184) : QColor(100, 215, 250));
    glow.setColorAt(1, mode == 3 ? QColor(54, 160, 126) : QColor(42, 157, 204));
    p.fillPath(ribbon, glow);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(mode == 3 ? green : cyan, 1.8));
    for (double v : {0., 1.}) {
      QPainterPath line;
      line.moveTo(crossPoint(0, v));
      for (int k = 1; k <= 64; ++k)
        line.lineTo(crossPoint(k / 8., v));
      p.drawPath(line);
    }
    for (double x : {0., 8.}) {
      QPainterPath line;
      line.moveTo(crossPoint(x, 0));
      for (int i = 1; i <= 32; ++i)
        line.lineTo(crossPoint(x, i / 32.));
      p.drawPath(line);
    }
    const auto note = [&](QPointF at, QString text, QColor c) {
      p.setFont(QFont("Sans", 9, QFont::DemiBold));
      p.setPen(c);
      p.drawText(QRectF(at.x(), at.y(), 130, 19), text);
    };
    if (mode == 0) {
      QPointF a = crossPoint(5, .55);
      p.setPen(QPen(amber, 1.4));
      p.drawLine(a, a + QPointF(31, -31));
      p.setBrush(amber);
      p.drawEllipse(a, 2.3, 2.3);
      note(a + QPointF(31, -47), "R costante", amber);
    }
    if (mode == 1) {
      note({338, 39}, "ρ = 0,40", pink);
      p.setFont(QFont("Sans", 8));
      const QColor cs[] = {cyan, green, pink};
      const QString names[] = {"Ellisse", "Parabola", "Iperbole"};
      for (int i = 0; i < 3; ++i) {
        p.setPen(QPen(cs[i], 2));
        QPainterPath path;
        path.moveTo(31 + i * 151, 190);
        path.cubicTo(31 + i * 151, 168 + i * 4, 48 + i * 151, 166, 66 + i * 151,
                     166);
        p.drawPath(path);
        p.drawText(QRectF(75 + i * 151, 174, 92, 20), names[i]);
      }
    } else if (mode == 2) {
      for (int i = 0; i < count; ++i) {
        double x = 8. * i / (count - 1);
        const auto at = crossPoint(x, .52);
        p.setBrush(amber);
        p.setPen(QPen(QColor(28, 36, 47), 1.4));
        p.drawEllipse(at, 4.1, 4.1);
        p.setPen(QPen(amber, 1));
        p.drawLine(at, at + QPointF(0, -22));
        p.setFont(QFont("Sans", 8));
        p.drawText(QRectF(at.x() - 14, at.y() - 41, 35, 18), Qt::AlignCenter,
                   QString("R%1").arg(i + 1));
      }
      p.setPen(light ? QColor(64, 84, 102) : QColor(166, 185, 202));
      p.setFont(QFont("Sans", 8));
      p.drawText(QRectF(15, 184, 480, 19), Qt::AlignCenter,
                 "Posizioni distribuite in parti uguali lungo il bordo");
    } else if (mode == 3) {
      note({137, 58}, "Faccia 2", cyan);
      note({242, 132}, "Faccia 1", amber);
      p.setPen(QPen(green, 1.4));
      p.drawLine(crossPoint(5, .5), {419, 97});
      note({420, 83}, "R", green);
      p.setFont(QFont("Sans", 8));
      p.setPen(light ? QColor(64, 84, 102) : QColor(166, 185, 202));
      p.drawText(QRectF(15, 184, 480, 19), Qt::AlignCenter,
                 "Due facce selezionate · superficie di raccordo tangente");
    } else {
      p.setFont(QFont("Sans", 8));
      p.setPen(light ? QColor(64, 84, 102) : QColor(166, 185, 202));
      p.drawText(QRectF(15, 184, 480, 19), Qt::AlignCenter,
                 "Seleziona i bordi e imposta un unico raggio");
    }
  }
};

} // namespace

BlendDialogResult filletDialog(
    QWidget *parent, CadViewport *viewport, const QString &title, int base,
    int hidden, const QVector<EdgePoint> &initialEdges, double size,
    const ChamferSpec &initialSpec,
    const std::function<QString(int, const QVector<EdgePoint> &, double, bool,
                                const ChamferSpec &)> &apply) {
  BlendDialogResult result;
  if (!viewport)
    return result;
  FunctionDialogPanel dialog(parent);
  dialog.setSizeIdentity(QStringLiteral("fillet"));
  dialog.setWindowTitle(title);
  ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
  dialog.setMinimumSize(400, 400);
  dialog.resize(560, 915);
  auto *root = new QVBoxLayout(&dialog);
  root->setContentsMargins(20, 0, 20, 16);
  root->setSpacing(13);
  auto *tabs = new QTabWidget(&dialog);
  tabs->setObjectName("filletTabs");
  tabs->setDocumentMode(true);
  tabs->setIconSize({21, 21});
  root->addWidget(tabs, 1);
  int body = base;
  FilletSpec values = initialSpec.fillet;
  QVector<FilletPathSpec> variablePaths = values.paths;
  QVector<ForgeCad::FilletPathGeometry> variableGeometry;
  QVector<EdgePoint> pathReferences = initialEdges;
  QString pathError, handleKey;
  QVector<FilletRadiusHandle> cachedHandles;
  int activePath = 0, pathBody = base;
  bool loadingPath = false;
  FilletPathSpec draft;
  draft.radii = values.radii;
  draft.smooth = values.smooth;
  draft.reverse = values.reverse;
  std::function<void(int)> loadPath;
  std::function<void()> synchronizePaths, updateHandles;
  const auto activeParameters = [&]() -> FilletPathSpec & {
    return activePath >= 0 && activePath < variablePaths.size()
               ? variablePaths[activePath]
               : draft;
  };
  QVector<EdgePoint> edges =
      values.mode == 3 ? QVector<EdgePoint>() : initialEdges;
  QVector<EdgePoint> faces =
      values.mode == 3 ? initialEdges : QVector<EdgePoint>();
  bool haveFace[2] = {
      faces.size() == 2 && faces[0].role == kEdgePointFaceBoundary &&
          faces[1].role == kEdgePointFaceBoundary,
      faces.size() == 2 && faces[0].role == kEdgePointFaceBoundary &&
          faces[1].role == kEdgePointFaceBoundary};
  if (faces.size() != 2)
    faces.resize(2);
  bool synchronizing = false, initialized = false,
       ordinaryPropagation = values.propagate;
  std::function<void(bool)> refresh;
  GeometryReferenceList *edgeLists[3] = {}, *faceLists[2] = {};
  BlendDiagram *diagrams[4] = {};
  QFormLayout *forms[4] = {};
  QDoubleSpinBox *radiiBoxes[3] = {};
  QComboBox *type = nullptr, *control = nullptr, *interpolation = nullptr;
  QDoubleSpinBox *rho = nullptr, *conicRadius = nullptr;
  QSpinBox *number = nullptr;
  QTableWidget *table = nullptr;
  QLabel *pointInfo = nullptr;
  QCheckBox *reverse = nullptr;
  const auto measure = [&](double value, QWidget *owner) {
    auto *spin = new ForgeCad::ExpressionSpinBox(owner);
    spin->setDecimals(6);
    spin->setRange(0.000001, 100000);
    spin->setValue(value);
    spin->setMinimumWidth(140);
    spin->setKeyboardTracking(false);
    return spin;
  };
  const auto startEdges = [&] {
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    synchronizing = true;
    if (body >= 0)
      viewport->resumeEdgePick(body, edges, false, true);
    else
      viewport->beginEdgePick(false, true);
    synchronizing = false;
    if (refresh)
      refresh(false);
  };
  const auto pickFace = [&](int which) {
    viewport->endEdgePick(true);
    const QString error =
        viewport->beginReferencePick(ForgeCad::DatumRoleFace, hidden);
    if (!error.isEmpty())
      return;
    viewport->setReferencePickCallback([&, which](bool picked,
                                                  GeometryRef ref) {
      viewport->cancelReferencePick();
      if (!picked || ref.kind != 5)
        return;
      const auto candidates = viewport->resultBodiesBefore(hidden);
      const int selected = resultBodyForPick(viewport, candidates, ref.index);
      if (selected < 0 || (hidden >= 0 && selected != base) ||
          selected != ref.index)
        return;
      if (body != selected) {
        body = selected;
        edges.clear();
        haveFace[0] = haveFace[1] = false;
      }
      faces[which] = ref.point;
      faces[which].role = kEdgePointFaceBoundary;
      haveFace[which] = true;
      if (refresh)
        refresh(false);
    });
  };
  const QString names[] = {QStringLiteral("Costante"), QStringLiteral("Conico"),
                           QStringLiteral("Variabile"),
                           QStringLiteral("Tra facce")};
  for (int mode = 0; mode < 4; ++mode) {
    auto *page = new QWidget(tabs);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 15, 0, 0);
    auto *scroll = new QScrollArea(page);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setObjectName("functionDialogScroll");
    pageLayout->addWidget(scroll);
    auto *content = new QWidget(scroll);
    content->setObjectName("functionDialogContents");
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);
    diagrams[mode] = new BlendDiagram(mode, content);
    diagrams[mode]->setObjectName(
        QStringLiteral("filletIllustration%1").arg(mode));
    column->addWidget(diagrams[mode]);
    auto *form = new QFormLayout;
    forms[mode] = form;
    form->setSpacing(10);
    form->setFormAlignment(Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    column->addLayout(form);
    column->addStretch();
    scroll->setWidget(content);
    if (mode < 3) {
      edgeLists[mode] = new GeometryReferenceList(
          dialog, viewport, QStringLiteral("filletEdges%1").arg(mode));
      edgeLists[mode]->setFixedHeight(67);
      auto *pick = new QPushButton(
          ForgeCad::commandIcon("fillet"),
          mode == 2 ? QStringLiteral("Seleziona…")
                    : QStringLiteral("Seleziona bordi nella vista…"),
          content);
      pick->setObjectName(QStringLiteral("filletPickEdges%1").arg(mode));
      if (mode == 2) {
        auto *row = new QWidget(content);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->addWidget(edgeLists[mode], 1);
        line->addWidget(pick);
        form->addRow(QStringLiteral("Bordi"), row);
      } else {
        form->addRow(QStringLiteral("Bordi"), edgeLists[mode]);
        form->addRow(QString(), pick);
      }
      QObject::connect(pick, &QPushButton::clicked, &dialog, startEdges);
    }
    if (mode == 0) {
      radiiBoxes[0] = measure(size, content);
      radiiBoxes[0]->setObjectName("filletConstantRadius");
      form->addRow(QStringLiteral("Raggio"), radiiBoxes[0]);
      form->addRow(QStringLiteral("Sezione"),
                   new QLabel(QStringLiteral("Circolare"), content));
    }
    if (mode == 1) {
      type = new QComboBox(content);
      type->setObjectName("filletConicType");
      type->addItems({QStringLiteral("Ellittica"), QStringLiteral("Parabolica"),
                      QStringLiteral("Iperbolica")});
      type->setCurrentIndex(qBound(0, values.conicType, 2));
      form->addRow(QStringLiteral("Tipo di conica"), type);
      control = new QComboBox(content);
      control->setObjectName("filletConicControl");
      control->addItems({QStringLiteral("Coefficiente ρ"),
                         QStringLiteral("Raggio al centro della sezione")});
      control->setCurrentIndex(values.conicControl);
      form->addRow(QStringLiteral("Controllo forma"), control);
      radiiBoxes[1] = measure(size, content);
      radiiBoxes[1]->setObjectName("filletReferenceRadius");
      form->addRow(QStringLiteral("Raggio di riferimento"), radiiBoxes[1]);
      rho = new QDoubleSpinBox(content);
      rho->setObjectName("filletRho");
      rho->setRange(.001, .999);
      rho->setDecimals(4);
      rho->setSingleStep(.01);
      rho->setValue(values.rho);
      form->addRow(QStringLiteral("Coefficiente ρ"), rho);
      conicRadius =
          measure(values.mode == 1 ? values.conicRadius : size, content);
      conicRadius->setObjectName("filletConicRadius");
      conicRadius->setDecimals(9);
      conicRadius->setValue(values.mode == 1 ? values.conicRadius : size);
      form->addRow(QStringLiteral("Raggio al centro"), conicRadius);
      auto *hint =
          new QLabel(QStringLiteral("Ellittica: 0 < ρ < 0,5 · Parabolica: ρ = "
                                    "0,5 · Iperbolica: 0,5 < ρ < 1"),
                     content);
      hint->setWordWrap(true);
      hint->setProperty("functionHint", true);
      form->addRow(hint);
    }
    if (mode == 2) {
      number = new QSpinBox(content);
      number->setObjectName("filletPointCount");
      number->setRange(2, 64);
      number->setMinimumWidth(75);
      number->setValue(
          values.radii.isEmpty() ? 5 : qBound(2, int(values.radii.size()), 64));
      form->addRow(QStringLiteral("Numero di punti"), number);
      pointInfo = new QLabel(content);
      pointInfo->setProperty("functionHint", true);
      pointInfo->setWordWrap(true);
      form->addRow(pointInfo);
      table = new QTableWidget(content);
      table->setObjectName("filletRadiusTable");
      table->setColumnCount(3);
      table->setHorizontalHeaderLabels({QStringLiteral("Punto"),
                                        QStringLiteral("Posizione"),
                                        QStringLiteral("Raggio")});
      table->verticalHeader()->hide();
      table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
      table->setAlternatingRowColors(true);
      table->horizontalHeader()->setFixedHeight(30);
      table->setFixedHeight(162);
      form->addRow(table);
      interpolation = new QComboBox(content);
      interpolation->setObjectName("filletInterpolation");
      interpolation->addItems(
          {QStringLiteral("Variazione morbida"), QStringLiteral("Lineare")});
      interpolation->setCurrentIndex(values.smooth ? 0 : 1);
      form->addRow(QStringLiteral("Tra i punti"), interpolation);
      reverse =
          new QCheckBox(QStringLiteral("Inverti direzione del bordo"), content);
      reverse->setChecked(values.reverse);
      form->addRow(reverse);
    }
    if (mode == 3) {
      for (int k = 0; k < 2; ++k) {
        auto *row = new QWidget(content);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        faceLists[k] = new GeometryReferenceList(
            dialog, viewport, QStringLiteral("filletFace%1").arg(k));
        faceLists[k]->setFixedHeight(38);
        line->addWidget(faceLists[k], 1);
        auto *pick = new QPushButton(QStringLiteral("Seleziona…"), row);
        pick->setObjectName(QStringLiteral("filletPickFace%1").arg(k));
        line->addWidget(pick);
        QObject::connect(pick, &QPushButton::clicked, &dialog,
                         [&, k] { pickFace(k); });
        form->addRow(QStringLiteral("Faccia %1").arg(k + 1), row);
      }
      radiiBoxes[2] = measure(size, content);
      radiiBoxes[2]->setObjectName("filletFaceRadius");
      form->addRow(QStringLiteral("Raggio"), radiiBoxes[2]);
      auto *hint = new QLabel(
          QStringLiteral(
              "Seleziona due facce adiacenti dello stesso corpo. Il raccordo "
              "rifila e unisce le facce lungo il bordo condiviso."),
          content);
      hint->setWordWrap(true);
      hint->setProperty("functionHint", true);
      form->addRow(hint);
    }
    tabs->addTab(page, modeIcon(mode), names[mode]);
  }
  auto *divider = new QFrame(&dialog);
  divider->setFrameShape(QFrame::HLine);
  divider->setStyleSheet("color:#35485c;");
  root->addWidget(divider);
  auto *propagate =
      new QCheckBox(QStringLiteral("Propaga lungo i bordi tangenti"), &dialog);
  propagate->setObjectName("filletPropagate");
  propagate->setChecked(values.propagate);
  root->addWidget(propagate);
  auto *previewRow = new QHBoxLayout;
  auto *automatic =
      new QCheckBox(QStringLiteral("Anteprima automatica"), &dialog);
  automatic->setChecked(true);
  automatic->setObjectName("filletAutomaticPreview");
  previewRow->addWidget(automatic);
  previewRow->addStretch();
  auto *calculate = new QPushButton(QStringLiteral("Calcola"), &dialog);
  previewRow->addWidget(calculate);
  root->addLayout(previewRow);
  auto *status = new QLabel(&dialog);
  status->setWordWrap(true);
  status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  status->setMaximumHeight(42);
  status->setProperty("functionHint", true);
  status->setObjectName("functionPreviewMessage");
  root->addWidget(status);
  auto *validationForm = new QFormLayout;
  root->addLayout(validationForm);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Conferma"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annulla"));
  validationForm->addRow(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  const auto currentSpec = [&] {
    ChamferSpec spec = initialSpec;
    auto &f = spec.fillet;
    f.mode = tabs->currentIndex();
    f.conicType = type->currentIndex();
    f.conicControl = control->currentIndex();
    f.rho = rho->value();
    f.conicRadius = conicRadius->value();
    f.paths = f.mode == 2 ? variablePaths : QVector<FilletPathSpec>();
    const auto &legacy =
        variablePaths.isEmpty() ? draft : variablePaths.first();
    f.radii = legacy.radii;
    f.smooth = legacy.smooth;
    f.reverse = legacy.reverse;
    f.propagate = f.mode == 2 ? false : propagate->isChecked();
    return spec;
  };
  const auto currentEdges = [&] {
    return tabs->currentIndex() == 3
               ? (haveFace[0] && haveFace[1] ? faces : QVector<EdgePoint>())
               : edges;
  };
  const auto currentRadius = [&] {
    const int mode = tabs->currentIndex();
    if (mode != 2)
      return radiiBoxes[mode == 0 ? 0 : mode == 1 ? 1 : 2]->value();
    double radius = 0.000001;
    for (const auto &path : variablePaths)
      for (double value : path.radii)
        radius = qMax(radius, value);
    if (variablePaths.isEmpty())
      for (double value : draft.radii)
        radius = qMax(radius, value);
    return radius;
  };
  const auto selectionError = [&]() -> QString {
    const int mode = tabs->currentIndex();
    if (body < 0)
      return QStringLiteral("Seleziona la geometria nella vista.");
    if (mode == 3 && (!haveFace[0] || !haveFace[1]))
      return QStringLiteral("Seleziona le due facce nella vista.");
    if (mode != 3 && edges.isEmpty())
      return QStringLiteral("Seleziona almeno un bordo nella vista.");
    if (mode == 2 && !pathError.isEmpty())
      return pathError;
    if (mode == 2 && variablePaths.isEmpty())
      return QStringLiteral(
          "Seleziona uno o più bordi per definire i percorsi.");
    return {};
  };
  refresh = [&](bool force) {
    if (!initialized)
      return;
    status->setToolTip(QString());
    for (auto *list : edgeLists)
      list->setEdges(body, edges);
    for (int k = 0; k < 2; ++k)
      faceLists[k]->setEdges(body, haveFace[k] ? QVector<EdgePoint>{faces[k]}
                                               : QVector<EdgePoint>());
    for (int k = 0; k < 2; ++k)
      if (haveFace[k] && faceLists[k]->count())
        faceLists[k]->item(0)->setText(
            faceLists[k]->item(0)->text() +
            QStringLiteral(" — F%1").arg(faces[k].subshape));
    if (edgeLists[2])
      for (int i = 0; i < edgeLists[2]->count(); ++i) {
        auto *item = edgeLists[2]->item(i);
        const QString full = item->text();
        item->setToolTip(full);
        for (int k = 0; k < variablePaths.size(); ++k)
          if (variablePaths[k].edges.contains(i))
            item->setText(QStringLiteral("P%1 · %2").arg(k + 1).arg(full));
      }
    if (updateHandles)
      updateHandles();
    const QString error = selectionError();
    if (!error.isEmpty()) {
      viewport->clearBlendPreview();
      status->setText(error);
      return;
    }
    if (!force && !automatic->isChecked()) {
      viewport->clearBlendPreview();
      status->setText(
          QStringLiteral("Premi Calcola per aggiornare l’anteprima."));
      return;
    }
    status->setText(QStringLiteral("Anteprima in calcolo…"));
    viewport->requestBlendPreview(body, currentEdges(), currentRadius(), false,
                                  hidden, currentSpec());
  };
  const auto changeRadius = [&](int pathIndex, int station, double radius) {
    auto &path = pathIndex >= 0 && pathIndex < variablePaths.size()
                     ? variablePaths[pathIndex]
                     : draft;
    if (station < 0 || station >= path.radii.size())
      return;
    path.radii[station] = radius;
    if (pathIndex >= 0 && pathIndex < variableGeometry.size() &&
        variableGeometry[pathIndex].path.closed &&
        (station == 0 || station + 1 == path.radii.size())) {
      path.radii[station == 0 ? path.radii.size() - 1 : 0] = radius;
    }
    if (pathIndex == activePath)
      for (int i = 0; i < path.radii.size(); ++i)
        if (auto *spin =
                qobject_cast<QDoubleSpinBox *>(table->cellWidget(i, 2))) {
          const QSignalBlocker blocker(spin);
          spin->setValue(path.radii[i]);
        }
    refresh(false);
  };
  const auto populate = [&](int count) {
    auto &path = activeParameters();
    const QVector<double> previous = path.radii;
    path.radii.resize(count);
    for (int i = 0; i < count; ++i) {
      if (previous.isEmpty())
        path.radii[i] = size;
      else {
        const double at = double(i) * (previous.size() - 1) / (count - 1);
        const int lo = qMin(int(at), int(previous.size()) - 1),
                  hi = qMin(lo + 1, int(previous.size()) - 1);
        path.radii[i] =
            previous[lo] + (at - lo) * (previous[hi] - previous[lo]);
      }
    }
    if (activePath < variableGeometry.size() &&
        variableGeometry[activePath].path.closed)
      path.radii.back() = path.radii.front();
    table->setRowCount(count);
    const int pathIndex = activePath;
    for (int i = 0; i < count; ++i) {
      auto *index = new QTableWidgetItem(QString::number(i + 1));
      auto *position = new QTableWidgetItem(
          QString::number(100. * i / (count - 1), 'f', count > 5 ? 1 : 0) +
          QStringLiteral(" %"));
      index->setFlags(Qt::ItemIsEnabled);
      position->setFlags(Qt::ItemIsEnabled);
      index->setTextAlignment(Qt::AlignCenter);
      position->setTextAlignment(Qt::AlignCenter);
      table->setItem(i, 0, index);
      table->setItem(i, 1, position);
      auto *value = measure(path.radii[i], table);
      value->setObjectName(QStringLiteral("filletStationRadius%1").arg(i));
      value->setMinimumWidth(0);
      value->setFrame(false);
      value->setStyleSheet(
          "border:none;background:transparent;min-height:24px;");
      table->setCellWidget(i, 2, value);
      table->setRowHeight(i, 26);
      QObject::connect(value, qOverload<double>(&QDoubleSpinBox::valueChanged),
                       &dialog, [&, pathIndex, i](double radius) {
                         changeRadius(pathIndex, i, radius);
                       });
    }
    const QString info =
        QStringLiteral("%1 punti · %2 tratti di uguale lunghezza")
            .arg(count)
            .arg(count - 1);
    pointInfo->setText(
        variablePaths.isEmpty()
            ? info
            : QStringLiteral("Percorso %1 di %2 · %3%4\n%5")
                  .arg(activePath + 1)
                  .arg(variablePaths.size())
                  .arg(path.edges.size() == 1
                           ? QStringLiteral("1 bordo")
                           : QStringLiteral("%1 bordi").arg(path.edges.size()))
                  .arg(activePath < variableGeometry.size() &&
                               variableGeometry[activePath].path.closed
                           ? QStringLiteral(" · chiuso")
                           : QString())
                  .arg(info));
    pointInfo->setToolTip(QStringLiteral(
        "Seleziona un bordo nell’elenco per modificare la tabella del suo "
        "percorso. Trascina i punti nella vista per cambiare i raggi."));
    diagrams[2]->count = count;
    diagrams[2]->update();
    handleKey.clear();
    // Cambiare la tabella visibile non cambia la geometria e conserva
    // anche un'anteprima calcolata manualmente.
    if (loadingPath) {
      if (updateHandles)
        updateHandles();
    } else
      refresh(false);
  };
  loadPath = [&](int index) {
    if (loadingPath)
      return;
    loadingPath = true;
    activePath = qBound(0, index, qMax(0, int(variablePaths.size()) - 1));
    auto &path = activeParameters();
    if (path.radii.size() < 2)
      path.radii = QVector<double>(5, size);
    const QSignalBlocker n(number), s(interpolation), r(reverse);
    number->setValue(path.radii.size());
    interpolation->setCurrentIndex(path.smooth ? 0 : 1);
    reverse->setChecked(path.reverse);
    populate(path.radii.size());
    loadingPath = false;
  };
  synchronizePaths = [&] {
    const auto previous = variablePaths;
    const auto previousRefs = pathReferences;
    const bool sameBody = pathBody == body;
    QString error;
    const auto geometry =
        body >= 0 && body < viewport->extrusions().size()
            ? ForgeCad::filletPathGeometry(
                  viewport->extrusions()[body].forgeBody, edges, &error)
            : QVector<ForgeCad::FilletPathGeometry>();
    variableGeometry = geometry;
    variablePaths.clear();
    pathError = error;
    const auto sameRef = [](const EdgePoint &a, const EdgePoint &b) {
      return !a.name.isEmpty() && !b.name.isEmpty()
                 ? a.name == b.name
                 : a.subshape == b.subshape && a.geometry == b.geometry &&
                       a.role == b.role && a.x == b.x && a.y == b.y &&
                       a.z == b.z;
    };
    int activeEdge = edgeLists[2]->currentRow();
    if ((activeEdge < 0 || activeEdge >= previousRefs.size()) &&
        activePath >= 0 && activePath < previous.size() &&
        !previous[activePath].edges.isEmpty())
      activeEdge = previous[activePath].edges.first();
    int nextPath = -1, nextShared = 0;
    bool anchorFound = false;
    for (const auto &group : geometry) {
      FilletPathSpec parameters = draft;
      int best = -1, score = 0;
      if (sameBody)
        for (int k = 0; k < previous.size(); ++k) {
          int shared = 0;
          for (int i : group.edges)
            for (int j : previous[k].edges)
              if (j >= 0 && j < previousRefs.size() &&
                  sameRef(edges[i], previousRefs[j]))
                ++shared;
          if (k == activePath && !anchorFound && shared > nextShared) {
            nextShared = shared;
            nextPath = variablePaths.size();
          }
          if (shared > score) {
            score = shared;
            best = k;
          }
        }
      if (sameBody && activeEdge >= 0 && activeEdge < previousRefs.size())
        for (int i : group.edges)
          if (sameRef(edges[i], previousRefs[activeEdge])) {
            nextPath = variablePaths.size();
            anchorFound = true;
            break;
          }
      if (best >= 0)
        parameters = previous[best];
      else if (previous.isEmpty() && !values.radii.isEmpty()) {
        parameters.radii = values.radii;
        parameters.smooth = values.smooth;
        parameters.reverse = values.reverse;
      }
      parameters.edges = group.edges;
      if (parameters.radii.size() < 2)
        parameters.radii = QVector<double>(number->value(), size);
      if (group.path.closed)
        parameters.radii.back() = parameters.radii.front();
      variablePaths.append(parameters);
    }
    pathReferences = edges;
    pathBody = body;
    handleKey.clear();
    // Conserva il percorso del bordo evidenziato anche quando l'elenco
    // cambia ordine o una catena viene unita/divisa.
    loadPath(nextPath >= 0
                 ? nextPath
                 : qMin(activePath, int(variablePaths.size()) - 1));
  };
  updateHandles = [&] {
    if (tabs->currentIndex() != 2 || activePath < 0 ||
        activePath >= variableGeometry.size() || body < 0 ||
        body >= viewport->extrusions().size()) {
      viewport->clearFilletRadiusHandles();
      return;
    }
    const auto source = viewport->extrusions()[body].forgeBody;
    const auto &path = variablePaths[activePath];
    QString key = QStringLiteral("%1/%2/%3/%4")
                      .arg(quintptr(source.get()))
                      .arg(activePath)
                      .arg(path.radii.size())
                      .arg(path.reverse);
    for (int edge : path.edges)
      key += QStringLiteral("/%1").arg(edge);
    if (key != handleKey) {
      try {
        cachedHandles = ForgeCad::filletStations(
            source, variableGeometry[activePath], path, activePath);
      } catch (const std::exception &) {
        cachedHandles.clear();
      }
      handleKey = key;
    }
    auto handles = cachedHandles;
    for (auto &handle : handles)
      handle.radius = path.radii[handle.station];
    viewport->setFilletRadiusHandles(
        handles, [&](int pathIndex, int station, double radius, bool finished) {
          changeRadius(pathIndex, station, radius);
          if (finished && automatic->isChecked())
            refresh(true);
        });
  };
  populate(number->value());
  const auto conicControls = [&](bool familyChanged) {
    const int family = type->currentIndex();
    const QSignalBlocker block(rho);
    const double lo = family == 0   ? .001
                      : family == 1 ? .5
                                    : .5001,
                 hi = family == 0   ? .4999
                      : family == 1 ? .5
                                    : .999;
    const double value = rho->value();
    rho->setRange(lo, hi);
    if (value < lo || value > hi)
      rho->setValue(family == 0 ? .4 : family == 1 ? .5 : .65);
    rho->setEnabled(family != 1);
    forms[1]->setRowVisible(rho, control->currentIndex() == 0);
    forms[1]->setRowVisible(conicRadius, control->currentIndex() == 1);
    if (familyChanged && control->currentIndex() == 1) {
      const QSignalBlocker blockRadius(conicRadius);
      conicRadius->setValue(radiiBoxes[1]->value() * std::sqrt(.5) *
                            (1 - rho->value()) / rho->value());
    }
    diagrams[1]->rho = rho->value();
    diagrams[1]->update();
    refresh(false);
  };
  conicControls(false);
  QObject::connect(type, &QComboBox::currentIndexChanged, &dialog,
                   [&] { conicControls(true); });
  QObject::connect(control, &QComboBox::currentIndexChanged, &dialog,
                   [&] { conicControls(true); });
  QObject::connect(rho, qOverload<double>(&QDoubleSpinBox::valueChanged),
                   &dialog, [&] {
                     diagrams[1]->rho = rho->value();
                     diagrams[1]->update();
                     refresh(false);
                   });
  QObject::connect(conicRadius,
                   qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                   [&] { refresh(false); });
  for (auto *spin : radiiBoxes)
    QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                     &dialog, [&] { refresh(false); });
  QObject::connect(number, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                   populate);
  QObject::connect(
      interpolation, &QComboBox::currentIndexChanged, &dialog, [&] {
        activeParameters().smooth = interpolation->currentIndex() == 0;
        refresh(false);
      });
  QObject::connect(reverse, &QCheckBox::toggled, &dialog, [&] {
    activeParameters().reverse = reverse->isChecked();
    handleKey.clear();
    refresh(false);
  });
  QObject::connect(edgeLists[2], &QListWidget::currentRowChanged, &dialog,
                   [&](int row) {
                     if (loadingPath || tabs->currentIndex() != 2 || row < 0)
                       return;
                     for (int i = 0; i < variablePaths.size(); ++i)
                       if (variablePaths[i].edges.contains(row)) {
                         if (i != activePath)
                           loadPath(i);
                         break;
                       }
                   });
  QObject::connect(propagate, &QCheckBox::toggled, &dialog,
                   [&] { refresh(false); });
  QObject::connect(automatic, &QCheckBox::toggled, &dialog,
                   [&] { refresh(false); });
  QObject::connect(calculate, &QPushButton::clicked, &dialog,
                   [&] { refresh(true); });
  QObject::connect(tabs, &QTabWidget::currentChanged, &dialog, [&](int mode) {
    if (mode == 2) {
      ordinaryPropagation = propagate->isChecked();
      const QSignalBlocker b(propagate);
      propagate->setChecked(false);
      if (synchronizePaths)
        synchronizePaths();
    } else if (!propagate->isEnabled()) {
      const QSignalBlocker b(propagate);
      propagate->setChecked(ordinaryPropagation);
    }
    propagate->setEnabled(mode != 2);
    propagate->setToolTip(
        mode == 2 ? QStringLiteral("I percorsi variabili usano solo i bordi "
                                   "selezionati, unendo quelli tangenti.")
                  : QString());
    if (mode == 3) {
      viewport->endEdgePick(true);
      refresh(false);
    } else
      startEdges();
  });
  const QPointer<QLabel> guardedStatus(status);
  viewport->setPreviewCallback([guardedStatus](const QString &error) {
    if (guardedStatus) {
      guardedStatus->setToolTip(error);
      guardedStatus->setText(
          error.isEmpty() ? QStringLiteral("Anteprima pronta nella vista.")
                          : error);
    }
  });
  viewport->setEdgePickChangedCallback(
      [&](int selected, QVector<EdgePoint> changed) {
        if (synchronizing || tabs->currentIndex() == 3)
          return;
        if (hidden >= 0 && selected != base) {
          startEdges();
          return;
        }
        body = selected;
        edges = std::move(changed);
        if (tabs->currentIndex() == 2)
          synchronizePaths();
        refresh(false);
      });
  viewport->setEdgePickPanelKeyCallbacks(
      [buttons] { buttons->button(QDialogButtonBox::Ok)->click(); },
      [&dialog] { dialog.reject(); });
  QObject::connect(&dialog, &QDialog::finished, viewport, [viewport] {
    viewport->clearBlendPreview();
    viewport->clearFilletRadiusHandles();
  });
  synchronizePaths();
  initialized = true;
  tabs->setCurrentIndex(qBound(0, values.mode, 3));
  if (tabs->currentIndex() != 3)
    startEdges();
  else
    refresh(false);
  const auto commit = [&]() -> QString {
    const QString error = selectionError();
    if (!error.isEmpty())
      return error;
    if (viewport->previewPending())
      return QStringLiteral("L’anteprima è ancora in calcolo. Puoi attendere "
                            "oppure annullare subito il comando.");
    return apply(body, currentEdges(), currentRadius(), false, currentSpec());
  };
  if (auto *window = qobject_cast<QMainWindow *>(parent))
    result.applied = runUntilAppliedModeless(window, viewport, dialog,
                                             validationForm, buttons, commit);
  else
    result.applied =
        ForgeCad::Gui::runUntilApplied(dialog, validationForm, buttons, commit);
  result.size = currentRadius();
  result.chamfer = false;
  result.spec = currentSpec();
  viewport->clearFilletRadiusHandles();
  viewport->setPreviewCallback({});
  viewport->setReferencePickCallback(nullptr);
  viewport->cancelReferencePick();
  viewport->setEdgePickChangedCallback({});
  viewport->setEdgePickPanelKeyCallbacks({}, {});
  viewport->endEdgePick(true);
  viewport->clearBlendPreview();
  return result;
}
