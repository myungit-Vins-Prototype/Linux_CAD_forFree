#pragma once
#include "cad_drawing.h"
#include "fk_tessellate.h"
#include <map>
#include <QStringList>
namespace ForgeCad {
// Contorni apparenti per la presentazione: punti ricavati sulle superfici,
// separati dai bordi e dalle sezioni esatte usati come riferimenti di quota.
struct DrawingSilhouette {
    quint64 featureId=0;
    Kernel::FaceId sourceFace;
    std::vector<Kernel::Vec3> points;
    bool refinementLimited=false;
};
struct DrawingSilhouetteResult {std::vector<DrawingSilhouette> paths;QStringList warnings;};
class DrawingSilhouetteCache {
    struct Entry {std::shared_ptr<const Kernel::Body> body;std::shared_ptr<const Kernel::Tessellation> mesh;double deflection=1.0;};
    std::map<const Kernel::Body *,Entry> entries_;
public:
    std::shared_ptr<const Kernel::Tessellation> seeds(const DrawingSourceBody &source,double maximumDeflection=1.0);
    // Query conservativa: la copia locale puo' completare la vista senza
    // generare o raffinare mesh. Non modifica la cache condivisa per valore.
    bool canReuse(const DrawingSourceBody &source,double maximumDeflection)const{
        const auto found=entries_.find(source.body.get());
        return found!=entries_.end()&&maximumDeflection>0&&found->second.deflection<=maximumDeflection;
    }
    void clear(){entries_.clear();}
};
QString drawingViewSilhouettes(const DrawingModelSnapshot &model,const DrawingView &view,
                              DrawingSilhouetteResult &contours,DrawingSilhouetteCache &cache);
}
