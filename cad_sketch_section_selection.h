#ifndef FORGECAD_SKETCH_SECTION_SELECTION_H
#define FORGECAD_SKETCH_SECTION_SELECTION_H

#include "cad_types.h"
#include <QSet>
#include <QStringList>

// Rami esplicitamente conservati dopo l'eliminazione di parte di una sezione.
// Gli indici si riferiscono all'uscita completa della medesima sorgente, prima
// della compattazione nello schizzo. Il nome persistito evita nuovi campi del
// codec; i lettori precedenti segnalano la sezione parziale come non risolta.
namespace ForgeCad::SketchSectionSelection {
inline QString prefix() { return QStringLiteral("section-subset-v1/"); }
struct Selection {
    bool declared = false, valid = false;
    int segments = 0, curves = 0;
    QVector<int> entries;
};
inline Selection read(const SketchLinkedReference &link) {
    Selection result;
    result.declared = link.source.kind == 11 && link.source.point.name.startsWith(prefix());
    if (!result.declared) return result;
    const auto fields = link.source.point.name.mid(prefix().size()).split('/');
    if (fields.size() != 3) return result;
    bool ok = false;
    result.segments = fields[0].toInt(&ok);
    if (!ok || result.segments < 0 || result.segments > 1000000) return result;
    result.curves = fields[1].toInt(&ok);
    if (!ok || result.curves < 0 || result.curves > 1000000 - result.segments) return result;
    const auto entries = fields[2].split(',');
    if (entries.size() != link.entities.size()) return result;
    QSet<int> seen;
    for (int i = 0; i < entries.size(); ++i) {
        const int value = entries[i].toInt(&ok);
        if (!ok || value < 0 || value >= result.segments + result.curves || seen.contains(value)
            || link.entities[i].kind != (value < result.segments ? 0 : 1)) return result;
        seen.insert(value); result.entries.append(value);
    }
    result.valid = true;
    return result;
}
inline void write(SketchLinkedReference &link, int segments, int curves, const QVector<int> &entries) {
    QStringList values;
    for (int entry : entries) values.append(QString::number(entry));
    link.source.point.name = prefix() + QString::number(segments) + '/' + QString::number(curves) + '/' + values.join(',');
}
inline void retain(SketchLinkedReference &link, const QVector<int> &positions) {
    if (link.source.kind != 11 || positions.size() == link.entities.size()) return;
    auto selection = read(link);
    if (selection.declared && !selection.valid) return;
    if (!selection.declared) {
        // Solo un blocco valido e completo ha ancora l'ordine originale.
        if (!link.error.isEmpty() || !link.source.point.name.isEmpty()) return;
        for (const auto &entity : link.entities) {
            if (entity.kind == 0) ++selection.segments;
            else ++selection.curves;
        }
        int s = 0, c = 0;
        for (const auto &entity : link.entities)
            selection.entries.append(entity.kind == 0 ? s++ : selection.segments + c++);
    }
    QVector<int> kept;
    for (int position : positions) kept.append(selection.entries.at(position));
    write(link, selection.segments, selection.curves, kept);
}
}
#endif
