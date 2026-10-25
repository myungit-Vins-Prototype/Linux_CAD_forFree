#pragma once
#include "cad_drawing_numbering.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QJsonArray>
#include <limits>
namespace {
QGraphicsItem *numberBadge(DrawingSheetEditor &editor,const QUuid &id){for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="dimensionBadge"&&item->data(3).toUuid()==id)return item;throw std::runtime_error("badge quota assente");}
void drawingDimensionNumberingExclusionTests(DrawingSheetEditor &e,const QVector<QUuid> &ids){
 const auto initial=encoded(e);const auto qa=QStringLiteral("/tmp/forgecad-numbering-exclusions-qa");QDir().mkpath(qa);
 const auto absent=[&](const QUuid &id){for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="dimensionBadge"&&item->data(3).toUuid()==id)return false;return true;};
 const auto assertGaps=[&]{QApplication::processEvents();require(items(e,"dimensionBadge")==2&&absent(ids[1])&&absent(ids[2])&&numberBadge(e,ids[0])->data(7).toInt()==1&&numberBadge(e,ids[3])->data(7).toInt()==4,"esclusioni manuale e parentesi lasciano1/4 senza rinumerazione");};
 auto presentation=e.sheet().dimensions[1].presentation;presentation.numberingExcluded=true;ok(e.updateDimension(e.sheet().drawing.dimensions[1],presentation));QApplication::processEvents();require(absent(ids[1])&&items(e,"dimensionBadge")==3&&numberBadge(e,ids[2])->data(7).toInt()==3&&numberBadge(e,ids[3])->data(7).toInt()==4,"nascondere2 lascia1/3/4");near(e.dimensionResult(ids[1]).value,6,"esclusione non altera nominale");
 presentation=e.sheet().dimensions[2].presentation;presentation.prefix=" (";presentation.suffix=") ";ok(e.updateDimension(e.sheet().drawing.dimensions[2],presentation));QApplication::processEvents();require(numberBadge(e,ids[2])->data(7).toInt()==3,"parentesi incluse per default");auto setting=e.sheet().dimensionNumbering;setting.excludeParenthesized=true;ok(e.setDimensionNumbering(setting));assertGaps();
 const auto excluded=encoded(e);DrawingSheet decoded;ok(decodeDrawingSheet(excluded,decoded));require(decoded.dimensionNumbering.excludeParenthesized&&decoded.dimensions[1].presentation.numberingExcluded&&drawingDimensionNumbers(decoded).value(ids[3])==4&&QJsonDocument::fromJson(excluded).object()["schema"].toInt()==22,"codec22 mantiene esclusioni e numeri riservati");
 e.grab().save(qa+"/esclusioni.png");DrawingPdfReport report;ok(e.exportPdf(qa+"/esclusioni.pdf",report));QFile pdf(qa+"/esclusioni.pdf");require(pdf.open(QIODevice::ReadOnly)&&!pdf.readAll().contains("/Subtype /Image"),"esclusioni nel PDF vettoriale");
 const auto json=QJsonDocument::fromJson(excluded).object();const auto reject=[&](QJsonObject bad){auto target=decoded;require(!decodeDrawingSheet(QJsonDocument(bad).toJson(),target).isEmpty(),"codec rifiuta metadati esclusioni malformati");QByteArray actual;ok(encodeDrawingSheet(target,actual));require(actual==excluded,"decode esclusioni atomico");};
 auto bad=json;bad["schema"]=20;reject(bad);bad=json;auto n=bad["dimensionNumbering"].toObject();n["excludeParenthesized"]=1;bad["dimensionNumbering"]=n;reject(bad);bad=json;auto dims=bad["dimensions"].toArray();auto dim=dims[1].toObject();auto pres=dim["presentation"].toObject();pres["numberingExcluded"]="true";dim["presentation"]=pres;dims[1]=dim;bad["dimensions"]=dims;reject(bad);
 // La sola esclusione individuale deve essere leggibile anche a numerazione disattivata.
 auto manualOnly=decoded;manualOnly.dimensionNumbering={};manualOnly.dimensions[2].presentation.prefix.clear();manualOnly.dimensions[2].presentation.suffix.clear();QByteArray manualBytes;ok(encodeDrawingSheet(manualOnly,manualBytes));require(!QJsonDocument::fromJson(manualBytes).object().contains("dimensionNumbering"),"flag individuale senza impostazioni globali");DrawingSheet manualDecoded;ok(decodeDrawingSheet(manualBytes,manualDecoded));require(manualDecoded.dimensions[1].presentation.numberingExcluded,"schema21 standalone conserva flag individuale");
 setting.excludeParenthesized=false;ok(e.setDimensionNumbering(setting));QApplication::processEvents();require(numberBadge(e,ids[2])->data(7).toInt()==3&&absent(ids[1]),"disattivazione filtro ripristina3 e rispetta esclusione manuale");
 QTimer::singleShot(0,[&]{auto *dialog=e.findChild<QDialog *>("drawingDimensionNumberingDialog");require(dialog,"dialogo esclusioni");dialog->findChild<QCheckBox *>("drawingNumberingExcludeParenthesized")->setChecked(true);dialog->grab().save(qa+"/opzioni.png");dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});e.findChild<QAction *>("drawingDimensionNumberingAction")->trigger();QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);assertGaps();
 select(e,ids[1]);e.findChild<QAction *>("drawingToggleDimensionNumberAction")->trigger();QApplication::processEvents();require(numberBadge(e,ids[1])->data(7).toInt()==2&&absent(ids[2])&&numberBadge(e,ids[3])->data(7).toInt()==4,"ripristino manuale recupera2 con filtro ancora attivo");ok(e.undo());assertGaps();
 // Selezione multipla: operazione atomica e numeri riservati.
 e.canvas()->scene()->clearSelection();dimensionTextItem(e,ids[0])->setSelected(true);dimensionTextItem(e,ids[3])->setSelected(true);e.findChild<QAction *>("drawingToggleDimensionNumberAction")->trigger();QApplication::processEvents();require(items(e,"dimensionBadge")==0,"esclusione multipla dei due badge restanti");ok(e.undo());assertGaps();
 select(e,ids[0]);const auto beforeProperties=encoded(e);QTimer::singleShot(0,[&]{auto *dialog=e.findChild<QDialog *>("drawingDimensionPropertiesDialog");require(dialog,"proprieta' con controllo esclusione");dialog->findChild<QCheckBox *>("drawingDimensionNumberExcluded")->setChecked(true);dialog->grab().save(qa+"/proprieta.png");dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});e.findChild<QAction *>("drawingEditDimensionAction")->trigger();QApplication::processEvents();require(absent(ids[0])&&numberBadge(e,ids[3])->data(7).toInt()==4,"proprieta' nascondono1 senza rinumerare4");ok(e.undo());require(encoded(e)==beforeProperties,"Undo singolo per proprieta' ed esclusione");assertGaps();
 e.setReadOnly(true);require(!e.findChild<QAction *>("drawingToggleDimensionNumberAction")->isEnabled(),"azione esclusione protetta in sola lettura");e.setReadOnly(false);
 for(int i=0;i<30&&encoded(e)!=initial&&e.canUndo();++i){ok(e.undo());}
 require(encoded(e)==initial,"Undo ripristina esattamente stato prima delle esclusioni");QApplication::processEvents();require(items(e,"dimensionBadge")==4,"Undo ripristina tutti i badge");
 DrawingDimensionPresentation parentheses;parentheses.prefix=" (REF ";parentheses.suffix=" ) ";require(drawingDimensionParenthesized(parentheses),"parentesi esterne con testo e spazi");parentheses.suffix.clear();require(!drawingDimensionParenthesized(parentheses),"parentesi incompleta non esclusa");parentheses.prefix="";parentheses.suffix=")";require(!drawingDimensionParenthesized(parentheses),"sola parentesi chiusa non esclusa");
 std::cout<<"PASS esclusioni: numeri1/3/4 invariati, filtro parentesi facoltativo, selezione multipla/proprieta', codec21/20/standalone, Undo, readonly e PDF\n";
}
void nearbyNumberingTests(){
 // Ricostruzione in mm della disposizione della foto: numeri originali 1..11.
 // Le viste non intervengono: contano soltanto i testi sul foglio.
 DrawingSheet sheet;sheet.dimensionNumbering.enabled=true;sheet.dimensionNumbering.persistent=true;
 const QVector<QPointF> positions{{65,155},{66,15},{97,23},{101,140},{104,37},{145,20},{237,46},{271,13},{281,13},{296,50},{18,82}};
 const QVector<int> expected{1,4,11,2,3,5,6,7,10,8,9};
 QVector<QUuid> ids;for(int i=0;i<positions.size();++i){ids.append(QUuid(QStringLiteral("00000000-0000-0000-0000-%1").arg(i+1,12,16,QChar('0'))));sheet.dimensions.append({ids.back(),positions[i]});}
 sheet.dimensionNumbering.firstDimension=ids.front();
 const QVector<QPair<int,int>> directions{{0,1},{0,3},{2,1},{2,3},{1,0},{1,2},{3,0},{3,2}};
 for(const auto &direction:directions){
  auto trial=sheet;auto &n=trial.dimensionNumbering;n.primaryDirection=direction.first;n.secondaryDirection=direction.second;
  for(int i=0;i<positions.size();++i){const double p=-positions[i].y(),s=positions[i].x();QPointF transformed;
   if(n.primaryDirection%2){transformed.setY(p*(n.primaryDirection>=2?-1:1));transformed.setX(s*(n.secondaryDirection>=2?-1:1));}
   else{transformed.setX(p*(n.primaryDirection>=2?-1:1));transformed.setY(s*(n.secondaryDirection>=2?-1:1));}
   trial.dimensions[i].position=transformed+QPointF(300,300);
  }
  const auto order=drawingNumberingOrder(trial);require(order.size()==expected.size(),"vicinanza: ogni quota compare una volta");
  for(int i=0;i<expected.size();++i)require(order[i]==ids[expected[i]-1],"vicinanza: percorso foto1/4/11/2/3/5/6/7/10/8/9 negli otto versi");
  std::reverse(trial.dimensions.begin(),trial.dimensions.end());require(drawingNumberingOrder(trial)==order,"vicinanza indipendente dall'ordine d'inserimento");
  ok(renumberDrawingDimensions(trial));for(int i=0;i<expected.size();++i)require(drawingDimensionNumbers(trial).value(ids[expected[i]-1])==i+1,"numeri assegnati seguono il percorso vicino");
  for(auto &d:trial.dimensions)d.position=d.position*2+QPointF(10,-20);require(drawingNumberingOrder(trial)==order,"vicinanza indipendente da traslazione e scala uniforme");
 }
 const QVector<QPointF> grid{{80,60},{160,60},{80,145},{160,145}};
 const QVector<QVector<int>> gridExpected{{0,1,2,3},{2,3,0,1},{1,0,3,2},{3,2,1,0},{0,2,1,3},{1,3,0,2},{2,0,3,1},{3,1,2,0}};
 for(int i=0;i<directions.size();++i){auto trial=sheet;trial.dimensions.resize(4);trial.dimensionNumbering.primaryDirection=directions[i].first;trial.dimensionNumbering.secondaryDirection=directions[i].second;trial.dimensionNumbering.firstDimension=ids[gridExpected[i].front()];
  for(int j=0;j<grid.size();++j)trial.dimensions[j].position=grid[j];const auto order=drawingNumberingOrder(trial);
  for(int j=0;j<4;++j)require(order[j]==ids[gridExpected[i][j]],"griglia guidata conserva la priorita' dei due versi");
 }
 auto trial=sheet;trial.dimensionNumbering.primaryDirection=3;trial.dimensionNumbering.secondaryDirection=0;
 trial.dimensions[4].presentation.numberingExcluded=true;ok(renumberDrawingDimensions(trial));require(!drawingDimensionNumbers(trial).contains(ids[4])&&drawingDimensionNumbers(trial).size()==10,"vicinanza rispetta le esclusioni");
 trial=sheet;trial.dimensionNumbering.primaryDirection=3;trial.dimensionNumbering.secondaryDirection=0;trial.dimensionNumbering.firstDimension=ids[10];
 const auto internal=drawingNumberingOrder(trial);QSet<QUuid> unique;for(const auto &id:internal)unique.insert(id);require(internal.front()==ids[10]&&unique.size()==11,"prima interna conserva tutte le quote senza duplicati");
 std::cout<<"PASS vicinanza: percorso foto1/4/11/2/3/5/6/7/10/8/9, otto versi, inserimento, scala, esclusioni e prima interna\n";
}
DrawingSheet viewNumberingFixture(){
 // Coordinate e appartenenze reali di Controllo numero quote.drw, UUID di test.
 DrawingSheet sheet;sheet.dimensionNumbering.enabled=true;sheet.dimensionNumbering.persistent=true;
 const QVector<QPointF> origins{{66.15486663839634,82.98202772868098},{143.8881999717297,35.01536106201431},{249.38664306245064,116.72178623806481}};
 for(int i=0;i<origins.size();++i){DrawingViewLayout view;view.viewId=QUuid(QStringLiteral("20000000-0000-0000-0000-%1").arg(i+1,12,16,QChar('0')));view.position=origins[i];view.title=QStringLiteral("Vista %1").arg(i+1);sheet.views.append(view);}
 const QVector<QPointF> positions{{112.82486680175907,48.140361062014335},
  {59.18221038839634,99.75887782891607},
  {24.155538067657467,56.107027728680976},
  {137.5991374717297,25.57775803708567},
  {23.70840482555839,25.03457731977858},
  {42.925878079224056,14.586558856494783},
  {115.28670609192618,30.31392534871302},
  {90.53360537429752,25.01689242336513},
  {92.78220618903121,85.93400941366592},
  {23.67831032685916,90.87847518813953},
  {64.34098498115596,22.059767600782294}};
 const QVector<int> belongs{1,0,0,1,0,0,1,0,0,0,0};
 for(int i=0;i<positions.size();++i){DrawingDimension definition;definition.id=QUuid(QStringLiteral("10000000-0000-0000-0000-%1").arg(i+1,12,16,QChar('0')));definition.viewId=sheet.views[belongs[i]].viewId;sheet.drawing.dimensions.push_back(definition);sheet.dimensions.append({definition.id,positions[i]});}
 sheet.dimensionNumbering.firstDimension=sheet.dimensions[1].dimensionId;sheet.dimensionNumbering.primaryDirection=3;sheet.dimensionNumbering.secondaryDirection=0;return sheet;
}
void viewNumberingTests(){
 const auto sample=viewNumberingFixture();const QVector<QPair<int,int>> directions{{0,1},{0,3},{2,1},{2,3},{1,0},{1,2},{3,0},{3,2}};
 const auto check=[&](const DrawingSheet &sheet){
  QHash<QUuid,QUuid> views;QHash<QUuid,int> counts;for(const auto &d:sheet.drawing.dimensions)views.insert(d.id,d.viewId);
  for(const auto &d:sheet.dimensions)if(!drawingDimensionNumberExcluded(d,sheet.dimensionNumbering))++counts[views.value(d.dimensionId)];
  const auto order=drawingNumberingOrder(sheet);QSet<QUuid> closed,unique;QUuid current;bool started=false;
  for(const auto &id:order){const auto view=views.value(id);if(!started||view!=current){if(started)closed.insert(current);require(!closed.contains(view),"vista: non tornare a una vista dopo essere passati a un'altra");current=view;started=true;}require(!unique.contains(id),"vista: quote senza duplicati");unique.insert(id);}
  int total=0;for(int count:counts)total+=count;require(order.size()==total,"vista: nessuna quota omessa");
  const auto first=sheet.dimensionNumbering.firstDimension;require(order.front()==first,"vista: prima quota selezionata rispettata");const auto firstView=views.value(first);
  for(int i=0;i<counts.value(firstView);++i)require(views.value(order[i])==firstView,"vista: completa tutta la vista iniziale prima delle altre");
 };
 for(const auto &direction:directions)for(int first:{1,2,0}){auto trial=sample;trial.dimensionNumbering.primaryDirection=direction.first;trial.dimensionNumbering.secondaryDirection=direction.second;trial.dimensionNumbering.firstDimension=trial.dimensions[first].dimensionId;check(trial);
  const auto order=drawingNumberingOrder(trial);std::reverse(trial.dimensions.begin(),trial.dimensions.end());std::reverse(trial.drawing.dimensions.begin(),trial.drawing.dimensions.end());std::reverse(trial.views.begin(),trial.views.end());require(drawingNumberingOrder(trial)==order,"vista: ordine indipendente dai vettori");
  ok(renumberDrawingDimensions(trial));const auto assigned=drawingDimensionNumbers(trial);for(int i=0;i<order.size();++i)require(assigned.value(order[i])==i+1,"vista: numeri seguono gruppi completi");
 }
 auto trial=sample;trial.dimensions[2].presentation.numberingExcluded=true;check(trial);
 trial=sample;for(auto &d:trial.dimensions)d.position={20,20};check(trial);
 trial=sample;trial.views.removeFirst();check(trial); // Vista senza layout: appartenenza conservata.
 std::cout<<"PASS una vista alla volta: dati reali Controllo numero quote,8+3 quote, otto versi, prima interna/altra vista, vettori invertiti, esclusioni, sovrapposizioni e layout mancante\n";
}
void nearbyNumberingGuiTests(const DrawingEditorModel &model,const DrawingSheet &base){
 auto sheet=base;const auto prototype=sheet.drawing.dimensions.front();sheet.dimensions.clear();sheet.drawing.dimensions.clear();sheet.dimensionNumbering={};sheet.size={420,297};
 const QVector<QPointF> positions{{65,155},{66,15},{97,23},{101,140},{104,37},{145,20},{237,46},{271,13},{281,13},{296,50},{18,82}};
 const QVector<int> expected{1,4,11,2,3,5,6,7,10,8,9};QVector<QUuid> ids;
 for(const auto &position:positions){auto dimension=prototype;dimension.id=QUuid::createUuid();sheet.drawing.dimensions.push_back(dimension);sheet.dimensions.append({dimension.id,position});ids.append(dimension.id);}
 DrawingSheetEditor editor(sheet,model);editor.resize(1450,900);editor.show();editor.fitSheet();QApplication::processEvents();editor.markSaved();const auto initial=encoded(editor);
 editor.findChild<QAction *>("drawingDimensionNumberingAction")->trigger();auto *dialog=editor.findChild<QDialog *>("drawingDimensionNumberingDialog");require(dialog,"pannello per quote vicine");
 click(editor,dimensionTextItem(editor,ids.front())->sceneBoundingRect().center());
 const auto arrow=[&](int direction){for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="numberingArrow"&&item->data(8).toInt()==direction){click(editor,item->sceneBoundingRect().center());return;}throw std::runtime_error("freccia per vicinanza assente");};
 arrow(3);arrow(0);auto *buttons=dialog->findChild<QDialogButtonBox *>();require(buttons->button(QDialogButtonBox::Ok)->isEnabled(),"alto poi destra confermabile");
 QHash<QUuid,int> preview;for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="numberingPreview")preview.insert(item->data(3).toUuid(),item->data(7).toInt());
 for(int i=0;i<expected.size();++i)require(preview.value(ids[expected[i]-1])==i+1,"anteprima reale segue la sequenza indicata dall'utente");
 require(encoded(editor)==initial,"anteprima vicinanza non modifica il foglio");
 const auto qa=QStringLiteral("/tmp/forgecad-numbering-proximity-qa");QDir().mkpath(qa);editor.grab().save(qa+"/anteprima.png");dialog->grab().save(qa+"/pannello.png");
 buttons->button(QDialogButtonBox::Ok)->click();QApplication::processEvents();require(drawingDimensionNumbers(editor.sheet())==preview,"conferma conserva esattamente l'anteprima");
 for(int i=0;i<expected.size();++i)require(numberBadge(editor,ids[expected[i]-1])->data(7).toInt()==i+1,"badge confermati seguono il percorso vicino");
 const auto assigned=encoded(editor);DrawingSheet restored;ok(decodeDrawingSheet(assigned,restored));require(drawingDimensionNumbers(restored)==preview,"salvataggio e riapertura mantengono il percorso");
 DrawingPdfReport report;ok(editor.exportPdf(qa+"/vicinanza.pdf",report));editor.grab().save(qa+"/confermata.png");
 ok(editor.undo());require(encoded(editor)==initial,"Undo unico dell'assegnazione per vicinanza");ok(editor.redo());require(encoded(editor)==assigned,"Redo ripristina il percorso confermato");
 editor.findChild<QAction *>("drawingDimensionNumberingAction")->trigger();dialog=editor.findChild<QDialog *>("drawingDimensionNumberingDialog");require(dialog,"riapertura pannello vicinanza");dialog->findChild<QCheckBox *>("drawingNumberingReassign")->setChecked(true);dialog->reject();QApplication::processEvents();require(encoded(editor)==assigned,"Annulla conserva il percorso precedente");
 QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
 std::cout<<"PASS vicinanza GUI: clic alto/destra, anteprima11 quote, conferma, codec, PDF, Undo/Redo e Annulla\n";
}
void viewNumberingGuiTests(const DrawingEditorModel &model,const DrawingSheet &base){
 const auto sample=viewNumberingFixture();auto sheet=base;sheet.dimensions=sample.dimensions;sheet.dimensionNumbering={};sheet.views.clear();sheet.drawing.views.clear();sheet.drawing.dimensions.clear();
 for(const auto &v:sample.views){auto layout=base.views.front();layout.viewId=v.viewId;layout.position=v.position;layout.title=v.title;sheet.views.append(layout);auto definition=base.drawing.views.front();definition.id=v.viewId;sheet.drawing.views.push_back(definition);}
 for(const auto &d:sample.drawing.dimensions){auto definition=base.drawing.dimensions.front();definition.id=d.id;definition.viewId=d.viewId;sheet.drawing.dimensions.push_back(definition);}
 DrawingSheetEditor editor(sheet,model);editor.resize(1450,900);editor.show();editor.fitSheet();QApplication::processEvents();const auto initial=encoded(editor);
 editor.findChild<QAction *>("drawingDimensionNumberingAction")->trigger();auto *dialog=editor.findChild<QDialog *>("drawingDimensionNumberingDialog");require(dialog,"pannello numerazione per vista");
 click(editor,dimensionTextItem(editor,sample.dimensionNumbering.firstDimension)->sceneBoundingRect().center());
 const auto arrow=[&](int direction){for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="numberingArrow"&&item->data(8).toInt()==direction){click(editor,item->sceneBoundingRect().center());return;}throw std::runtime_error("freccia per vista assente");};
 arrow(3);arrow(0);auto *buttons=dialog->findChild<QDialogButtonBox *>();require(buttons->button(QDialogButtonBox::Ok)->isEnabled(),"numerazione per vista confermabile");
 QHash<QUuid,int> preview;for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="numberingPreview")preview.insert(item->data(3).toUuid(),item->data(7).toInt());
 require(preview.size()==11,"anteprima per vista comprende tutte le quote");for(const auto &d:sheet.drawing.dimensions){const auto number=preview.value(d.id);require(d.viewId==sample.views[0].viewId?(number>=1&&number<=8):(number>=9&&number<=11),"anteprima completa Vista1 prima della sezione");}
 require(encoded(editor)==initial,"anteprima per vista non modifica il documento");
 const auto qa=QStringLiteral("/tmp/forgecad-numbering-views-qa");QDir().mkpath(qa);editor.grab().save(qa+"/anteprima.png");dialog->grab().save(qa+"/pannello.png");
 buttons->button(QDialogButtonBox::Ok)->click();QApplication::processEvents();require(drawingDimensionNumbers(editor.sheet())==preview,"conferma per vista uguale all'anteprima");
 const auto assigned=encoded(editor);DrawingSheet restored;ok(decodeDrawingSheet(assigned,restored));require(drawingDimensionNumbers(restored)==preview,"codec conserva i blocchi per vista");
 DrawingPdfReport report;ok(editor.exportPdf(qa+"/viste.pdf",report));ok(editor.undo());require(encoded(editor)==initial,"Undo unico numerazione per vista");ok(editor.redo());require(encoded(editor)==assigned,"Redo numerazione per vista");
 QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
 std::cout<<"PASS GUI una vista alla volta: clic alto/destra, anteprima Vista1=1..8 e sezione=9..11, conferma, codec, PDF, Undo/Redo\n";
}
void persistentNumberingTests(DrawingSheetEditor &e,const QVector<QUuid> &ids){
 auto source=e.sheet();source.dimensionNumbering.persistent=true;source.dimensionNumbering.assignments.clear();
 const QVector<QVector<int>> expected{{0,1,2,3},{2,3,0,1},{1,0,3,2},{3,2,1,0},{0,2,1,3},{1,3,0,2},{2,0,3,1},{3,1,2,0}};
 const QVector<QPair<int,int>> directions{{0,1},{0,3},{2,1},{2,3},{1,0},{1,2},{3,0},{3,2}};
 for(int i=0;i<8;++i){auto trial=source;trial.dimensionNumbering.primaryDirection=directions[i].first;trial.dimensionNumbering.secondaryDirection=directions[i].second;const auto order=drawingNumberingOrder(trial);for(int j=0;j<4;++j)require(order[j]==ids[expected[i][j]],"otto scansioni rispettano priorita e segni");}
 auto trial=source;trial.dimensionNumbering.firstDimension=ids[1];ok(renumberDrawingDimensions(trial));require(trial.dimensionNumbering.assignments.value(ids[1])==1&&trial.dimensionNumbering.assignments.value(ids[0])==4,"prima quota ruota tutta la sequenza");
 trial=source;trial.dimensions[1].presentation.prefix="REF misura";trial.dimensions[2].presentation.prefix="(";trial.dimensions[2].presentation.suffix=")";trial.dimensionNumbering.excludePatterns={QStringLiteral("ref*")};trial.dimensionNumbering.excludeParenthesized=true;ok(renumberDrawingDimensions(trial));require(trial.dimensionNumbering.assignments.size()==2&&drawingDimensionNumbers(trial).value(ids[3])==2,"filtri iniziali non consumano numeri");
 auto moved=trial;moved.dimensions[3].position={0,0};require(drawingDimensionNumbers(moved)==drawingDimensionNumbers(trial),"persistenza indipendente dal movimento");
 ok(changeDrawingDimensionNumber(trial,ids[0],2,true));require(drawingDimensionNumbers(trial).value(ids[0])==2&&drawingDimensionNumbers(trial).value(ids[3])==1,"scambio esplicito atomico");
 const auto assigned=trial.dimensionNumbering.assignments;require(!changeDrawingDimensionNumber(trial,ids[0],1,false).isEmpty()&&trial.dimensionNumbering.assignments==assigned,"conflitto senza scambio non modifica");
 trial.dimensions.removeLast();require(!changeDrawingDimensionNumber(trial,ids[0],1,true).isEmpty(),"numero eliminato resta riservato");
 auto newcomer=trial.dimensions.front();newcomer.dimensionId=QUuid::createUuid();trial.dimensions.append(newcomer);ok(extendDrawingNumbering(trial));require(trial.dimensionNumbering.assignments.value(newcomer.dimensionId)==3,"nuova quota non riutilizza numero eliminato");
 trial=source;ok(renumberDrawingDimensions(trial));QByteArray bytes;ok(encodeDrawingSheet(trial,bytes));DrawingSheet restored;ok(decodeDrawingSheet(bytes,restored));require(restored.dimensionNumbering.assignments==trial.dimensionNumbering.assignments,"codec22 conserva associazioni");
 auto json=QJsonDocument::fromJson(bytes).object();auto metadata=json["numberingAssignments"].toObject();auto assignments=metadata["assignments"].toArray();auto duplicate=assignments[1].toObject();duplicate["number"]=assignments[0].toObject()["number"];assignments[1]=duplicate;metadata["assignments"]=assignments;json["numberingAssignments"]=metadata;require(!decodeDrawingSheet(QJsonDocument(json).toJson(),restored).isEmpty()&&restored.dimensionNumbering.assignments==trial.dimensionNumbering.assignments,"codec rifiuta numeri duplicati atomicamente");
 const auto before=encoded(e);e.findChild<QAction *>("drawingDimensionNumberingAction")->trigger();auto *dialog=e.findChild<QDialog *>("drawingDimensionNumberingDialog");require(dialog&&!dialog->isModal(),"pannello non modale");dialog->findChild<QCheckBox *>("drawingNumberingReassign")->setChecked(true);
 auto *buttons=dialog->findChild<QDialogButtonBox *>();require(!buttons->button(QDialogButtonBox::Ok)->isEnabled(),"conferma richiede prima quota e frecce");click(e,dimensionTextItem(e,ids[1])->sceneBoundingRect().center());
 const auto arrow=[&](int direction){for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="numberingArrow"&&item->data(8).toInt()==direction){const auto p=item->sceneBoundingRect().center();click(e,p);return;}throw std::runtime_error("freccia numerazione assente");};
 arrow(1);require(items(e,"numberingArrow")==2,"secondo verso solo ortogonale");arrow(2);require(buttons->button(QDialogButtonBox::Ok)->isEnabled()&&encoded(e)==before&&items(e,"numberingPreview")==4,"anteprima reale senza modifiche");
 QDir().mkpath("/tmp/forgecad-numbering-qa");e.grab().save("/tmp/forgecad-numbering-qa/guidata.png");dialog->grab().save("/tmp/forgecad-numbering-qa/pannello-guidato.png");buttons->button(QDialogButtonBox::Ok)->click();QApplication::processEvents();require(e.sheet().dimensionNumbering.persistent&&numberBadge(e,ids[1])->data(7).toInt()==1,"conferma guidata salva numerazione");ok(e.undo());require(encoded(e)==before,"Undo singolo del flusso guidato");QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
 std::cout<<"PASS badge persistenti: otto scansioni, rotazione, filtri, scambio, riserve, codec22, frecce e Undo\n";
}
void drawingDimensionNumberingTests(){
 viewNumberingTests();
 nearbyNumberingTests();
 DrawingSheet spatial;spatial.dimensionNumbering.enabled=true;
 const auto add=[&](int id,QPointF position){spatial.dimensions.push_back({QUuid(QStringLiteral("00000000-0000-0000-0000-%1").arg(id,12,16,QChar('0'))),position});};
 add(1,{100,10});add(2,{20,12});add(3,{5,14});add(4,{100,14});add(5,{100,14});
 auto numbers=drawingDimensionNumbers(spatial);require(numbers.value(spatial.dimensions[1].dimensionId)==1&&numbers.value(spatial.dimensions[0].dimensionId)==2&&numbers.value(spatial.dimensions[2].dimensionId)==3&&numbers.value(spatial.dimensions[3].dimensionId)==4&&numbers.value(spatial.dimensions[4].dimensionId)==5,"ordine righe sinistra-destra e sopra-sotto, senza concatenare tolleranza, con parita' UUID");
 std::reverse(spatial.dimensions.begin(),spatial.dimensions.end());require(drawingDimensionNumbers(spatial)==numbers,"ordine indipendente dall'inserimento e dal vettore");spatial.dimensionNumbering.rowTolerance=0;require(drawingDimensionNumbers(spatial).value(QUuid("00000000-0000-0000-0000-000000000001"))==1,"tolleranza zero mantiene le righe esatte");
 for(int number:{1,99,100,999999})for(double diameter:{3.,6.,20.}){const auto glyph=drawingDimensionBadgeText(number,diameter,{}).boundingRect();require(glyph.width()<=diameter*.72+1e-7&&glyph.height()<=diameter*.48+1e-7&&QLineF(glyph.center(),{}).length()<1e-7,"numero centrato e contenuto nel cerchio anche a sei cifre");}
 DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeBox(Frame3(),10,6,4),1)};model.choices={{1,"Blocco"}};DrawingSheet sheet;sheet.title="Quote numerate";sheet.drawing.model=model.snapshot.version;
 DrawingSheetEditor e(sheet,model);e.resize(1450,900);e.show();QApplication::processEvents();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=4;ok(e.insertView(v,{v.id,"Superiore",{115,115},{1}}));
 const auto a=seriesPoint(e,model,v,{0,0}),b=seriesPoint(e,model,v,{10,6});QVector<QUuid> ids;
 const QVector<QPointF> positions{{80,60},{160,60},{80,145},{160,145}};
 for(int i=0;i<4;++i){DrawingDimension d;d.viewId=v.id;d.section=a.curve;d.references={a,b};d.measure=i%2?DrawingMeasure::VerticalDistance:DrawingMeasure::HorizontalDistance;ok(e.addDimension(d,positions[i]));ids.push_back(d.id);}
 e.fitSheet();QApplication::processEvents();require(items(e,"dimensionBadge")==0,"fogli precedenti senza badge");e.markSaved();const auto legacy=encoded(e);const auto *curve=e.curves(v.id).front().curve.get();DrawingDimensionNumbering setting{true,1,6,3};ok(e.setDimensionNumbering(setting));QApplication::processEvents();const auto numbered=encoded(e);
 require(e.isModified()&&items(e,"dimensionBadge")==4&&e.curves(v.id).front().curve.get()==curve,"badge su tutte le quote e cache geometrica conservata");
 for(int i=0;i<4;++i){auto *badge=numberBadge(e,ids[i]);auto *label=dimensionTextItem(e,ids[i]);require(badge->data(7).toInt()==i+1&&!badge->sceneBoundingRect().intersects(label->sceneBoundingRect()),"numero spaziale e spazio libero dal testo");near(badge->sceneBoundingRect().width(),6.3,"diametro in mm carta indipendente dalla scala vista");}
 persistentNumberingTests(e,ids);
 nearbyNumberingGuiTests(model,e.sheet());
 viewNumberingGuiTests(model,e.sheet());
 drawingDimensionNumberingExclusionTests(e,ids);
 ok(e.undo());require(encoded(e)==legacy&&!e.isModified(),"Undo numerazione e stato salvato");ok(e.redo());require(encoded(e)==numbered,"Redo numerazione");ok(e.moveDimension(ids[3],{25,25}));QApplication::processEvents();require(numberBadge(e,ids[3])->data(7).toInt()==4&&numberBadge(e,ids[0])->data(7).toInt()==1,"spostamento conserva la sequenza");near(e.dimensionResult(ids[3]).value,6,"badge non altera misura");ok(e.undo());require(encoded(e)==numbered,"Undo spostamento ripristina numeri e disposizione");QApplication::processEvents();
 DrawingDimension extra=e.sheet().drawing.dimensions.front();extra.id=QUuid::createUuid();ok(e.addDimension(extra,{20,20}));QApplication::processEvents();require(items(e,"dimensionBadge")==5&&numberBadge(e,extra.id)->data(7).toInt()==5,"inserimento usa il progressivo successivo");select(e,extra.id);e.findChild<QAction *>("drawingDeleteSelectionAction")->trigger();QApplication::processEvents();require(items(e,"dimensionBadge")==4&&numberBadge(e,ids[0])->data(7).toInt()==1,"eliminazione compatta la sequenza");ok(e.undo());require(e.sheet().drawing.dimensions.size()==5,"Undo cancellazione ripristina quota numerata");ok(e.undo());require(encoded(e)==numbered,"Undo inserimento ripristina sequenza precedente");QApplication::processEvents();
 // Il badge segue anche le trasformazioni transitorie del testo durante il drag.
 auto *moving=dimensionTextItem(e,ids[0]);const auto before=numberBadge(e,ids[0])->sceneBoundingRect();moving->setPos(moving->pos()+QPointF(3,7));const auto after=numberBadge(e,ids[0])->sceneBoundingRect();near(after.left()-before.left(),3,"badge segue X transitorio");near(after.top()-before.top(),7,"badge segue Y transitorio");moving->setPos(moving->pos()-QPointF(3,7));
 DrawingSheet decoded;ok(decodeDrawingSheet(numbered,decoded));require(decoded.dimensionNumbering.enabled&&QJsonDocument::fromJson(numbered).object()["schema"].toInt()==20,"codec20 preserva numerazione");
 const auto reject=[&](QJsonObject json){DrawingSheet target=decoded;QByteArray untouched;ok(encodeDrawingSheet(target,untouched));require(!decodeDrawingSheet(QJsonDocument(json).toJson(),target).isEmpty(),"codec rifiuta numerazione malformata");QByteArray actual;ok(encodeDrawingSheet(target,actual));require(actual==untouched,"decode atomico");};
 const auto json=QJsonDocument::fromJson(numbered).object();auto bad=json;bad["schema"]=19;reject(bad);bad=json;bad.remove("dimensionNumbering");reject(bad);
 for(const auto &key:{"enabled","firstNumber","diameter","rowTolerance"}){bad=json;auto n=bad["dimensionNumbering"].toObject();n.remove(key);bad["dimensionNumbering"]=n;reject(bad);}
 for(const auto &entry:QVector<QPair<QString,QJsonValue>>{{"enabled",1},{"firstNumber",1.5},{"firstNumber",0},{"firstNumber",999999},{"diameter",2.99},{"diameter",20.01},{"rowTolerance",-1},{"rowTolerance",20.01},{"diameter","6"},{"extra",true}}){bad=json;auto n=bad["dimensionNumbering"].toObject();n[entry.first]=entry.second;bad["dimensionNumbering"]=n;reject(bad);}
 auto invalid=setting;invalid.diameter=std::numeric_limits<double>::quiet_NaN();require(!e.setDimensionNumbering(invalid).isEmpty()&&encoded(e)==numbered,"NaN rifiutato senza modifiche");invalid=setting;invalid.firstNumber=999999;require(!e.setDimensionNumbering(invalid).isEmpty(),"overflow numerazione rifiutato");
 auto disabled=setting;disabled.enabled=false;ok(e.setDimensionNumbering(disabled));require(QJsonDocument::fromJson(encoded(e)).object()["schema"].toInt()==22,"disattivazione conserva riserve persistenti");disabled.firstNumber=27;ok(e.setDimensionNumbering(disabled));ok(decodeDrawingSheet(encoded(e),decoded));require(!decoded.dimensionNumbering.enabled&&decoded.dimensionNumbering.firstNumber==27,"impostazioni personalizzate conservate anche se disattivate");ok(e.setDimensionNumbering(setting));QApplication::processEvents();
 const auto qa=QStringLiteral("/tmp/forgecad-numbering-qa");QDir().mkpath(qa);bool dialogSeen=false;
 QTimer::singleShot(0,[&]{auto *dialog=e.findChild<QDialog *>("drawingDimensionNumberingDialog");require(dialog,"dialogo numerazione aperto");dialog->findChild<QSpinBox *>("drawingNumberingFirst")->setValue(10);dialog->findChild<QDoubleSpinBox *>("drawingNumberingDiameter")->setValue(7);dialog->grab().save(qa+"/dialogo.png");dialogSeen=true;dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});e.findChild<QAction *>("drawingDimensionNumberingAction")->trigger();QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);require(dialogSeen&&e.sheet().dimensionNumbering.firstNumber==10,"impostazioni dal dialogo");QApplication::processEvents();const auto beforeCancel=encoded(e);QTimer::singleShot(0,[&]{auto *dialog=e.findChild<QDialog *>("drawingDimensionNumberingDialog");dialog->findChild<QCheckBox *>("drawingNumberingEnabled")->setChecked(false);dialog->reject();});e.findChild<QAction *>("drawingDimensionNumberingAction")->trigger();QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);require(encoded(e)==beforeCancel,"annulla dialogo conserva foglio");ok(e.setDimensionNumbering(setting));QApplication::processEvents();e.grab().save(qa+"/quote.png");
 auto tolerance=e.sheet().dimensions[1].presentation;tolerance.mode=DrawingToleranceMode::Limits;tolerance.upper=.2;tolerance.lower=-.1;ok(e.updateDimension(e.sheet().drawing.dimensions[1],tolerance));auto style=e.sheet().style;style.dimensionTextOrientation=DrawingTextOrientation::Aligned;style.dimensionTextPosition=DrawingTextPosition::Above;ok(e.setDrawingStyle(style));QApplication::processEvents();auto *vertical=dimensionTextItem(e,ids[1]);auto *badge=numberBadge(e,ids[1]);require(std::abs(vertical->rotation())>1&&std::abs(badge->sceneTransform().m12())<1e-7&&!badge->sceneBoundingRect().intersects(vertical->sceneBoundingRect()),"badge diritto e libero con limiti impilati e testo verticale");e.grab().save(qa+"/ruotate.png");
 e.setReadOnly(true);require(!e.findChild<QAction *>("drawingDimensionNumberingAction")->isEnabled()&&!e.setDimensionNumbering({}).isEmpty(),"sola lettura protegge comando e API");DrawingPdfReport report;ok(e.exportPdf(qa+"/quote.pdf",report));QFile pdf(qa+"/quote.pdf");require(pdf.open(QIODevice::ReadOnly)&&!pdf.readAll().contains("/Subtype /Image"),"PDF quote numerate vettoriale");e.setReadOnly(false);
 auto absent=model;absent.snapshot.bodies.clear();absent.choices.clear();ok(e.updateModel(absent));QApplication::processEvents();require(items(e,"dimensionBadge")==4&&e.dimensionResult(ids[0]).status!=DrawingDimensionStatus::Ready,"quote orfane mantengono badge senza riusare i numeri");
 std::cout<<"PASS numerazione: ordine per righe, parita', codec20/legacy, Undo/Redo, movimenti, rotazioni/tolleranze, dialogo, sola lettura, orfani e PDF vettoriale\n";
}
}
