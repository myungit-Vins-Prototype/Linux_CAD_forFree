#include "forgeCad2026_dialogs.h"
#include "cad_datum.h"
#include "cad_expression.h"
#include "cad_pattern.h"
#include "cad_model_history.h"
#include "cad_icons.h"
#include "cad_feature_dependencies.h"
#include "cad_surface_tools.h"
#include "cad_topology_ref.h"
#include "fk_surface_algo.h"
#include "fk_curve_ops.h"
#include "fk_classify.h"
#include <cmath>
namespace {
ForgeCad::ExpressionSpinBox *number(FunctionDialogPanel &dialog,double value,double low,double high,int decimals=6) {
    auto *box=new ForgeCad::ExpressionSpinBox(&dialog);box->setDecimals(decimals);box->setRange(low,high);box->setValue(value);box->setKeyboardTracking(false);return box;
}
GeometryRef modelPlane(int index){GeometryRef r;r.kind=1;r.index=index;return r;}
bool samePickedFace(const GeometryRef &a,const GeometryRef &b) {
    if(a.kind!=b.kind||a.index!=b.index)return false;
    // Durante la scelta il B-rep a monte e' immutabile. Il nome genealogico
    // puo' essere comune a piu' discendenti: non identifica da solo la faccia.
    if(a.point.subshape>=0&&b.point.subshape>=0)return a.point.subshape==b.point.subshape;
    return !a.point.name.isEmpty()&&a.point.name==b.point.name&&a.point.x==b.point.x&&a.point.y==b.point.y&&a.point.z==b.point.z;
}

QVector<int> sources(CadViewport *view,int replaced,bool features) {
    auto list=view->resultBodiesBefore(replaced);const auto &b=view->extrusions();
    const int end=replaced<0?view->historyPosition():replaced;
    if(features)for(int i=0;i<end;++i)if(b[i].forgeBody&&!b[i].suppressed&&ForgeCad::patternFeatureSupported(b[i])&&!list.contains(i))list.append(i);
    return list;
}
}
bool fillPatternDialog(QMainWindow *window,CadViewport *view,const QString &title,int replaced,const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject&)> &apply) {
    using namespace ForgeCad;
    ExtrusionObject definition=initial;definition.feature=BodyFeature::Pattern;definition.operation=-1;
    FunctionDialogPanel dialog(window,false);dialog.setWindowTitle(title);dialog.setSizeIdentity("pattern-fill");setDisplayLengthUnit(view->lengthUnit(),&dialog);auto *form=dialog.createScrollableForm();
    const auto candidates=sources(view,replaced,true);
    auto *body=new QComboBox(&dialog);body->setObjectName("fillPatternSource");for(int i:candidates)body->addItem(logicalBodyLabel(view,i)+QStringLiteral(" — ")+view->extrusions()[i].name,i);
    body->setCurrentIndex(candidates.indexOf(initial.firstBody));BodyPicker picker(dialog,view,replaced,candidates,body);
    auto *what=new QComboBox(&dialog);what->addItems({QStringLiteral("Corpo intero"),QStringLiteral("Funzione (materiale aggiunto o sottratto)")});what->setCurrentIndex(initial.pattern.featureOnly?1:0);
    auto *kind=new QComboBox(&dialog);kind->setObjectName("fillPatternKind");kind->setIconSize(QSize(48,48));
    const QStringList icons={"fillRect","fillStaggered","fillHex","fillCircular"};
    const QStringList names={QStringLiteral("Rettangolare"),QStringLiteral("Sfalsato"),QStringLiteral("Esagonale"),QStringLiteral("Concentrico")};
    for(int i=0;i<4;++i)kind->addItem(commandIcon(icons[i]),names[i]);
    kind->setCurrentIndex(qBound(0,initial.pattern.kind-3,3));
    GeometryRef boundary=initial.pattern.refs.value(0);
    auto *sketch=new QComboBox(&dialog);sketch->setObjectName("fillPatternBoundary");sketch->addItem(QStringLiteral("Scegli uno schizzo o una faccia dalla vista"));
    for(const auto &s:view->sketches())sketch->addItem(s.name);
    if(boundary.kind==7)sketch->setCurrentIndex(boundary.index+1);
    auto *pick=new QPushButton(&dialog);pick->setObjectName("fillPatternBoundaryPick");
    auto *stepX=number(dialog,initial.pattern.spacing,1e-5,1e6),*stepY=number(dialog,initial.pattern.spacing2,1e-5,1e6),*angle=number(dialog,initial.pattern.angle==360?0:initial.pattern.angle,-360,360);
    angle->setSuffix(QStringLiteral(" °"));
    form->addRow(QStringLiteral("Pattern:"),kind);form->addRow(QStringLiteral("Ripeti:"),what);form->addRow(QStringLiteral("Sorgente:"),picker.row(body));
    form->addRow(QStringLiteral("Contorno:"),sketch);form->addRow(QStringLiteral("Dalla vista:"),pick);
    form->addRow(QStringLiteral("Passo tra copie:"),stepX);form->addRow(QStringLiteral("Passo righe / anelli:"),stepY);form->addRow(QStringLiteral("Rotazione griglia (°):"),angle);
    form->addRow(ForgeCad::Gui::wrappedNote(QStringLiteral("Il riempimento conserva solo copie interamente nel contorno, escludendo i fori. Il rettangolo di ingombro della sorgente viene controllato sui bordi esatti. Massimo500copie. Nessun originale aggiunto fuori dal contorno."),&dialog));
    PreviewScope scope(view,dialog,form,replaced);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    bool choosing=false;
    const auto current=[&]{auto d=definition;d.firstBody=body->currentIndex()<0?-1:candidates[body->currentIndex()];d.secondBody=-1;d.pattern.kind=kind->currentIndex()+3;d.pattern.refs={boundary};d.pattern.spacing=stepX->value();d.pattern.spacing2=stepY->value();d.pattern.angle=angle->value();d.pattern.featureOnly=what->currentIndex()==1;d.pattern.keepOriginal=false;d.pattern.excluded.clear();d.pattern.variations.clear();d.patternComponent=-1;return d;};
    const auto refresh=[&]{stepY->setEnabled(kind->currentIndex()!=2);pick->setText(choosing?QStringLiteral("Clicca un contorno o una faccia piana"):boundary.kind>=0?geometryRefText(boundary,view->sketches(),view->extrusions()):QStringLiteral("Scegli contorno"));view->setReferenceMarks(boundary.kind>=0?QVector<GeometryRef>{boundary}:QVector<GeometryRef>{});if(!choosing&&!picker.active())scope.request(current());};
    picker.changed=refresh;picker.resolve=[&](const GeometryRef &ref){if(ref.kind!=5)return -1;const int index=what->currentIndex()==1?view->sourceFeatureForFace(ref):resultBodyForPick(view,candidates,ref.index);return candidates.contains(index)?index:-1;};
    view->setReferencePickCallback([&](bool picked,GeometryRef ref){if(picker.handle(picked,ref))return;if(picked&&choosing&&(ref.kind==5||ref.kind==7)) {if(ref.kind==7)ref.element={};boundary=ref;QSignalBlocker block(sketch);sketch->setCurrentIndex(ref.kind==7?ref.index+1:0);}choosing=false;refresh();});
    QObject::connect(pick,&QPushButton::clicked,&dialog,[&]{picker.stop();choosing=true;view->beginReferencePick(DatumRoleFace|DatumRoleCurve,replaced);refresh();});
    QObject::connect(sketch,&QComboBox::currentIndexChanged,&dialog,[&](int i){if(i>0){boundary={};boundary.kind=7;boundary.index=i-1;choosing=false;view->cancelReferencePick();refresh();}});
    for(auto *box:{kind,what,body})QObject::connect(box,&QComboBox::currentIndexChanged,&dialog,[&]{refresh();});
    for(auto *box:{stepX,stepY,angle})QObject::connect(box,&QDoubleSpinBox::valueChanged,&dialog,[&]{refresh();});
    refresh();const bool applied=runUntilAppliedModeless(window,view,dialog,form,buttons,[&]{if(choosing||picker.active())return QStringLiteral("Termina la scelta dei riferimenti.");auto d=current();d.patternComponent=0;return apply(d);});
    view->setReferencePickCallback(nullptr);view->setReferenceMarks({});return applied;
}
bool surfaceMappingDialog(QMainWindow *window,CadViewport *view,const QString &title,int replaced,const ExtrusionObject &initial,
                           const std::function<QString(const ExtrusionObject&)> &apply) {
    using namespace ForgeCad;using namespace Kernel;
    ExtrusionObject definition=initial;auto &parameters=definition.surfaceTool;const bool unroll=parameters.mode==3;
    if(parameters.targets.size()<2)parameters.targets={GeometryRef{},modelPlane(0)};
    QVector<GeometryRef> targetFaces;
    if(!unroll)for(int i=0;i<parameters.targets.size();++i)if(i!=1&&parameters.targets[i].kind==5)targetFaces.append(parameters.targets[i]);
    auto selections=parameters.sources;
    if(selections.isEmpty()) {
        if(unroll&&parameters.targets[0].kind==5)selections.append(parameters.targets[0]);
        else if(!unroll&&initial.firstBody>=0){GeometryRef r;r.kind=11;r.index=initial.firstBody;selections.append(r);}
    }
    QVector<GeometryRef> sourceFaces;
    for(const auto &r:selections)if(r.kind==5)sourceFaces.append(r);
    FunctionDialogPanel dialog(window,false);dialog.setWindowTitle(title);
    dialog.setSizeIdentity(unroll?"surface-unroll":"surface-wrap");setDisplayLengthUnit(view->lengthUnit(),&dialog);
    auto *form=dialog.createScrollableForm();const int owner=replaced<0?view->historyPosition():replaced;
    auto candidates=sources(view,replaced,false);
    for(const auto &r:selections)if(r.index>=0&&r.index<owner&&!candidates.contains(r.index))candidates.append(r.index);
    auto *bodies=new QListWidget(&dialog);bodies->setObjectName("mappingBodies");bodies->setMaximumHeight(120);
    for(int index:candidates) {
        auto *item=new QListWidgetItem(logicalBodyLabel(view,index)+QStringLiteral(" — ")+view->extrusions()[index].name,bodies);
        item->setData(Qt::UserRole,index);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
        item->setCheckState(std::any_of(selections.begin(),selections.end(),[&](const auto &r){return r.kind==11&&r.index==index;})?Qt::Checked:Qt::Unchecked);
    }
    BodyPicker picker(dialog,view,replaced,candidates,nullptr,bodies);
    auto *bodyControls=picker.row(new QWidget(&dialog));
    auto *pickBody=bodyControls->findChild<QPushButton*>();pickBody->setObjectName("mappingBodyPick");
    auto *bodyLayout=static_cast<QHBoxLayout*>(bodyControls->layout());
    auto *copies=new QPushButton(QStringLiteral("Copie"),bodyControls);copies->setObjectName("mappingPatternBodies");
    copies->setToolTip(QStringLiteral("Seleziona solo tutti i corpi della ripetizione evidenziata nell'elenco."));bodyLayout->addWidget(copies);
    auto *all=new QPushButton(QStringLiteral("Tutti"),bodyControls),*none=new QPushButton(QStringLiteral("Nessuno"),bodyControls);
    all->setObjectName("mappingAllBodies");none->setObjectName("mappingNoBodies");bodyLayout->addWidget(all);bodyLayout->addWidget(none);
    auto *faceList=new GeometryReferenceList(dialog,view,"mappingSourceFaces");
    faceList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto *facePick=new QPushButton(&dialog);facePick->setObjectName(unroll?"mappingFacePick":"mappingSourceFacePick");
    auto *remove=new QPushButton(QStringLiteral("Rimuovi"),&dialog);remove->setObjectName("mappingRemoveFaces");
    auto *faceControls=new QWidget(&dialog);auto *faceLayout=new QHBoxLayout(faceControls);faceLayout->setContentsMargins(0,0,0,0);faceLayout->addWidget(facePick);faceLayout->addWidget(remove);
    auto *target=new QPushButton(&dialog);target->setObjectName("mappingFacePick");
    if(unroll)target->setObjectName("mappingUnusedTarget");
    auto *targetList=new GeometryReferenceList(dialog,view,"mappingTargetFaces");
    targetList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto *removeTarget=new QPushButton(QStringLiteral("Rimuovi"),&dialog);removeTarget->setObjectName("mappingRemoveTargets");
    auto *targetControls=new QWidget(&dialog);auto *targetLayout=new QHBoxLayout(targetControls);
    targetLayout->setContentsMargins(0,0,0,0);targetLayout->addWidget(target);targetLayout->addWidget(removeTarget);
    auto *plane=new QComboBox(&dialog);plane->setObjectName("mappingPlane");QVector<GeometryRef> planes;
    for(int i=0;i<3;++i){planes.append(modelPlane(i));plane->addItem(planeNames().value(i));}
    for(int i=0;i<owner;++i)if(view->extrusions()[i].feature==BodyFeature::DatumPlane&&view->extrusions()[i].datumValid) {
        GeometryRef r;r.kind=8;r.index=i;r.featureId=view->extrusions()[i].featureId;planes.append(r);plane->addItem(view->extrusions()[i].name);
    }
    for(int i=0;i<planes.size();++i)if(planes[i].kind==parameters.targets[1].kind&&planes[i].index==parameters.targets[1].index)plane->setCurrentIndex(i);
    auto *u=number(dialog,parameters.mappingU,-1e6,1e6,12),*v=number(dialog,parameters.mappingV,-1e6,1e6,12);
    u->setObjectName("mappingU");v->setObjectName("mappingV");u->setLengthMeasurement(false);v->setLengthMeasurement(false);
    auto *x=number(dialog,parameters.mappingX,-1e6,1e6),*y=number(dialog,parameters.mappingY,-1e6,1e6),
         *angle=number(dialog,parameters.mappingAngle,-360,360),*offset=number(dialog,parameters.mappingOffset,-1e6,1e6);
    angle->setLengthMeasurement(false);angle->setSuffix(QStringLiteral(" °"));
    auto *place=new QPushButton(QStringLiteral("Centra e allinea dalla sorgente"),&dialog);place->setObjectName("mappingAlignSource");
    place->setToolTip(QStringLiteral("Usa il piano dello schizzo sorgente quando disponibile; centra i corpi selezionati sulla faccia e allinea il loro asse X alla direzione parametrica piu' vicina. Le posizioni restano modificabili."));
    auto *placementNote=new QLabel(&dialog);placementNote->setWordWrap(true);
    auto *copy=new QCheckBox(QStringLiteral("Conserva anche i corpi sorgenti"),&dialog);copy->setChecked(parameters.mappingCopy);
    form->addRow(QStringLiteral("Corpi da deformare:"),bodies);form->setRowVisible(bodies,!unroll);
    form->addRow(bodyControls);form->setRowVisible(bodyControls,!unroll);
    form->addRow(unroll?QStringLiteral("Facce da distendere:"):QStringLiteral("Facce da deformare:"),faceList);form->addRow(faceControls);
    form->addRow(QStringLiteral("Superfici destinazione:"),targetList);form->setRowVisible(targetList,!unroll);
    form->addRow(targetControls);form->setRowVisible(targetControls,!unroll);
    form->addRow(unroll?QStringLiteral("Piano destinazione:"):QStringLiteral("Piano sorgente:"),plane);
    form->addRow(place);form->setRowVisible(place,!unroll);form->addRow(placementNote);form->setRowVisible(placementNote,!unroll);
    form->addRow(QStringLiteral("Punto destinazione U:"),u);form->addRow(QStringLiteral("Punto destinazione V:"),v);
    form->setRowVisible(u,!unroll);form->setRowVisible(v,!unroll);
    form->addRow(QStringLiteral("Posizione X:"),x);form->addRow(QStringLiteral("Posizione Y:"),y);form->addRow(QStringLiteral("Rotazione (°):"),angle);
    form->addRow(QStringLiteral("Distanza normale:"),offset);form->setRowVisible(offset,!unroll);form->addRow(copy);form->setRowVisible(copy,!unroll);
    form->addRow(ForgeCad::Gui::wrappedNote(unroll?
        QStringLiteral("Scegli piu' facce, anche dalla storyboard. Secondo clic: rimuovi; Esc o Termina selezione: anteprima. Sviluppo esatto di piani, cilindri e coni supportati, conservando la precisione dei bordi sorgenti. Le facce sono disposte sul piano come isole separate a 1 mm. Le facce a doppia curvatura sono rifiutate."):
        QStringLiteral("Spunta piu' corpi e/o aggiungi singole facce. I corpi interi restano solidi; le facce diventano lamine separate. Aggiungi piu' facce destinazione: formano un unico appoggio e i corpi possono attraversarne le giunzioni tangenti. La prima faccia imposta il punto di appoggio. Con piu' facce la deformazione segue la normale a questo appoggio; vuoti o sovrapposizioni vengono segnalati. Le sorgenti selezionate come sole facce restano visibili."),&dialog));
    PreviewScope scope(view,dialog,form,replaced);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    int choosing=0; // 1 facce sorgenti, 2 destinazione
    const auto current=[&] {
        auto d=definition;d.operation=-1;d.feature=BodyFeature::SurfaceTools;auto &p=d.surfaceTool;
        p.sources=sourceFaces;
        if(!unroll)for(int i=0;i<bodies->count();++i)if(bodies->item(i)->checkState()==Qt::Checked) {
            GeometryRef r;r.kind=11;r.index=bodies->item(i)->data(Qt::UserRole).toInt();r.featureId=view->extrusions()[r.index].featureId;p.sources.append(r);
        }
        d.firstBody=p.sources.isEmpty()?-1:p.sources[0].index;
        p.targets={unroll?sourceFaces.value(0):targetFaces.value(0),planes[plane->currentIndex()]};
        if(!unroll)for(int i=1;i<targetFaces.size();++i)p.targets.append(targetFaces[i]);
        p.mappingU=u->value();p.mappingV=v->value();
        p.mappingX=x->value();p.mappingY=y->value();p.mappingAngle=angle->value();p.mappingOffset=offset->value();p.mappingCopy=unroll||copy->isChecked();return d;
    };
    const auto refresh=[&] {
        place->setEnabled(!targetFaces.isEmpty()&&!choosing&&!picker.active());
        faceList->setReferences(sourceFaces);
        for(int i=0;i<sourceFaces.size();++i)faceList->item(i)->setText(faceList->item(i)->text()+QStringLiteral(" — F%1").arg(sourceFaces[i].point.subshape));
        facePick->setText(choosing==1?QStringLiteral("Termina selezione"):QStringLiteral("Scegli facce nella vista"));
        target->setText(choosing==2?QStringLiteral("Termina selezione"):QStringLiteral("Scegli superfici nella vista"));
        targetList->setReferences(targetFaces);
        for(int i=0;i<targetFaces.size();++i)targetList->item(i)->setText(targetList->item(i)->text()+QStringLiteral(" — F%1%2").arg(targetFaces[i].point.subshape).arg(i==0?QStringLiteral(" (appoggio)"):QString()));
        auto marks=sourceFaces;if(!unroll)marks+=targetFaces;view->setReferenceMarks(marks);
        if(!choosing&&!picker.active())scope.request(current());
    };
    picker.changed=refresh;picker.starting=[&]{choosing=0;};
    const auto begin=[&](int step){picker.stop();view->cancelReferencePick();choosing=step;view->beginReferencePick(DatumRoleFace,replaced);refresh();};
    QObject::connect(facePick,&QPushButton::clicked,&dialog,[&]{if(choosing==1){choosing=0;view->cancelReferencePick();refresh();}else begin(1);});
    QObject::connect(target,&QPushButton::clicked,&dialog,[&]{if(choosing==2){choosing=0;view->cancelReferencePick();refresh();}else begin(2);});
    QObject::connect(pickBody,&QPushButton::clicked,&dialog,[&]{choosing=0;refresh();});
    view->setReferencePickCallback([&](bool picked,GeometryRef ref) {
        if(picker.handle(picked,ref))return;
        const int step=choosing;choosing=0;
        if(picked&&ref.kind==5&&step==1) {
            const auto existing=std::find_if(sourceFaces.begin(),sourceFaces.end(),[&](const auto &r){return samePickedFace(r,ref);});
            if(existing==sourceFaces.end())sourceFaces.append(ref);else sourceFaces.erase(existing);
            begin(1);return;
        }
        if(picked&&ref.kind==5&&step==2) {
            const auto existing=std::find_if(targetFaces.begin(),targetFaces.end(),[&](const auto &r){return samePickedFace(r,ref);});
            const bool firstChanged=targetFaces.isEmpty()||existing==targetFaces.begin();
            if(existing==targetFaces.end())targetFaces.append(ref);else targetFaces.erase(existing);
            if(firstChanged&&!targetFaces.isEmpty()) {
                const auto &first=targetFaces.front();
                if(first.index>=0&&first.index<view->extrusions().size())if(const auto carrier=view->extrusions()[first.index].forgeBody) {
                    const auto id=resolveFaceReference(*carrier,first.point,1e-6);
                    if(id.valid()){const auto q=projectPoint(*carrier->face(id).surface,Vec3(first.point.x,first.point.y,first.point.z));QSignalBlocker bu(u),bv(v);u->setValue(q.u);v->setValue(q.v);}
                }
            }
            begin(2);return;
        }
        refresh();
    });
    QObject::connect(remove,&QPushButton::clicked,&dialog,[&]{auto rows=faceList->selectionModel()->selectedRows();std::sort(rows.begin(),rows.end(),[](const auto &a,const auto &b){return a.row()>b.row();});for(const auto &row:rows)sourceFaces.removeAt(row.row());refresh();});
    QObject::connect(removeTarget,&QPushButton::clicked,&dialog,[&]{
        auto rows=targetList->selectionModel()->selectedRows();std::sort(rows.begin(),rows.end(),[](const auto &a,const auto &b){return a.row()>b.row();});
        bool firstChanged=false;for(const auto &row:rows){firstChanged=firstChanged||row.row()==0;targetFaces.removeAt(row.row());}
        if(firstChanged&&!targetFaces.isEmpty()) {
            const auto &first=targetFaces.front();const auto carrier=view->extrusions()[first.index].forgeBody;
            const auto face=carrier?resolveFaceReference(*carrier,first.point,1e-6):FaceId{};
            if(face.valid()){const auto q=projectPoint(*carrier->face(face).surface,Vec3(first.point.x,first.point.y,first.point.z));QSignalBlocker bu(u),bv(v);u->setValue(q.u);v->setValue(q.v);}
        }
        refresh();
    });
    QObject::connect(copies,&QPushButton::clicked,&dialog,[&] {
        auto *item=bodies->currentItem();
        if(!item)for(int i=0;i<bodies->count();++i)if(bodies->item(i)->checkState()==Qt::Checked){item=bodies->item(i);break;}
        const int root=item?patternSourceIndex(view->extrusions(),item->data(Qt::UserRole).toInt()):-1;
        if(root<0||view->extrusions()[root].feature!=BodyFeature::Pattern){placementNote->setText(QStringLiteral("Evidenzia un corpo della ripetizione nell'elenco, poi premi Copie."));return;}
        {QSignalBlocker block(bodies);for(int i=0;i<bodies->count();++i){auto *entry=bodies->item(i);entry->setCheckState(patternSourceIndex(view->extrusions(),entry->data(Qt::UserRole).toInt())==root?Qt::Checked:Qt::Unchecked);}}
        placementNote->clear();refresh();
    });
    QObject::connect(place,&QPushButton::clicked,&dialog,[&] {
        const auto fail=[&](const QString &message){placementNote->setText(message);};
        auto params=current().surfaceTool;
        if(params.sources.isEmpty()){fail(QStringLiteral("Seleziona i corpi o le facce da deformare."));return;}
        for(const auto &source:params.sources)if(source.index<0||source.index>=owner||!view->extrusions()[source.index].forgeBody){fail(QStringLiteral("Sorgente non disponibile."));return;}
        auto planeReference=planes[plane->currentIndex()];
        // Per una ripetizione si risale alla funzione sorgente, conservando
        // il riferimento esplicito al piano dello schizzo (anche traslato).
        int seed=patternSourceIndex(view->extrusions(),params.sources.front().index);
        if(view->extrusions()[seed].feature==BodyFeature::Pattern)seed=view->extrusions()[seed].firstBody;
        if(seed>=0&&seed<owner) {
            const int sketch=view->extrusions()[seed].sketchIndex;
            if(sketch>=0&&sketch<view->sketches().size()) {
                const auto &source=view->sketches()[sketch];
                if(source.datumPlane>=0&&source.datumPlane<owner){planeReference={};planeReference.kind=8;planeReference.index=source.datumPlane;planeReference.featureId=view->extrusions()[source.datumPlane].featureId;}
                else if(!source.customFrame&&source.plane>=0&&source.plane<3)planeReference=modelPlane(source.plane);
            }
        }
        ResolvedRef resolved;QString reason;
        if(!resolveGeometryRef(planeReference,owner,view->sketches(),view->extrusions(),resolved,&reason)||!resolved.hasPlane){fail(reason);return;}
        const Frame3 frame(resolved.point,resolved.direction,std::fabs(resolved.direction.x())<.9?Vec3(1,0,0):Vec3(0,1,0));
        const auto &ref=targetFaces.front();const auto carrier=view->extrusions()[ref.index].forgeBody;
        const auto id=carrier?resolveFaceReference(*carrier,ref.point,1e-6):FaceId{};
        if(!id.valid()){fail(QStringLiteral("Faccia destinazione mancante o ambigua."));return;}
        const auto &face=carrier->face(id);Box uvBox,sourceBox;
        for(auto loop:face.loops)for(auto use:carrier->loopFins(loop)) {
            const auto &fin=carrier->fin(use);if(fin.pcurve)uvBox.add(curveBox(*embedCurve(fin.pcurve,Frame3()),carrier->edge(fin.edge).range));
        }
        if(uvBox.isEmpty()){fail(QStringLiteral("La faccia non ha un dominio limitato."));return;}
        Vec2 anchor(.5*uvBox.lo.x()+.5*uvBox.hi.x(),.5*uvBox.lo.y()+.5*uvBox.hi.y());
        if(classifyPointOnFace(*carrier,id,face.surface->point(anchor.x(),anchor.y()),1e-7)==PointLocation::Outside) {
            Vec3 inside;if(!faceInteriorPoint(*carrier,id,inside,&anchor)){fail(QStringLiteral("Nessun appoggio interno alla faccia."));return;}
        }
        Vec3 derivatives[4];face.surface->evaluate(anchor.x(),anchor.y(),1,derivatives);
        if(norm(derivatives[2])<1e-12||norm(derivatives[1])<1e-12){fail(QStringLiteral("Il centro della faccia e' singolare."));return;}
        const auto du=normalized(derivatives[2]),dv=(targetFaces.size()>1&&!face.sense?-1.:1.)*normalized(derivatives[1]);
        const double crossTerm=dot(du,dv),det=1-crossTerm*crossTerm;
        if(det<=1e-12){fail(QStringLiteral("Tangenti della faccia degeneri."));return;}
        const double alongU=dot(frame.xDir(),du),alongV=dot(frame.xDir(),dv);
        if(std::hypot(alongU,alongV)<1e-8){fail(QStringLiteral("L'asse X sorgente e' normale alla faccia: scegli un altro piano."));return;}
        // Sceglie il verso parametrico piu' vicino all'asse X sorgente.
        // La proiezione obliqua nel piano tangente mescolava l'asse lungo
        // e quello corto sulle facce sagomate, facendo uscire le copie da U.
        const double rotation=std::fabs(alongU)>=std::fabs(alongV)?(alongU>=0?0.:kPi):(alongV>=0?kHalfPi:-kHalfPi);
        const double c=std::cos(rotation),s=std::sin(rotation);
        for(const auto &source:params.sources) {
            const auto body=view->extrusions()[source.index].forgeBody;
            const auto faces=source.kind==5?std::vector<FaceId>{resolveFaceReference(*body,source.point,1e-6)}:body->faces();
            for(auto f:faces)if(f.valid())for(auto loop:body->face(f).loops)for(auto use:body->loopFins(loop)) {
                const auto &edge=body->edge(body->fin(use).edge);
                // Porta le curve nel piano esatto prima dell'ingombro: nessuna
                // dipendenza da mesh o orientamento della camera.
                auto local=std::make_shared<TransformedCurve>(edge.curve,Transform3::fromFrame(frame).inverted());
                sourceBox.add(curveBox(*local,edge.range));
            }
        }
        if(sourceBox.isEmpty()){fail(QStringLiteral("Sorgenti senza geometria."));return;}
        const double cx=.5*sourceBox.lo.x()+.5*sourceBox.hi.x(),cy=.5*sourceBox.lo.y()+.5*sourceBox.hi.y();
        {QSignalBlocker bp(plane),bu(u),bv(v),bx(x),by(y),ba(angle);
            for(int i=0;i<planes.size();++i)if(planes[i].kind==planeReference.kind&&planes[i].index==planeReference.index)plane->setCurrentIndex(i);
            u->setValue(anchor.x());v->setValue(anchor.y());angle->setValue(rotation*180./kPi);x->setValue(-c*cx+s*cy);y->setValue(-s*cx-c*cy);}
        placementNote->setText(QStringLiteral("Sorgenti centrate e asse X allineato alla direzione della faccia. Regola posizione e rotazione per cambiare l'appoggio."));refresh();
    });
    const auto checkAll=[&](Qt::CheckState state){QSignalBlocker block(bodies);for(int i=0;i<bodies->count();++i)bodies->item(i)->setCheckState(state);refresh();};
    QObject::connect(all,&QPushButton::clicked,&dialog,[&]{checkAll(Qt::Checked);});QObject::connect(none,&QPushButton::clicked,&dialog,[&]{checkAll(Qt::Unchecked);});
    QObject::connect(bodies,&QListWidget::itemChanged,&dialog,[&]{refresh();});
    QObject::connect(plane,&QComboBox::currentIndexChanged,&dialog,[&]{refresh();});
    for(auto *box:{u,v,x,y,angle,offset})QObject::connect(box,&QDoubleSpinBox::valueChanged,&dialog,[&]{refresh();});
    QObject::connect(copy,&QCheckBox::toggled,&dialog,[&]{refresh();});
    refresh();const bool applied=runUntilAppliedModeless(window,view,dialog,form,buttons,[&]{if(choosing||picker.active())return QStringLiteral("Termina la scelta dei riferimenti.");return apply(current());});
    view->setReferencePickCallback(nullptr);view->setReferenceMarks({});return applied;
}
