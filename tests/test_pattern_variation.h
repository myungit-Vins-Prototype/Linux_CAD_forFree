#pragma once
#include "../cad_pattern_variation.h"
#include "../cad_feature_build.h"
#include "../cad_sketch_edit.h"
#include "../cad_pattern_variation_editor.h"
#include "../cad_model_history.h"
#include "../cad_document_io.h"
#include "../cad_history.h"
#include "../forgeCad2026_feature_preview.h"
#include "../cad_drawing_file.h"
#include "../cad_constraints.h"
#include "../kernel/fk_mass.h"
#include "../kernel/fk_body_check.h"
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QCryptographicHash>
#include <cmath>
#include <stdexcept>
#include <iostream>

inline void patternVariationTests() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    const auto require = [](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(message);
    };
    const auto close = [&](double a, double b) {
        require(std::abs(a-b)<1e-6*std::max(1.,std::abs(b)), "misura della ripetizione variabile errata");
    };
    const auto parameter = [&](const ExtrusionObject &definition, const DocumentState &doc,
                               const QString &suffix, double increment) {
        for (const auto &item : patternParameters(definition, doc.sketches, doc.extrusions))
            if (item.reference.parameter.endsWith(suffix)) {
                auto v=item.reference; v.increment=increment; return v;
            }
        throw std::runtime_error(qPrintable("Parametro assente: "+suffix));
    };
    const auto regenerate = [&](DocumentState &doc) {
        normalizeModelHistory(doc);
        for (int i=0;i<doc.extrusions.size();++i) {
            buildGeometry(doc.extrusions[i],i,doc.sketches,doc.extrusions);
            require(doc.extrusions[i].error.isEmpty(),"fixture non valida");
        }
    };
    const auto build = [&](ExtrusionObject definition,const DocumentState &doc) {
        buildGeometry(definition,doc.extrusions.size(),doc.sketches,doc.extrusions);
        if (!definition.error.isEmpty()) throw std::runtime_error(qPrintable(definition.error));
        require(bool(definition.forgeBody),"risultato mancante");
        require(checkBody(*definition.forgeBody).empty(),"B-rep variabile invalido");
        return definition;
    };
    // Booleano: appartengono alla funzione gli utensili diretti, mai la base fissa.
    DocumentState state;
    ExtrusionObject fixed; fixed.feature=BodyFeature::Primitive; fixed.name="Base fissa";
    fixed.primitive.size[0]=fixed.primitive.size[1]=fixed.primitive.size[2]=1;
    fixed.primitive.origin[0]=fixed.primitive.origin[1]=fixed.primitive.origin[2]=-50;
    ExtrusionObject box; box.feature=BodyFeature::Primitive; box.name="Utensile";
    box.primitive.size[0]=2;box.primitive.size[1]=3;box.primitive.size[2]=4;box.primitive.origin[0]=20;
    ExtrusionObject join;join.name="Unione";join.operation=0;join.firstBody=0;join.secondBody=1;
    state.extrusions={fixed,box,join};regenerate(state);
    const auto original=state.extrusions[1].forgeBody;
    ExtrusionObject p;p.feature=BodyFeature::Pattern;p.name="Variabile";p.firstBody=2;
    p.pattern.featureOnly=true;p.pattern.count=4;p.pattern.spacing=20;p.pattern.refs={{2,0,{},{}}};
    p.pattern.variations={parameter(p,state,"size0",1),parameter(p,state,"size2",2)};
    const auto list=patternParameters(p,state.sketches,state.extrusions);
    for(const auto &v:list)require(v.reference.ownerId==state.extrusions[1].featureId,"base fissa nel catalogo");
    auto result=build(p,state);close(massProperties(*result.forgeBody).volume,325);
    require(state.extrusions[1].forgeBody==original && state.extrusions[1].primitive.size[0]==2,"sorgente modificata");
    auto zero=p;for(auto &v:zero.pattern.variations)v.increment=0;
    close(massProperties(*build(zero,state).forgeBody).volume,97);
    auto excluded=p;excluded.pattern.excluded={1};close(massProperties(*build(excluded,state).forgeBody).volume,271);
    auto circular=p;circular.pattern.kind=1;circular.pattern.refs[0].index=2;circular.pattern.angle=360;
    close(massProperties(*build(circular,state).forgeBody).volume,325);
    auto grid=p;grid.pattern.count=2;grid.pattern.count2=2;grid.pattern.spacing2=30;grid.pattern.refs.append({2,1,{},{}});
    close(massProperties(*build(grid,state).forgeBody).volume,325);
    auto bad=p;bad.pattern.featureOnly=false;
    require(patternParameters(bad,state.sketches,state.extrusions).isEmpty(),"catalogo per corpo intero");
    buildGeometry(bad,3,state.sketches,state.extrusions);
    require(!bad.forgeBody && bad.error.contains("corpo intero"),"variazioni su corpo non rifiutate");
    bad=p;bad.pattern.variations[0].increment=-1;buildGeometry(bad,3,state.sketches,state.extrusions);
    require(!bad.forgeBody && bad.error.contains("Istanza 3"),"istanza errata non identificata");
    bad=p;bad.pattern.variations.append(bad.pattern.variations.front());buildGeometry(bad,3,state.sketches,state.extrusions);
    require(!bad.forgeBody && !bad.error.isEmpty(),"parametro duplicato accettato");
    bad=p;bad.pattern.variations[0].increment=std::numeric_limits<double>::infinity();buildGeometry(bad,3,state.sketches,state.extrusions);
    require(!bad.forgeBody && !bad.error.isEmpty(),"incremento infinito accettato");
    auto key=p;key.pattern.variations[0].increment=2;
    require(Gui::FeaturePreviewBuilder::previewKey(p,-1)!=Gui::FeaturePreviewBuilder::previewKey(key,-1),"cache ignora incremento");
    // Una dipendenza dell'utensile non entra nel catalogo e non può essere forzata da file.
    ExtrusionObject scale;scale.feature=BodyFeature::Scale;scale.firstBody=1;scale.scaleFactor=2;
    auto ancestors=state;ancestors.extrusions.insert(2,scale);ancestors.extrusions[3].secondBody=2;regenerate(ancestors);
    auto ap=p;ap.firstBody=3;
    const auto own=patternParameters(ap,ancestors.sketches,ancestors.extrusions);
    require(own.size()==1 && own[0].reference.parameter.endsWith("/scale"),"catalogo espone antenati dell'utensile");
    buildGeometry(ap,4,ancestors.sketches,ancestors.extrusions);
    require(!ap.forgeBody && ap.error.contains("non appartiene"),"parametro antenato forzato accettato");
    // Estrusione: quote del profilo e distanza contemporanee, nessun altro schizzo/piano.
    DocumentState sketches;
    SketchObject sketch;sketch.name="Profilo";
    CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{20,0},{22,0}};sketch.curves={circle};
    SketchConstraint diameter;diameter.type=ConstraintType::Diameter;diameter.first={1,0,-1};diameter.value=4;
    sketch.geometricConstraints={diameter};
    auto other=sketch;other.name="Schizzo estraneo";
    sketches.sketches={sketch,other};
    ExtrusionObject extrusion;extrusion.name="Estrusione";extrusion.sketchIndex=0;extrusion.distance=5;
    extrusion.mergeOperation=1;extrusion.mergeBodies={0};
    sketches.extrusions={fixed,extrusion};regenerate(sketches);
    auto sp=p;sp.firstBody=1;sp.pattern.variations={parameter(sp,sketches,"/value",2),parameter(sp,sketches,"/distance",1)};
    for(const auto &v:patternParameters(sp,sketches.sketches,sketches.extrusions))
        require(v.reference.sketch ? v.reference.ownerId==sketches.sketches[0].storyId
                                  : v.reference.ownerId==sketches.extrusions[1].featureId,"parametri estranei nel catalogo");
    double expected=1;for(int i=0;i<4;++i)expected+=M_PI*(2+i)*(2+i)*(5+i);
    close(massProperties(*build(sp,sketches).forgeBody).volume,expected);
    require(sketches.sketches[0].geometricConstraints[0].value==4,"quota originale mutata");
    auto moved=sketches;SketchConstraint unrelated;unrelated.type=ConstraintType::Horizontal;unrelated.first={2,1,-1};
    moved.sketches[0].geometricConstraints.prepend(unrelated);normalizeModelHistory(moved);
    const auto movedList=patternParameters(sp,moved.sketches,moved.extrusions);
    require(std::any_of(movedList.begin(),movedList.end(),[&](const auto &v){return samePatternParameter(v.reference,sp.pattern.variations[0]);}),"ID quota perso");
    auto removed=sketches;removed.sketches[0].geometricConstraints={diameter};normalizeModelHistory(removed);
    bad=sp;buildGeometry(bad,2,removed.sketches,removed.extrusions);
    require(!bad.forgeBody && bad.error.contains("non esiste"),"quota eliminata riassociata");
    // Formato63, hash62 e Undo/Redo preservano identità e regole.
    sketches.extrusions.append(sp);normalizeModelHistory(sketches);
    QByteArray bytes;require(encodeDocument(sketches,bytes,false).isEmpty(),"codifica63");
    DocumentState decoded;require(decodeDocument(bytes,decoded).isEmpty(),"decodifica63");
    regenerate(decoded);close(massProperties(*decoded.extrusions.last().forgeBody).volume,expected);
    require(decoded.sketches[0].geometricConstraints[0].parameterId==sketches.sketches[0].geometricConstraints[0].parameterId,"ID codec");
    QByteArray legacy;require(!encodeDocumentDefinition62ForHash(sketches,legacy).isEmpty(),"codec62 scarta variazioni");
    auto old=sketches;old.extrusions.removeLast();require(encodeDocumentDefinition62ForHash(old,legacy).isEmpty(),"hash62");
    bool matches=false;require(drawingModelDefinitionMatches(old,QCryptographicHash::hash(legacy,QCryptographicHash::Sha256),matches).isEmpty()&&matches,"firma62");
    History history;history.record(sketches);auto edited=sketches;edited.extrusions.last().pattern.variations[0].increment=3;
    require(history.undo(edited)&&edited.extrusions.last().pattern.variations[0].increment==2,"Undo");
    require(history.redo(edited)&&edited.extrusions.last().pattern.variations[0].increment==3,"Redo");
    // Sottrazione su base fissa.
    DocumentState holes;auto plate=fixed;plate.primitive.origin[0]=plate.primitive.origin[1]=plate.primitive.origin[2]=0;
    plate.primitive.size[0]=70;plate.primitive.size[1]=20;plate.primitive.size[2]=5;
    auto drill=box;drill.primitive.kind=PrimitiveKind::Cylinder;drill.primitive.origin[0]=drill.primitive.origin[1]=10;
    drill.primitive.origin[2]=-1;drill.primitive.size[0]=1;drill.primitive.size[1]=7;
    auto cut=join;cut.operation=2;holes.extrusions={plate,drill,cut};regenerate(holes);
    auto hp=p;hp.pattern.count=3;hp.pattern.variations={parameter(hp,holes,"size0",1)};
    close(massProperties(*build(hp,holes).forgeBody).volume,7000-5*M_PI*(1+4+9));
    // Il conteggio del pattern interno allo schizzo resta un parametro della funzione.
    auto patterned=old;SketchPattern sketchPattern;sketchPattern.count=2;sketchPattern.spacing=12;sketchPattern.direction={1,0};
    sketchPattern.parametric=true;sketchPattern.dimensioned=true;
    require(patternSketchEntities(patterned.sketches[0],{{1,0}},sketchPattern).error.isEmpty(),"fixture schizzo ripetuto");
    regenerate(patterned);auto cp=sp;cp.pattern.count=3;cp.pattern.spacing=30;cp.pattern.refs={{2,1,{},{}}};
    cp.pattern.variations={parameter(cp,patterned,"/count",1)};
    close(massProperties(*build(cp,patterned).forgeBody).volume,1+9*M_PI*4*5);
    bad=cp;bad.pattern.variations[0].increment=.5;buildGeometry(bad,2,patterned.sketches,patterned.extrusions);
    require(!bad.forgeBody && !bad.error.isEmpty(),"conteggio frazionario accettato");
    // Quote cliccabili separate dal breve elenco dei parametri funzione; niente duplicati.
    PatternVariationEditor widget;widget.setContext(sp,old.sketches,old.extrusions);
    auto *combo=widget.findChild<QComboBox *>("patternVariationParameter");
    require(combo && combo->count()==1,"elenco non limitato alla distanza estrusione");
    require(widget.dimensions().size()==1,"quota profilo non offerta alla vista");
    int changes=0;widget.changed=[&]{++changes;};widget.selectParameter(widget.dimensions()[0]);
    widget.selectParameter(widget.dimensions()[0]);require(widget.variations().size()==1 && changes==1,"clic quota duplicato");
    auto *input=widget.findChild<QDoubleSpinBox *>("patternVariationIncrement0");require(input,"incremento mancante");input->setValue(2);
    require(changes==2 && widget.variations()[0].increment==2,"incremento UI");
    widget.findChild<QTableWidget *>("patternVariationTable")->setCurrentCell(0,0);
    widget.findChild<QPushButton *>("patternVariationRemove")->click();require(widget.variations().isEmpty(),"rimozione UI");
    auto body=sp;body.pattern.featureOnly=false;widget.setContext(body,old.sketches,old.extrusions);
    require(widget.dimensions().isEmpty() && combo->count()==0,"parametri corpo esposti");
    std::cout<<"Pattern variation scope, geometry, sketches, exclusions, validation, codec and Undo/Redo PASS\n";
}
