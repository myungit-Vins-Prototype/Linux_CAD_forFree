#include "cad_loft_correspondence.h"
#include "cad_datum.h"
#include "cad_features.h"
#include "cad_forge.h"
#include "cad_kernel.h"
#include <cmath>
#include <stdexcept>
namespace ForgeCad {
using namespace Kernel;
std::vector<LoftSection> resolveLoftSections(const ExtrusionObject &d, int owner, const QVector<SketchObject> &sketches,
                                             const QVector<ExtrusionObject> &bodies) {
    if (owner < 0)
        owner = bodies.size();
    QVector<GeometryRef> refs = d.loftSections;
    if (refs.isEmpty())
        for (int index : d.loftSketches) {
            GeometryRef ref;
            ref.kind = 7;
            ref.index = index;
            ref.element = {-1, -1, -1};
            refs.append(ref);
        }
    std::vector<LoftSection> result;
    for (int k = 0; k < refs.size(); ++k) {
        auto ref = refs[k];
        if(ref.kind==7 && ref.index>=0 && ref.index<sketches.size()) {
            const auto &sketch=sketches[ref.index];
            int point=-1,count=0;
            if(ref.element.kind==-1 && forgeSketchSegments(sketch).empty()) {
                for(int i=0;i<sketch.curves.size();++i)
                    if(sketch.curves[i].tool==DrawingTool::Point && isSketchEntityVisible(sketch,1,i,false)){point=i;++count;}
                if(count==1)ref.element={1,point,0};
            }
            if(ref.element.kind==1 && ref.element.element>=0 && ref.element.element<sketch.curves.size()
                && sketch.curves[ref.element.element].tool==DrawingTool::Point) {
                ref.kind=6;ref.element.point=0;
            }
        }
        LoftSection section;
        const auto fail = [&](QString message) {
            throw std::domain_error((message + QStringLiteral(" [[loft-section=%1]]").arg(k)).toStdString());
        };
        if (ref.kind == 7 || ref.kind == 6) {
            if (ref.index < 0 || ref.index >= sketches.size())
                fail(QStringLiteral("Lo schizzo della sezione non esiste piu'."));
            if (ref.element.kind >= 0 &&
                !isSketchEntityVisible(sketches[ref.index], ref.element.kind, ref.element.element, false))
                fail(QStringLiteral("Un'entita' di costruzione non e' una sezione."));
        }
        if(geometryRefRoles(ref,sketches)&DatumRolePoint) {
            ResolvedRef resolved;QString error;
            if(!resolveGeometryRef(ref,owner,sketches,bodies,resolved,&error) || !resolved.hasPoint)fail(error);
            section.point=resolved.point;
            if(ref.kind==6){double height;forgeSketchFrame(sketches[ref.index],0.0,section.frame,height);}
        } else if (ref.kind == 7 && ref.element.kind == -1) {
            const auto &sketch = sketches[ref.index];
            if (sketch.datumPlane >= owner)
                fail(QStringLiteral("Una sezione sta su un piano successivo."));
            const auto profile = buildProfile(forgeSketchSegments(sketch), kSketchConnectionTolerance);
            double height;
            forgeSketchFrame(sketch, 0.0, section.frame, height);
            if (profile.regions.size() == 1 && profile.regions.front().holes.empty() && profile.chains.empty())
                section.loop = profile.regions.front().outer;
            else if (profile.regions.empty() && profile.chains.size() == 1)
                section.loop = profile.chains.front();
            else
                fail(QStringLiteral("Serve un solo contorno senza fori o una sola catena."));
        } else {
            QString error;
            if (!geometryRefPath(ref, owner, sketches, bodies, section.path, &error))
                fail(error);
        }
        result.push_back(std::move(section));
    }
    return result;
}
void applyLoftCorrespondences(const QVector<LoftSectionCorrespondence> &controls,
                              const std::vector<LoftSection> &sections, LoftOptions &options) {
    options.correspondencePoints.clear();
    options.reverseSections.clear();
    if (controls.isEmpty())
        return;
    if (controls.size() != int(sections.size()))
        throw std::domain_error("loft: corrispondenze incomplete");
    const int count = controls.front().positions.size();
    if (count < 1 || count > 64)
        throw std::domain_error("loft: numero di collegamenti non valido");
    for (int i = 0; i < controls.size(); ++i) {
        if (controls[i].positions.size() != count)
            throw std::domain_error("loft: manca un punto di corrispondenza");
        std::vector<Vec3> points;
        for (double position : controls[i].positions)
            points.push_back(loftSectionPoint(sections[i], position));
        options.correspondencePoints.push_back(std::move(points));
        options.reverseSections.push_back(controls[i].reverse);
    }
}
} // namespace ForgeCad
