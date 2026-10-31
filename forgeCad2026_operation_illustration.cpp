#include "forgeCad2026_operation_illustration.h"

#include <QLinearGradient>
#include <QApplication>
#include <QMap>
#include <QPainterPath>
#include <QRadialGradient>
#include <cmath>

namespace ForgeCad::Gui {
namespace {
const QColor cyan(99, 211, 255), amber(255, 192, 104), green(124, 222, 185),
    pink(218, 161, 255);
bool lightCanvas(const QPainter &p) {
    const auto *widget = dynamic_cast<const QWidget *>(p.device());
    return (widget ? widget->palette() : QApplication::palette()).color(QPalette::Window).lightness() >= 128;
}
QColor canvasColor(const QPainter &p, QColor color) {
    if (!lightCanvas(p)) return color;
    if (color == cyan) return QColor(0, 112, 155);
    if (color == amber) return QColor(146, 87, 0);
    if (color == green) return QColor(12, 119, 81);
    if (color == pink) return QColor(125, 57, 165);
    return color;
}
struct Description {
  const char *title;
  const char *caption;
};
const QMap<QString, Description> &descriptions() {
  static const QMap<QString, Description> values = {
      {"fillet",
       {"SEZIONE CIRCOLARE COSTANTE",
        "Seleziona i bordi e imposta un unico raggio"}},
      {"chamfer",
       {"SMUSSO DEI BORDI",
        "Seleziona i bordi e definisci le distanze dello smusso"}},
      {"extrusion",
       {"ESTRUSIONE DEL PROFILO",
        "Seleziona il profilo e definisci distanza e direzione"}},
      {"revolution",
       {"RIVOLUZIONE ATTORNO A UN ASSE",
        "Seleziona il profilo, l’asse e l’angolo di rotazione"}},
      {"sweep",
       {"SEZIONE LUNGO UN PERCORSO",
        "Scegli una sezione e la curva che deve seguire"}},
      {"loft",
       {"SUPERFICIE TRA SEZIONI ORDINATE",
        "Ordina le sezioni; le guide devono attraversarle tutte"}},
      {"scale",
       {"SCALA DEL CORPO", "Scegli il centro e imposta i fattori di scala"}},
      {"transform",
       {"SPOSTAMENTO E ROTAZIONE",
        "Definisci la traslazione e la rotazione dei corpi selezionati"}},
      {"pattern",
       {"RIPETIZIONE DELLA GEOMETRIA",
        "Imposta direzione, distanza e numero di copie"}},
      {"boolean",
       {"OPERAZIONI TRA SOLIDI", "Seleziona i corpi e scegli come combinarli"}},
      {"draft",
       {"SFORMO DELLE FACCE",
        "Scegli le facce, il piano neutro e l’angolo di sformo"}},
      {"thread",
       {"FILETTATURA DELLA SUPERFICIE",
        "Scegli la faccia cilindrica e definisci profilo e lunghezza"}},
      {"projection",
       {"PROIEZIONE DELLE CURVE",
        "Seleziona le curve e la superficie di destinazione"}},
      {"thicken",
       {"SPESSORE DELLA SUPERFICIE",
        "Imposta lo spessore e il lato di crescita"}},
      {"shell",
       {"SVUOTAMENTO DEL SOLIDO",
        "Seleziona le facce da aprire e lo spessore della parete"}},
      {"delete-face",
       {"RIMOZIONE DELLE FACCE", "Seleziona le facce da eliminare"}},
      {"surface-offset",
       {"OFFSET DELLE FACCE",
        "Scegli le facce, la distanza e il verso dell’offset"}},
      {"ruled",
       {"SUPERFICIE RIGATA",
        "Collega le curve selezionate con generatrici rettilinee"}},
      {"boundary",
       {"SUPERFICIE DAI BORDI",
        "Scegli i bordi che delimitano la nuova superficie"}},
      {"fill",
       {"RIEMPIMENTO DEL CONTORNO",
        "Seleziona il contorno e imposta la continuità sui bordi"}},
      {"planar",
       {"SUPERFICIE PIANA", "Scegli un contorno chiuso e complanare"}},
      {"sew",
       {"CUCITURA DELLE SUPERFICI",
        "Seleziona le superfici da unire lungo i bordi comuni"}},
      {"trim",
       {"RIFILATURA DELLA SUPERFICIE",
        "Scegli la superficie, il limite e la parte da conservare"}},
      {"surface-extend",
       {"ESTENSIONE DELLA SUPERFICIE",
        "Seleziona i bordi liberi e definisci l’estensione"}},
      {"surface-extend-to",
       {"ESTENSIONE FINO AL RIFERIMENTO",
        "Scegli il bordo da estendere e la geometria limite"}},
      {"surface-network",
       {"SUPERFICIE DA RETE DI CURVE",
        "Seleziona le curve nelle due direzioni della rete"}},
      {"surface-poles",
       {"MODIFICA DEI POLI DELLA SUPERFICIE",
        "Sposta i poli per modificare la forma della superficie"}},
      {"helix",
       {"CURVA ELICOIDALE", "Definisci asse, raggio, passo e altezza"}},
      {"datum-plane",
       {"PIANO DI RIFERIMENTO",
        "Scegli i riferimenti e regola posizione e orientamento"}},
      {"sketch-offset",
       {"OFFSET DEL PROFILO", "Seleziona le curve e imposta la distanza"}},
      {"sketch-move",
       {"SPOSTAMENTO DEL PROFILO",
        "Seleziona le entità e definisci la trasformazione"}},
      {"spatial-sketch",
       {"CURVE NELLO SPAZIO",
        "Definisci i punti e i vincoli della curva tridimensionale"}},
      {"extracted-curve",
       {"CURVE DALLA GEOMETRIA",
        "Seleziona i bordi o le facce da cui ricavare le curve"}},
      {"shape-analysis",
       {"ANALISI DELLE SUPERFICI",
        "Controlla forma, riflessi e continuità della geometria"}},
      {"measure",
       {"MISURA DELLA GEOMETRIA",
        "Seleziona i riferimenti da misurare nella vista"}},
      {"mass-properties",
       {"PROPRIETÀ DEL CORPO",
        "Seleziona i corpi per calcolare massa e baricentro"}},
      {"primitive",
       {"CREAZIONE DI UNA PRIMITIVA",
        "Scegli le dimensioni e la posizione del nuovo corpo"}},
      {"sketch-pattern",
       {"RIPETIZIONE DEL PROFILO",
        "Definisci la disposizione e il numero di copie"}}};
  return values;
}
QString key(QString operation) {
  if (operation.startsWith("primitive-"))
    return "primitive";
  if (operation.startsWith("sketch-pattern-"))
    return "sketch-pattern";
  return operation;
}
struct Point {
  double x, y, z;
};
QPointF project(Point a) {
  return {118 + 34 * a.x - 18 * a.y, 166 - 25 * a.z - 9 * a.y};
}
void polygon(QPainter &p, std::initializer_list<Point> points, QColor fill) {
  QPolygonF face;
  for (auto a : points)
    face << project(a);
  p.setPen(QPen(QColor(79, 132, 168), 1));
  p.setBrush(fill);
  p.drawPolygon(face);
}
void box(QPainter &p, Point o, double x, double y, double z,
         bool translucent = false) {
  const double a = o.x, b = o.y, c = o.z;
  QColor front(36, 79, 114), top(48, 108, 143), side(30, 65, 96);
  if (translucent) {
    front.setAlpha(65);
    top.setAlpha(65);
    side.setAlpha(65);
  }
  polygon(p, {{a, b, c}, {a + x, b, c}, {a + x, b, c + z}, {a, b, c + z}},
          front);
  polygon(p, {{a, b, c}, {a, b + y, c}, {a, b + y, c + z}, {a, b, c + z}},
          side);
  polygon(p,
          {{a, b, c + z},
           {a + x, b, c + z},
           {a + x, b + y, c + z},
           {a, b + y, c + z}},
          top);
}
void arrow(QPainter &p, QPointF start, QPointF end, QString label = {},
           QColor color = amber) {
  color = canvasColor(p, color);
  p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(start, end);
  const double a = std::atan2(end.y() - start.y(), end.x() - start.x());
  p.drawLine(end, end - QPointF(8 * std::cos(a - .5), 8 * std::sin(a - .5)));
  p.drawLine(end, end - QPointF(8 * std::cos(a + .5), 8 * std::sin(a + .5)));
  if (!label.isEmpty()) {
    p.setFont(QFont("Sans", 9, QFont::DemiBold));
    p.drawText(end + QPointF(7, -5), label);
  }
}
void ribbon(QPainter &p, const QPainterPath &path, QColor base = cyan,
            qreal width = 33) {
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(QColor(30, 78, 111), width + 2, Qt::SolidLine, Qt::RoundCap));
  p.drawPath(path);
  QLinearGradient g(0, 65, 0, 160);
  g.setColorAt(0, base.darker(150));
  g.setColorAt(.48, base);
  g.setColorAt(1, base.darker(175));
  p.setPen(QPen(QBrush(g), width, Qt::SolidLine, Qt::RoundCap));
  p.drawPath(path);
}
void roundedEdge(QPainter &p, bool chamfer) {
  const auto cross = [&](double x, double v) {
    if (chamfer)
      return project({x, .95 * v, 2.05 + .95 * v});
    const double w = std::sqrt(.5),
                 d = (1 - v) * (1 - v) + 2 * w * v * (1 - v) + v * v;
    return project(
        {x, .95 * v * v / d,
         2.05 * (1 - v) * (1 - v) / d + 3 * (2 * w * v * (1 - v) + v * v) / d});
  };
  polygon(p, {{0, 0, 0}, {8, 0, 0}, {8, 0, 2.05}, {0, 0, 2.05}},
          QColor(36, 79, 114));
  polygon(p, {{0, .95, 3}, {8, .95, 3}, {8, 3, 3}, {0, 3, 3}},
          QColor(48, 108, 143));
  QPainterPath band;
  band.moveTo(cross(0, 0));
  band.lineTo(cross(8, 0));
  for (int k = 1; k <= 32; ++k)
    band.lineTo(cross(8, k / 32.));
  band.lineTo(cross(0, 1));
  for (int k = 31; k >= 0; --k)
    band.lineTo(cross(0, k / 32.));
  band.closeSubpath();
  QLinearGradient glow(cross(4, 0), cross(4, 1));
  glow.setColorAt(0, QColor(38, 126, 176));
  glow.setColorAt(.55, QColor(100, 215, 250));
  glow.setColorAt(1, QColor(42, 157, 204));
  p.fillPath(band, glow);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(canvasColor(p, cyan), 1.8));
  p.drawPath(band);
  auto at = cross(5, .55);
  p.setPen(QPen(canvasColor(p, amber), 1.4));
  p.drawLine(at, at + QPointF(31, -31));
  p.setBrush(amber);
  p.drawEllipse(at, 2.3, 2.3);
  p.setFont(QFont("Sans", 9, QFont::DemiBold));
  p.drawText(at + QPointF(31, -35), chamfer ? QStringLiteral("Distanza")
                                            : QStringLiteral("R costante"));
}
void cylinder(QPainter &p) {
  QLinearGradient g(145, 0, 325, 0);
  g.setColorAt(0, QColor(36, 79, 114));
  g.setColorAt(.37, QColor(99, 211, 255));
  g.setColorAt(.65, QColor(45, 116, 154));
  g.setColorAt(1, QColor(30, 65, 96));
  QPainterPath side;
  side.moveTo(145, 76);
  side.lineTo(145, 147);
  side.cubicTo(145, 176, 325, 176, 325, 147);
  side.lineTo(325, 76);
  side.closeSubpath();
  p.setPen(QPen(QColor(79, 132, 168), 1));
  p.setBrush(g);
  p.drawPath(side);
  p.setBrush(QColor(48, 108, 143));
  p.drawEllipse(QRectF(145, 49, 180, 54));
}
QPointF patch(double u, double v) {
  return project({1 + 7 * u, .4 + 3 * v,
                  1.1 + 1.6 * std::sin(3.141592653589793 * u) *
                            std::sin(3.141592653589793 * v)});
}
void surface(QPainter &p, const QString &operation) {
  QPainterPath body;
  body.moveTo(patch(0, 0));
  for (int i = 1; i <= 30; ++i)
    body.lineTo(patch(i / 30., 0));
  for (int i = 1; i <= 30; ++i)
    body.lineTo(patch(1, i / 30.));
  for (int i = 29; i >= 0; --i)
    body.lineTo(patch(i / 30., 1));
  for (int i = 29; i >= 0; --i)
    body.lineTo(patch(0, i / 30.));
  body.closeSubpath();
  QLinearGradient g(patch(.5, 0), patch(.5, 1));
  g.setColorAt(0, QColor(36, 79, 114));
  g.setColorAt(.5, QColor(61, 149, 185));
  g.setColorAt(1, QColor(48, 108, 143));
  p.setPen(QPen(canvasColor(p, cyan), 1.4));
  p.setBrush(g);
  p.drawPath(body);
  if (operation == "surface-poles" || operation == "surface-network" ||
      operation == "ruled" || operation == "boundary") {
    for (int k = 0; k <= 4; ++k)
      for (int dir = 0; dir < 2; ++dir) {
        QPainterPath line;
        for (int j = 0; j <= 24; ++j) {
          auto a = dir ? patch(k / 4., j / 24.) : patch(j / 24., k / 4.);
          if (j)
            line.lineTo(a);
          else
            line.moveTo(a);
        }
        p.setPen(QPen(operation == "surface-network" ? (dir ? pink : cyan)
                                                     : QColor(136, 201, 225),
                      1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
      }
  }
  if (operation == "surface-poles") {
    p.setBrush(amber);
    p.setPen(Qt::NoPen);
    for (int i = 0; i <= 4; ++i)
      for (int j = 0; j <= 3; ++j)
        p.drawEllipse(patch(i / 4., j / 3.), 2.3, 2.3);
    arrow(p, patch(.5, .67), patch(.5, .67) + QPointF(0, -47),
          QStringLiteral("Polo"));
  } else if (operation == "surface-offset" || operation == "thicken" ||
             operation == "projection") {
    p.save();
    p.translate(0, -24);
    p.setOpacity(.5);
    p.fillPath(body, QColor(124, 222, 185));
    p.setPen(QPen(canvasColor(p, green), 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawPath(body);
    p.restore();
    arrow(p, patch(.7, .5), patch(.7, .5) + QPointF(0, -28),
          QStringLiteral("Distanza"));
  } else if (operation == "trim") {
    p.setPen(QPen(canvasColor(p, amber), 2, Qt::DashLine));
    p.drawLine(patch(.6, 0) + QPointF(0, 15), patch(.6, 1) + QPointF(0, -15));
  } else if (operation.startsWith("surface-extend")) {
    arrow(p, patch(1, .5), patch(1, .5) + QPointF(50, 0),
          QStringLiteral("Estensione"));
  } else if (operation == "sew") {
    p.setPen(QPen(canvasColor(p, green), 3));
    p.drawLine(patch(.5, 0), patch(.5, 1));
  }
}
} // namespace

bool hasOperationIllustration(const QString &operation) {
  return descriptions().contains(key(operation));
}
QIcon operationPanelIcon(const QString &operation) {
  QIcon icon;
  for (int size : {24, 48}) {
    QPixmap px(size, size);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24., size / 24.);
    p.setPen(QPen(canvasColor(p, cyan), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (operation == "fillet" || operation == "chamfer") {
      QPainterPath path;
      path.moveTo(4, 20);
      path.lineTo(4, 11);
      if (operation == "fillet")
        path.quadTo(4, 4, 11, 4);
      else
        path.lineTo(11, 4);
      path.lineTo(20, 4);
      p.drawPath(path);
    } else if (operation.contains("surface") || operation == "loft" ||
               operation == "sweep") {
      QPainterPath path;
      path.moveTo(3, 17);
      path.cubicTo(6, 3, 14, 23, 21, 6);
      p.drawPath(path);
      p.setPen(QPen(canvasColor(p, pink), 1.5));
      p.drawLine(4, 6, 4, 19);
      p.drawLine(20, 4, 20, 17);
    } else {
      p.drawPolygon(QPolygonF{{4, 8}, {15, 4}, {21, 8}, {10, 12}});
      p.drawPolygon(QPolygonF{{4, 8}, {10, 12}, {10, 21}, {4, 17}});
      p.drawPolygon(QPolygonF{{10, 12}, {21, 8}, {21, 17}, {10, 21}});
    }
    p.end();
    icon.addPixmap(px);
  }
  return icon;
}
void paintOperationIllustration(QPainter &p, const QRectF &bounds,
                                const QString &id) {
  const QString operation = key(id);
  if (!descriptions().contains(operation))
    return;
  const auto description = descriptions().value(operation);
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(lightCanvas(p) ? QColor(Qt::white) : QColor(21, 29, 40));
  p.drawRoundedRect(bounds, 9, 9);
  p.translate(bounds.topLeft());
  p.scale(bounds.width() / 510., bounds.height() / 211.);
  p.setPen(lightCanvas(p) ? QColor(64, 84, 102) : QColor(157, 176, 196));
  p.setFont(QFont("Sans", 8));
  p.drawText(QRectF(14, 10, 482, 20), QString::fromUtf8(description.title));
  if (operation == "fillet" || operation == "chamfer")
    roundedEdge(p, operation == "chamfer");
  else if (operation == "extrusion") {
    box(p, {.3, 0, 0}, 6, 3, 3);
    p.setPen(QPen(canvasColor(p, cyan), 2));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(QPolygonF{project({.3, 0, 0}), project({6.3, 0, 0}),
                            project({6.3, 0, 3}), project({.3, 0, 3})});
    arrow(p, project({4, 0, 3.4}), project({4, 3, 3.4}),
          QStringLiteral("Distanza"));
  } else if (operation == "revolution" || operation == "helix" ||
             operation == "thread" || id == "primitive-1") {
    cylinder(p);
    if (operation == "revolution") {
      p.setPen(QPen(canvasColor(p, amber), 1.6));
      p.setBrush(Qt::NoBrush);
      p.drawArc(QRectF(122, 37, 225, 91), 20 * 16, 290 * 16);
      arrow(p, {333, 50}, {342, 61}, QStringLiteral("Angolo"));
      p.setPen(QPen(QColor(157, 176, 196), 1, Qt::DashLine));
      p.drawLine(235, 37, 235, 174);
    } else if (operation == "helix" || operation == "thread") {
      QPainterPath helix;
      for (int i = 0; i <= 180; ++i) {
        double t = i / 180., a = t * 8 * 3.141592653589793;
        QPointF at(235 + 88 * std::cos(a), 68 + 90 * t + 23 * std::sin(a));
        if (i)
          helix.lineTo(at);
        else
          helix.moveTo(at);
      }
      p.setPen(QPen(canvasColor(p, amber), 2));
      p.setBrush(Qt::NoBrush);
      p.drawPath(helix);
    }
  } else if (id == "primitive-2") {
    QRadialGradient glow(QPointF(206, 88), 105, QPointF(193, 77));
    glow.setColorAt(0, QColor(126, 226, 255));
    glow.setColorAt(.5, QColor(48, 145, 185));
    glow.setColorAt(1, QColor(27, 64, 97));
    p.setPen(QPen(QColor(79, 132, 168), 1));
    p.setBrush(glow);
    p.drawEllipse(QRectF(170, 43, 136, 131));
  } else if (id == "primitive-3") {
    QLinearGradient glow(155, 0, 325, 0);
    glow.setColorAt(0, QColor(36, 79, 114));
    glow.setColorAt(.4, cyan);
    glow.setColorAt(1, QColor(30, 65, 96));
    QPainterPath cone;
    cone.moveTo(151, 146);
    cone.lineTo(216, 61);
    cone.lineTo(264, 61);
    cone.lineTo(329, 146);
    cone.cubicTo(313, 183, 167, 183, 151, 146);
    cone.closeSubpath();
    p.setPen(QPen(QColor(79, 132, 168), 1));
    p.setBrush(glow);
    p.drawPath(cone);
    p.setBrush(QColor(48, 108, 143));
    p.drawEllipse(QRectF(216, 53, 48, 16));
  } else if (id == "primitive-4") {
    QPainterPath torus;
    torus.setFillRule(Qt::OddEvenFill);
    torus.addEllipse(QRectF(137, 51, 214, 119));
    torus.addEllipse(QRectF(202, 82, 84, 42));
    QLinearGradient glow(0, 51, 0, 170);
    glow.setColorAt(0, QColor(48, 108, 143));
    glow.setColorAt(.44, cyan);
    glow.setColorAt(1, QColor(30, 65, 96));
    p.setPen(QPen(QColor(79, 132, 168), 1));
    p.setBrush(glow);
    p.drawPath(torus);
  } else if (operation == "sweep") {
    QPainterPath path;
    path.moveTo(110, 143);
    path.cubicTo(170, 147, 190, 62, 338, 75);
    ribbon(p, path);
    p.setBrush(QColor(36, 79, 114));
    p.setPen(QPen(canvasColor(p, cyan), 1.8));
    p.drawEllipse(QRectF(99, 128, 22, 30));
    p.setPen(QPen(canvasColor(p, amber), 1.4, Qt::DashLine));
    p.drawPath(path);
    arrow(p, {310, 74}, {348, 77}, QStringLiteral("Percorso"));
  } else if (operation == "loft") {
    auto section = [](double x, double v) {
      double r = 1.1 + .7 * std::sin(x * 3.141592653589793 / 7);
      return project({x, 1.5 + r * std::cos(v), 1.5 + r * .8 * std::sin(v)});
    };
    QPainterPath body;
    body.moveTo(section(.6, -1.570796326794897));
    for (int i = 1; i <= 40; ++i)
      body.lineTo(section(.6 + i * 6.8 / 40., -1.570796326794897));
    for (int i = 0; i <= 32; ++i)
      body.lineTo(
          section(7.4, -1.570796326794897 + i * 3.141592653589793 / 32));
    for (int i = 40; i >= 0; --i)
      body.lineTo(section(.6 + i * 6.8 / 40., 1.570796326794897));
    for (int i = 0; i <= 32; ++i)
      body.lineTo(section(.6, 1.570796326794897 + i * 3.141592653589793 / 32));
    body.closeSubpath();
    QLinearGradient g(0, 50, 0, 164);
    g.setColorAt(0, QColor(48, 108, 143));
    g.setColorAt(.5, QColor(99, 211, 255));
    g.setColorAt(1, QColor(36, 79, 114));
    p.fillPath(body, g);
    for (double x : {.6, 3.5, 7.4}) {
      QPainterPath curve;
      for (int k = 0; k <= 64; ++k) {
        auto a = section(x, k * 2 * 3.141592653589793 / 64);
        if (k)
          curve.lineTo(a);
        else
          curve.moveTo(a);
      }
      p.setPen(QPen(canvasColor(p, cyan), 1.7));
      p.setBrush(Qt::NoBrush);
      p.drawPath(curve);
    }
    QPainterPath guide;
    for (int i = 0; i <= 40; ++i) {
      auto a = section(.6 + i * 6.8 / 40., 1.1);
      if (i)
        guide.lineTo(a);
      else
        guide.moveTo(a);
    }
    p.setPen(QPen(canvasColor(p, pink), 2));
    p.drawPath(guide);
  } else if (operation == "scale" || operation == "transform" ||
             operation == "pattern" || operation == "sketch-pattern") {
    if (operation == "pattern" || operation == "sketch-pattern") {
      for (int k = 0; k < 3; ++k)
        box(p, {k * 2.8, 0, 0}, 1.5, 1.6, 1.6, k != 0);
      arrow(p, {177, 66}, {366, 66}, QStringLiteral("Passo"));
    } else {
      box(p, {0, 0, 0}, 2, 1.8, 1.8, true);
      box(p, {4, 0, 0}, operation == "scale" ? 3.6 : 2,
          operation == "scale" ? 3 : 1.8, operation == "scale" ? 3 : 1.8);
      arrow(p, {185, 119}, {239, 104},
            operation == "scale" ? QStringLiteral("Fattore")
                                 : QStringLiteral("Spostamento"));
    }
  } else if (operation == "boolean") {
    box(p, {.6, 0, 0}, 4.5, 2.7, 2.7);
    box(p, {3, 1.3, 1}, 3.4, 2, 2.1, true);
    arrow(p, {299, 89}, {328, 57}, QStringLiteral("Corpi"));
  } else if (operation == "shell" || operation == "delete-face") {
    box(p, {.6, 0, 0}, 7, 3, 2.8);
    polygon(
        p, {{.9, .3, 2.81}, {7.3, .3, 2.81}, {7.3, 2.7, 2.81}, {.9, 2.7, 2.81}},
        QColor(21, 42, 59));
    arrow(p, {300, 103}, {349, 65}, QStringLiteral("Parete"));
  } else if (operation == "draft") {
    polygon(p, {{0, 0, 0}, {7, 0, 0}, {6.3, .5, 3}, {.7, .5, 3}},
            QColor(36, 79, 114));
    polygon(p, {{.7, .5, 3}, {6.3, .5, 3}, {6.3, 2.5, 3}, {.7, 2.5, 3}},
            QColor(48, 108, 143));
    arrow(p, {351, 154}, {335, 87}, QStringLiteral("Angolo"));
  } else if (operation == "datum-plane") {
    box(p, {1, 0, 0}, 5, 2.4, 2);
    polygon(
        p, {{-.1, -.4, 2.6}, {7.2, -.4, 2.6}, {7.2, 3.5, 2.6}, {-.1, 3.5, 2.6}},
        QColor(99, 211, 255, 75));
    arrow(p, {290, 118}, {290, 63}, QStringLiteral("Offset"));
  } else if (operation == "measure" || operation == "mass-properties") {
    box(p, {1, 0, 0}, 6, 2.5, 2.5);
    arrow(p, {145, 174}, {347, 174}, QStringLiteral("Misura"));
    if (operation == "mass-properties") {
      p.setPen(QPen(canvasColor(p, amber), 1.5));
      p.drawLine(219, 105, 239, 105);
      p.drawLine(229, 95, 229, 115);
      p.drawEllipse(QPointF(229, 105), 6, 6);
    }
  } else if (operation == "spatial-sketch" || operation == "extracted-curve" ||
             operation.startsWith("sketch-")) {
    QPainterPath curve;
    curve.moveTo(113, 148);
    curve.cubicTo(165, 31, 248, 181, 359, 63);
    p.setPen(QPen(canvasColor(p, cyan), 2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(curve);
    if (operation == "sketch-offset") {
      p.save();
      p.translate(0, -15);
      p.setPen(QPen(canvasColor(p, green), 2));
      p.drawPath(curve);
      p.restore();
    }
    p.setPen(Qt::NoPen);
    p.setBrush(amber);
    for (auto a : {QPointF(113, 148), QPointF(235, 114), QPointF(359, 63)})
      p.drawEllipse(a, 3, 3);
  } else if (operation == "primitive")
    box(p, {.6, 0, 0}, 7, 3, 3);
  else
    surface(p, operation);
  p.setPen(lightCanvas(p) ? QColor(64, 84, 102) : QColor(157, 176, 196));
  p.setFont(QFont("Sans", 8));
  p.drawText(QRectF(14, 181, 482, 23), Qt::AlignCenter,
             QString::fromUtf8(description.caption));
  p.restore();
}
OperationIllustration::OperationIllustration(const QString &operation,
                                             QWidget *parent)
    : QWidget(parent), operation_(operation) {
  setObjectName("operationIllustration");
  setFixedHeight(211);
  setMinimumWidth(280);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setAttribute(Qt::WA_TransparentForMouseEvents);
}
void OperationIllustration::setOperation(const QString &operation) {
  if (operation_ == operation)
    return;
  operation_ = operation;
  update();
}
void OperationIllustration::paintEvent(QPaintEvent *) {
  QPainter p(this);
  paintOperationIllustration(p, rect(), operation_);
}
} // namespace ForgeCad::Gui
