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
    struct Entry {std::shared_ptr<const Kernel::Body> body;std::shared_ptr<const Kernel::Tessellation> mesh;};
    std::map<const Kernel::Body *,Entry> entries_;
public:
    std::shared_ptr<const Kernel::Tessellation> seeds(const DrawingSourceBody &source);
    void clear(){entries_.clear();}
};
QString drawingViewSilhouettes(const DrawingModelSnapshot &model,const DrawingView &view,
                              DrawingSilhouetteResult &contours,DrawingSilhouetteCache &cache);
}
