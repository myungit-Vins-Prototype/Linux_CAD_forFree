#include "cad_solid_trim.h"
#include "cad_topology_ref.h"
#include "cad_element_names.h"
#include "fk_solid_trim.h"
#include "fk_sew.h"
#include <stdexcept>
namespace ForgeCad {
void buildSolidTrim(ExtrusionObject &feature,int owner,const QVector<ExtrusionObject> &bodies) {
    using namespace Kernel;
    try {
        const auto &p=feature.surfaceTool;
        if(feature.firstBody<0||feature.firstBody>=owner||feature.firstBody>=bodies.size()||!bodies[feature.firstBody].forgeBody)
            throw std::domain_error("Scegliere un corpo solido valido da tagliare.");
        const auto source=bodies[feature.firstBody].forgeBody;
        if(source->isSheet())throw std::domain_error("Il corpo da tagliare deve contenere solidi, anche disconnessi.");
        if(p.targets.size()!=2)throw std::domain_error("Scegliere due facce limite.");
        std::vector<ForgeBody> operands{source};
        for(const auto &ref:p.targets) {
            if(ref.kind!=5||ref.index<0||ref.index>=owner||ref.index>=bodies.size()||!bodies[ref.index].forgeBody)
                throw std::domain_error("Faccia limite non disponibile.");
            const auto original=bodies[ref.index].forgeBody;
            const auto face=resolveFaceReference(*original,ref.point,1e-5);
            if(!face.valid())throw std::domain_error("Faccia limite mancante o ambigua: riselezionarla.");
            BodyHistory extraction;
            auto sheet=std::make_shared<const Body>(facesAsSheet(*original,{face},&extraction));
            registerDeclaredHistory(sheet,{original},std::move(extraction));operands.push_back(sheet);
        }
        BodyHistory history;
        auto result=trimSolidBetweenFaces(*source,*operands[1],*operands[2],p.trimReverseFirst,p.trimReverseSecond,{},&history);
        feature.forgeBody=std::make_shared<const Body>(std::move(result));
        registerDeclaredHistory(feature.forgeBody,operands,std::move(history));
        feature.solid=true;feature.error.clear();
        feature.notice=QStringLiteral("%1 facce. Conservati tutti i volumi presenti tra le superfici; chiusure sui limiti esatti.").arg(feature.forgeBody->faces().size());
    } catch(const std::exception &error) {feature.forgeBody.reset();feature.error=QString::fromUtf8(error.what());}
}
}
