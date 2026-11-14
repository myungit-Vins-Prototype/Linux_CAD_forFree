#pragma once

static void featureFaceSearchRegressions() {
    using namespace ForgeCad;
    using namespace Kernel;
    auto base = std::make_shared<const Body>(makeBox(Frame3(),6,4,3));
    auto bevel = std::make_shared<const Body>(blendEdges(*base,{nearestEdge(*base,Vec3(6,2,3),1e-6)},.5,true));
    auto moved = std::make_shared<const Body>(transformBody(*bevel,Transform3::translation(Vec3(20,0,0))));
    const std::vector<std::pair<int,ForgeBody>> chain{{0,base},{1,bevel},{2,moved}};
    for (const ForgeBody &result : {bevel,moved}) {
        BodyDisplay display; forgeTessellate(*result,0,display);
        for (int feature : {0,1,2}) {
            std::vector<std::pair<int,ForgeBody>> upTo;
            for (const auto &entry : chain) if (entry.first <= feature) upTo.push_back(entry);
            QVector<int> expected;
            for (qsizetype i=0;i<display.faceIds.size();++i) {
                const int face=display.faceIds[i]; const auto label=display.faceLabelPoints[i];
                const auto &surface=*result->face(FaceId(face)).surface;
                const auto on=projectPoint(surface,Vec3(label.x(),label.y(),label.z()));
                if (forgeFaceOwner(*result,face,surface.point(on.u,on.v),upTo)==feature) expected.append(face);
            }
            require(forgeFeatureFaces(*result,display.faceIds,display.faceLabelPoints,feature,chain)==expected,
                    "ricerca condivisa uguale al picking singolo per base, smusso e traslazione");
        }
        require(forgeFeatureFaces(*result,display.faceIds,display.faceLabelPoints,1,{}).isEmpty(),
                "nessun proprietario senza storia");
        require(forgeFeatureFaces(*result,display.faceIds,{},1,chain).isEmpty(), "nessun punto etichetta: nessuna faccia");
    }
}
