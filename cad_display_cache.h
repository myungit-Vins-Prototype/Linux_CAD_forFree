#ifndef FORGECAD_DISPLAY_CACHE_H
#define FORGECAD_DISPLAY_CACHE_H

#include "cad_types.h"
#include <QOpenGLBuffer>
#include <memory>
#include <vector>

namespace ForgeCad {
// Solo dati di visualizzazione. Tutti i metodi richiedono il contesto GL corrente.
class DisplayCache {
public:
    void beginFrame();
    void clear();
    void faces(const BodyDisplay &display);
    void edges(const BodyDisplay &display);
private:
    struct Entry {
        BodyDisplay source; // mantiene vivi i dati implicitamente condivisi della chiave
        QOpenGLBuffer positions, normals, lines;
        QVector<int> starts, counts;
        unsigned long used = 0;
        bool ready = false;
    };
    Entry &get(const BodyDisplay &display);
    std::vector<std::unique_ptr<Entry>> entries_;
    unsigned long frame_ = 0;
};
}
#endif
