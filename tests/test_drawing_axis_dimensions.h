#pragma once
#include <QComboBox>
#include <QGraphicsSimpleTextItem>
namespace {
DrawingAttachment axisRef(const DrawingAxis &axis,DrawingPointRole role=DrawingPointRole::Curve,double fraction=0){DrawingAttachment r;r.axisId=axis.id;r.role=role;r.fraction=fraction;return r;}
void axisDimensionTests(){
 DrawingEditorModel m;m.snapshot.version={QUuid::createUuid(),{}};m.snapshot.bodies={body(1,makeBox(Frame3(),12,8,6))};m.choices={{1,"Blocco"}};
 DrawingSheet sheet;sheet.title="Quote sugli assi associati";sheet.drawing.model=m.snapshot.version;sheet.size={297,210};DrawingSheetEditor e(sheet,m);e.resize(1300,850);e.show();QApplication::processEvents();e.fitSheet();
 DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=4;ok(e.insertView(v,{v.id,"Vista superiore",{100,100},{1}}));ok(e.setHiddenLinesVisible(v.id,true));
 const auto &curves=e.curves(v.id);
 DrawingAxis median;median.viewId=v.id;median.name="Mediana";median.kind=DrawingAxisKind::BetweenLines;median.references={vertical(v,m,curves,0),vertical(v,m,curves,12)};ok(e.addAxis(median));
 DrawingAxis left;left.viewId=v.id;left.name="Asse sinistro";left.references={point(v,m,curves,{0,0}),point(v,m,curves,{0,8})};ok(e.addAxis(left));
 DrawingAxis diagonal;diagonal.viewId=v.id;diagonal.name="Diagonale";diagonal.references={point(v,m,curves,{0,0}),point(v,m,curves,{12,8})};ok(e.addAxis(diagonal));
 const auto make=[&](std::vector<DrawingAttachment> refs,DrawingMeasure type=DrawingMeasure::Distance,bool automatic=false){return drawingDimensionBetween(v,m.snapshot,e.curves(v.id),std::move(refs),type,{18,-5},automatic,drawingSheetAttachmentResolver(e.sheet(),v,m.snapshot,e.curves(v.id)));};
 const auto add=[&](DrawingDimension d,QPointF p,double expected){ok(e.addDimension(d,p));if(std::abs(e.dimensionResult(d.id).value-expected)>1e-6)std::cerr<<"misura="<<int(d.measure)<<" actual="<<e.dimensionResult(d.id).value<<" expected="<<expected<<"\n";near(e.dimensionResult(d.id).value,expected,"misura esatta della quota su asse");return d.id;};
 const auto corner=point(v,m,curves,{12,0});
 const auto d=make({corner,axisRef(median)});const auto pointAxis=add(d,{165,113},6);
 const auto axes=make({axisRef(left),axisRef(median)});const auto axisAxis=add(axes,{120,56},6);
 auto angular=make({axisRef(median),axisRef(diagonal)},DrawingMeasure::Angle);angular.angleSides=0;const auto angle=add(angular,{150,68},56.309932474);
 add(make({corner,axisRef(median)},DrawingMeasure::AxisDiameter),{160,120},12);
 add(make({corner,axisRef(median)},DrawingMeasure::AxisRadius),{160,127},6);
 add(make({axisRef(left,DrawingPointRole::Midpoint),axisRef(median,DrawingPointRole::Midpoint)},DrawingMeasure::HorizontalDistance),{115,74},6);
 add(make({axisRef(left,DrawingPointRole::End),axisRef(median,DrawingPointRole::Start)},DrawingMeasure::VerticalDistance),{83,83},8);
 add(make({axisRef(left,DrawingPointRole::Start),axisRef(median,DrawingPointRole::Nearest,.25)}),{70,40},std::sqrt(40.0));
 bool singleRejected=false;try{make({axisRef(median)},DrawingMeasure::Distance,true);}catch(const std::exception &){singleRejected=true;}need(singleRejected,"un asse da solo richiede un secondo riferimento senza dereferenziare curve fittizie");
 need(evaluateDrawingAttachments(d,v,m.snapshot,curves).status==DrawingDimensionStatus::Missing,"senza contesto del foglio nessuna misura sostitutiva");
 auto bad=median;bad.id=QUuid::createUuid();bad.references[0]=axisRef(left);need(!e.addAxis(bad).isEmpty(),"dipendenze fra assi rifiutate");
 QByteArray encoded=bytes(e);auto root=QJsonDocument::fromJson(encoded).object();need(root["schema"].toInt()==5&&root["drawing"].toObject()["schema"].toInt()==5,"schema 5 solo per agganci alle entità asse");
 DrawingSheet decoded;ok(decodeDrawingSheet(encoded,decoded));DrawingSheetEditor reopened(decoded,m);near(reopened.dimensionResult(pointAxis).value,6,"riapertura quota per UUID asse");
 auto downgraded=root;downgraded["schema"]=4;need(!decodeDrawingSheet(QJsonDocument(downgraded).toJson(),decoded).isEmpty(),"rifiuta downgrade del foglio con quote asse");
 auto geometry=root["drawing"].toObject();geometry["schema"]=4;downgraded=root;downgraded["drawing"]=geometry;need(!decodeDrawingSheet(QJsonDocument(downgraded).toJson(),decoded).isEmpty(),"rifiuta downgrade della geometria con quote asse");
 // Codec rifiuta UUID non validi, riferimenti a un’altra vista e dati topologici spuri.
 auto malformed=root;auto malformedDrawing=malformed["drawing"].toObject();auto malformedDimensions=malformedDrawing["dimensions"].toArray();auto malformedDimension=malformedDimensions[0].toObject();auto malformedRefs=malformedDimension["references"].toArray();auto malformedRef=malformedRefs[1].toObject();malformedRef["axisId"]="non-un-uuid";malformedRefs[1]=malformedRef;malformedDimension["references"]=malformedRefs;malformedDimensions[0]=malformedDimension;malformedDrawing["dimensions"]=malformedDimensions;malformed["drawing"]=malformedDrawing;need(!decodeDrawingSheet(QJsonDocument(malformed).toJson(),decoded).isEmpty(),"UUID asse non valido rifiutato");
 auto forged=e.sheet();auto foreign=v;foreign.id=QUuid::createUuid();forged.drawing.views.push_back(foreign);forged.views.append({foreign.id,"Altra vista",{230,150},{1}});auto foreignDimension=d;foreignDimension.id=QUuid::createUuid();foreignDimension.viewId=foreign.id;forged.drawing.dimensions.push_back(foreignDimension);forged.dimensions.append({foreignDimension.id,{230,180}});QByteArray rejected;need(!encodeDrawingSheet(forged,rejected).isEmpty(),"asse della vista madre non usato per una quota della vista figlia");
 auto dirtyAxisRef=d;dirtyAxisRef.id=QUuid::createUuid();dirtyAxisRef.references[1].curve.featureId=1;need(!e.addDimension(dirtyAxisRef,{170,140}).isEmpty(),"asse non combinato con una falsa sorgente topologica");
 const auto *cachedGeometry=e.curves(v.id)[0].curve.get();
 // Proprietà grafiche e visibilità non cambiano le misure e non perdono le quote.
 auto hidden=median;hidden.name="Asse rinominato";hidden.extension=150;hidden.visible=false;ok(e.updateAxis(hidden));near(e.dimensionResult(pointAxis).value,6,"grafica non cambia punto asse");near(e.dimensionResult(axisAxis).value,6,"asse nascosto mantiene misura");need(e.curves(v.id)[0].curve.get()==cachedGeometry,"proprietà asse riusano curve esatte");
 auto relocated=hidden;relocated.kind=DrawingAxisKind::ThroughPoints;relocated.references=left.references;ok(e.updateAxis(relocated));near(e.dimensionResult(pointAxis).value,12,"ridefinizione asse aggiorna quote");near(e.dimensionResult(axisAxis).value,0,"ridefinizione aggiorna anche asse asse");
 ok(e.undo());near(e.dimensionResult(pointAxis).value,6,"Undo ridefinizione ripristina geometria e quote");ok(e.redo());near(e.dimensionResult(pointAxis).value,12,"Redo ridefinizione conserva UUID");ok(e.undo());ok(e.updateAxis(median));need(e.curves(v.id)[0].curve.get()==cachedGeometry,"ridefinizione e Undo riusano geometria vista");
 ok(e.removeAxis(median.id));need(e.dimensionResult(pointAxis).status==DrawingDimensionStatus::Missing&&e.dimensionResult(axisAxis).status==DrawingDimensionStatus::Missing,"eliminazione asse lascia quote da verificare senza riassociazione");
 auto missing=bytes(e);DrawingSheet orphan;ok(decodeDrawingSheet(missing,orphan));DrawingSheetEditor orphanEditor(orphan,m);need(orphanEditor.dimensionResult(pointAxis).status==DrawingDimensionStatus::Missing,"quota mancante resta tale dopo riapertura");ok(e.undo());near(e.dimensionResult(pointAxis).value,6,"Undo eliminazione ripristina asse e misura");
 auto moved=v;moved.scale=2;ok(e.updateView(moved,e.sheet().views[0]));ok(e.moveView(v.id,{95,95}));near(e.dimensionResult(pointAxis).value,6,"scala e posizione non cambiano misura");
 auto changed=m;changed.snapshot.bodies[0]=body(1,makeBox(Frame3(),16,8,6));ok(e.updateModel(changed));near(e.dimensionResult(pointAxis).value,8,"quota mediana segue modifica del modello");near(e.dimensionResult(axisAxis).value,8,"asse asse segue modello");need(e.dimensionResult(angle).status==DrawingDimensionStatus::Ready,"angolo resta associato");
 const auto output=qEnvironmentVariable("FORGECAD_AXIS_DIM_OUTPUT");if(!output.isEmpty()){need(QDir().mkpath(output),"cartella QA quote assi");DrawingPdfReport report;ok(e.exportPdf(output+"/quote-assi.pdf",report));need(report.warnings.isEmpty(),"PDF valido senza avvisi");e.fitSheet();need(e.grab().save(output+"/editor.png"),"anteprima quote sugli assi");}
 auto absent=changed;absent.snapshot.bodies.clear();ok(e.updateModel(absent));need(e.dimensionResult(pointAxis).status==DrawingDimensionStatus::Missing,"perdita sorgente non usa coordinate archiviate");
 if(!output.isEmpty()){DrawingPdfReport report;ok(orphanEditor.exportPdf(output+"/mancanti.pdf",report));need(!report.warnings.isEmpty(),"PDF segnala riferimenti mancanti");need(!e.exportPdf(output+"/senza-modello.pdf",report).isEmpty(),"PDF rifiutato senza alcun modello disponibile");}
 // Caso in cui venga eliminato l'ultimo asse: il formato deve conservare la quota orfana.
 auto isolated=sheet;isolated.drawing.views={v};isolated.views={{v.id,"Vista",{100,100},{1}}};isolated.axes={median};isolated.drawing.dimensions={d};isolated.dimensions={{d.id,{165,113}}};isolated.axes.clear();QByteArray orphanBytes;ok(encodeDrawingSheet(isolated,orphanBytes));ok(decodeDrawingSheet(orphanBytes,decoded));need(decoded.axes.empty()&&decoded.drawing.dimensions[0].references[1].axisId==median.id,"ultimo asse eliminato non elimina o altera riferimento");
 // Interazione reale dello strumento Quota: punto geometrico, asse come retta, posizionamento.
 DrawingSheetEditor gui(sheet,m);gui.resize(1300,850);gui.show();QApplication::processEvents();gui.fitSheet();ok(gui.insertView(v,{v.id,"Snap asse",{100,100},{1}}));ok(gui.setHiddenLinesVisible(v.id,true));ok(gui.addAxis(median));
 auto *tool=gui.findChild<QAction *>("drawingSmartDimensionAction");tool->setChecked(true);mouse(gui,{148,100},true);mouse(gui,{124,92},true,Qt::ShiftModifier);mouse(gui,{165,116},true);need(gui.sheet().drawing.dimensions.size()==1,"quota punto asse inserita con mouse");
 const auto actual=gui.sheet().drawing.dimensions[0];need(actual.references[1].axisId==median.id,"snap mouse conserva UUID asse");near(gui.dimensionResult(actual.id).value,6,"misura mouse punto asse");
 ok(gui.addAxis(diagonal));
 // Lo strumento automatico sceglie angolo per due rette non parallele.
 mouse(gui,{124,92},true,Qt::ShiftModifier);mouse(gui,{136,76},true,Qt::ShiftModifier);mouse(gui,{165,63},true);need(gui.sheet().drawing.dimensions.size()==2,"quota asse asse inserita con mouse");
 const auto &mouseAngle=gui.sheet().drawing.dimensions.back();need(mouseAngle.measure==DrawingMeasure::Angle&&mouseAngle.references[0].axisId==median.id&&mouseAngle.references[1].axisId==diagonal.id,"scelta automatica dell’angolo tra assi");
 mouse(gui,{148,100},true);mouse(gui,{124,92},true,Qt::AltModifier);mouse(gui,{174,130},true);need(gui.sheet().drawing.dimensions.size()==3,"punto su asse con Alt inserito con mouse");
 const auto &mouseNearest=gui.sheet().drawing.dimensions.back();need(mouseNearest.references[1].axisId==median.id&&mouseNearest.references[1].role==DrawingPointRole::Nearest,"Alt memorizza parametro sull’asse");
 need(std::abs(mouseNearest.references[1].fraction-.25)<.02,"Alt localizzato vicino al quarto dell’asse entro la risoluzione pixel");near(gui.dimensionResult(mouseNearest.id).value,std::hypot(6,8*mouseNearest.references[1].fraction),"punto parametrico su asse misurato dal mouse");
 // Una scelta di snap non sopravvive alla rimozione dell'asse prima del ridisegno differito.
 mouse(gui,{124,92},false,Qt::ShiftModifier);need(gui.removeAxis(median.id).isEmpty(),"elimina asse durante hover quota");QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(gui.canvas(),&tab);QApplication::processEvents();need(gui.dimensionResult(actual.id).status==DrawingDimensionStatus::Missing,"snap obsoleto cancellato senza arresto e quota marcata mancante");
 gui.setReadOnly(true);need(!gui.addDimension(d,{160,130}).isEmpty()&&!gui.updateAxis(median).isEmpty(),"quote sugli assi rispettano sola lettura");
 std::cout<<"PASS quote assi: punto/retta, asse/asse, angoli, raggi/diametri rispetto asse, snap, ridefinizione, proprietà, scala, modello, codec 5, orfani, Undo/Redo e sola lettura\n";
}
}
