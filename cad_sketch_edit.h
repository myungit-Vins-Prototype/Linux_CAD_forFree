#ifndef FORGECAD_SKETCH_EDIT_H
#define FORGECAD_SKETCH_EDIT_H

#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

#include "cad_types.h"

// Modifica delle entita' dello schizzo: taglia, estendi, spezza, raccordo e
// smusso. Tutto in double sulla geometria esatta: le intersezioni vengono da
// Geom2dAPI_InterCurveCurve sulle curve di `curveGeometry` (rette e cerchi in
// forma chiusa); un tratto di segmento o d'arco resta un segmento o un arco
// dello stesso cerchio, un tratto di spline e' fatto dei tratti di Bezier
// esatti (de Casteljau). Le NURBS fanno da bordo di taglio ma non si
// modificano (i loro nodi sono uniformi per costruzione); un poligono si
// scompone prima nei suoi lati.
//
// Le funzioni lavorano su una copia: se falliscono lo schizzo non cambia.
// Le curve modificate vanno ricampionate (`recalculateCurve`) dal chiamante.
namespace ForgeCad {

struct SketchEntity {
    int kind = -1;   // 0 segmento, 1 curva
    int index = -1;
};

struct SketchEditResult {
    QString error;            // vuoto = riuscita
    QVector<int> segmentMap;  // vecchio indice di segmento -> nuovo (-1 eliminato); vuoto = indici invariati
};

// Taglia: toglie il tratto dell'entita' tra le due intersezioni con le altre
// entita' che stanno attorno a `pick` (fino all'estremo se da quella parte
// non ce ne sono). Senza intersezioni l'entita' sparisce; un cerchio con
// almeno due intersezioni diventa un arco.
SketchEditResult trimSketchEntity(SketchObject &sketch, SketchEntity entity, const QPointF &pick);

// Tratto che il taglio toglierebbe, campionato solo per disegnarlo (vuoto se non si puo').
QVector<QPointF> trimPreview(const SketchObject &sketch, SketchEntity entity, const QPointF &pick);

// Estendi: l'estremo del segmento o dell'arco piu' vicino a `pick` si
// allunga (lungo la retta o il cerchio) fino alla prima entita' che incontra.
SketchEditResult extendSketchEntity(SketchObject &sketch, SketchEntity entity, const QPointF &pick);

// Spezza: l'entita' si divide in due nel punto piu' vicino a `point`, che si
// aggancia alle intersezioni con le altre entita' (e al punto medio dei
// segmenti) entro `snapTolerance`. Un cerchio diventa un arco chiuso con un
// vertice nel punto.
SketchEditResult splitSketchEntity(SketchObject &sketch, SketchEntity entity, const QPointF &point, double snapTolerance);

// Raccordo (arco di raggio `size`) o smusso (segmento tra i punti a distanza
// `size` dallo spigolo) tra due segmenti. `pickFirst`/`pickSecond` stanno
// sulla parte da tenere di ciascuno: i segmenti si accorciano (o si
// allungano) fino ai punti di tangenza, oltre lo spigolo spariscono.
SketchEditResult blendSketchSegments(SketchObject &sketch, int first, const QPointF &pickFirst, int second,
                                     const QPointF &pickSecond, double size, bool chamfer);

// Spigolo vicino a `point` (entro `tolerance`): un estremo comune a due soli
// segmenti. Dà i due segmenti e un punto sulla parte da tenere di ciascuno.
bool sketchCornerAt(const SketchObject &sketch, const QPointF &point, double tolerance, int &first, QPointF &pickFirst,
                    int &second, QPointF &pickSecond);

// Elimina segmenti e curve (con i loro vincoli e le linee di costruzione) e
// restituisce la mappa dei segmenti vecchio -> nuovo indice (-1 eliminato).
QVector<int> removeSketchEntities(SketchObject &sketch, const QSet<int> &segments, const QSet<int> &curves);

}

#endif
