#ifndef FORGECAD_SKETCH_EDIT_H
#define FORGECAD_SKETCH_EDIT_H

#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

#include "cad_types.h"

// Modifica delle entita' dello schizzo: taglia, estendi, spezza, raccordo e
// smusso. Tutto in double sulla geometria esatta: le intersezioni vengono da
// intersectCurves del kernel (fk_intersect) sulle curve di `curveGeometry`
// (rette e cerchi in forma chiusa); un tratto di segmento o d'arco resta un segmento o un arco
// dello stesso cerchio, un tratto di spline e' fatto dei tratti di Bezier
// esatti (de Casteljau). Gli archi ellittici restano NURBS razionali esatte;
// ellissi, NURBS e riferimenti copiati si possono tagliare e spezzare. Un poligono si
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

// Ripetizione di entita' dello schizzo (segmenti e curve, anche di
// costruzione): copie esatte trasformate nel piano.
//  - kind 0 lineare: `count` istanze lungo `direction` (versore) ogni
//    `spacing` e, se count2 > 1, `count2` lungo `direction2` ogni `spacing2`
//    (griglia); l'istanza 0 e' l'originale.
//  - kind 1 circolare: `count` istanze attorno a `center`; `angle` (gradi) e'
//    l'angolo totale se `spread` (360: il giro diviso in parti uguali,
//    altrimenti tra la prima e l'ultima), il passo altrimenti. Con segno: antiorario.
//  - kind 2 specchio: l'immagine rispetto alla retta per `axisPoint` lungo
//    `axisDirection` (gli archi si percorrono ancora in senso antiorario).
// I vincoli tra le entita' ripetute si copiano quando restano veri (non i
// fissi, ne' quelli verso origine e assi, ne' orizzontale/verticale se le
// copie ruotano, ne' gli angoli nello specchio); gli estremi delle copie che
// toccano altri punti diventano coincidenti. `created` riceve le entita' nuove.
struct SketchPattern {
    int kind = 0;
    QPointF direction{1.0, 0.0};
    double spacing = 10.0;
    int count = 3;
    QPointF direction2{0.0, 1.0};
    double spacing2 = 10.0;
    int count2 = 1;
    QPointF center;
    double angle = 360.0;
    bool spread = true;
    QPointF axisPoint, axisDirection{0.0, 1.0};
    // Parametrica: le copie restano legate alle entita' di partenza con un
    // vincolo Pattern (niente vincoli copiati: sono gia' determinate), con il
    // passo o l'angolo come quota se `dimensioned`. I riferimenti dello
    // schizzo da cui vengono direzioni, centro e retta dello specchio (kind -1:
    // valori fissi, quelli sopra) seguono la geometria.
    bool parametric = true;
    bool dimensioned = true;
    ConstraintRef directionRef, direction2Ref, centerRef, axisRef;
};
SketchEditResult patternSketchEntities(SketchObject &sketch, const QVector<SketchEntity> &entities, const SketchPattern &pattern,
                                       QVector<SketchEntity> *created = nullptr);

// Sposta, ruota o copia entita' dello schizzo: rotazione di `angle` gradi
// (antiorario) attorno a `center`, poi traslazione di `translation`. Le
// entita' spostate perdono i vincoli che non valgono piu' (verso le entita'
// rimaste ferme, fissi, orizzontali/verticali dopo una rotazione) e prendono
// le coincidenze con i punti che toccano; le copie (`copy`) portano i vincoli
// tra le entita' copiate che restano veri. `created`: le copie o le entita' spostate.
struct SketchMove {
    QPointF translation;
    QPointF center;
    double angle = 0.0;
    bool copy = false;
};
SketchEditResult moveSketchEntities(SketchObject &sketch, const QVector<SketchEntity> &entities, const SketchMove &move,
                                    QVector<SketchEntity> *created = nullptr);

// Parametri nuovi della ripetizione parametrica `constraint` (istanze, passi,
// angolo, quote): se il numero di istanze non cambia si aggiornano i valori
// (le copie si spostano con il risolutore), altrimenti le copie si rifanno.
SketchEditResult editSketchPattern(SketchObject &sketch, int constraint, const SketchPatternData &values);

// Elimina segmenti e curve (con i loro vincoli e le linee di costruzione) e
// restituisce la mappa dei segmenti vecchio -> nuovo indice (-1 eliminato).
QVector<int> removeSketchEntities(SketchObject &sketch, const QSet<int> &segments, const QSet<int> &curves);

}

#endif
