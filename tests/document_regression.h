#ifndef FORGECAD_DOCUMENT_REGRESSION_H
#define FORGECAD_DOCUMENT_REGRESSION_H
#include "../cad_import.h"
#include "fk_body_io.h"
#include "forgecad_source_hash.h"
#include "../cad_model_history.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <map>
#include <condition_variable>
#include <chrono>
#include <thread>

// Diagnostica sui file forniti: nessuna geometria o fixture utente nel repo.
namespace DocumentRegression {
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
inline void progress(const QString &s) { std::cerr << s.toStdString() << std::endl; }
inline void save(const QString &path, const QJsonObject &value) {
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) throw std::runtime_error(out.errorString().toStdString());
    out.write(QJsonDocument(value).toJson(QJsonDocument::Indented));
    if (!out.commit()) throw std::runtime_error(out.errorString().toStdString());
}
inline DocumentState read(const QString &path, QStringList &notes) {
    DocumentState state;
    if (QFileInfo(path).suffix().compare(QStringLiteral("prt"), Qt::CaseInsensitive) == 0) {
        const QString error = loadDocumentFile(path, state);
        if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
    } else {
        QVector<ImportedPart> parts;
        const QString error = importCadFile(path, parts, &notes);
        if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
        for (const auto &part : parts) {
            ExtrusionObject feature; feature.feature = BodyFeature::Imported;
            feature.name = part.name; feature.importData = part.data; feature.importSource = path;
            feature.forgeBody = part.body; feature.solid = part.solid; feature.cachedGeometry = true;
            state.extrusions.append(feature);
        }
    }
    normalizeModelHistory(state);
    return state;
}
// Un integrale non disponibile non deve interrompere le altre verifiche.
// La cancellazione coopera con curve/superfici del kernel; non lascia worker.
template<class Task> auto budgeted(int seconds,const Task &task) -> decltype(task()) {
    auto token=std::make_shared<std::atomic<bool>>(false);
    std::mutex mutex;std::condition_variable condition;bool done=false;
    std::thread timer([&]{std::unique_lock<std::mutex> lock(mutex);
        if(!condition.wait_for(lock,std::chrono::seconds(seconds),[&]{return done;}))token->store(true);});
    const auto finish=[&]{{std::lock_guard<std::mutex> lock(mutex);done=true;}condition.notify_one();timer.join();};
    try{CancellationScope scope(token);auto value=task();finish();return value;}
    catch(...){finish();throw;}
}
inline QJsonObject snapshot(const DocumentState &state) {
    QJsonArray features, bodies;
    int errors = 0, invalid = 0, incomplete = 0;
    for (int i = 0; i < state.extrusions.size(); ++i) {
        const auto &f = state.extrusions[i];
        progress(QStringLiteral("misura %1/%2: %3").arg(i+1).arg(state.extrusions.size()).arg(f.name));
        QJsonObject row{{"id",QString::number(f.featureId)},{"body_id",QString::number(f.modelBodyId)},
            {"index",i},{"name",f.name},{"kind",int(f.feature)},{"suppressed",f.suppressed},
            {"error",f.error},{"notice",f.notice},{"has_shape",bool(f.forgeBody)},
            {"has_curve",bool(f.curve)||!f.curves.isEmpty()}};
        if (!f.suppressed && !f.error.isEmpty()) ++errors;
        QJsonArray resolutions;
        for (const auto &ref : f.referenceResolutions) resolutions.append(QJsonObject{
            {"method",int(ref.method)},{"name",ref.reference.name},{"kind",ref.kind},
            {"resolved_id",ref.resolvedSubshape},{"candidates",ref.candidates},{"warning",ref.warning}});
        row["resolutions"] = resolutions;
        if (f.forgeBody) {
            const Body &body = *f.forgeBody;
            row["faces"] = int(body.faces().size()); row["edges"] = int(body.edges().size());
            row["vertices"] = int(body.vertices().size()); row["sheet"] = body.isSheet();
            // Gli integrali e checkBody dipendono soltanto dal B-rep esatto.
            // Nomi e audit dipendono dalla feature: si ricalcolano sempre.
            static std::map<QByteArray,QJsonObject> geometryMetrics;
            const std::string binary=writeBodyBinary(body);
            const QByteArray signature=QCryptographicHash::hash(
                QByteArray::fromRawData(binary.data(),qsizetype(binary.size())),QCryptographicHash::Sha256);
            const auto cached=geometryMetrics.find(signature);
            if(cached!=geometryMetrics.end()){
                for(auto it=cached->second.begin();it!=cached->second.end();++it)row[it.key()]=it.value();
                if(!row["check_issues"].toArray().isEmpty())++invalid;
            }else{
            try {
                progress("  integrali B-rep");
                if(body.isSheet()){
                    const double area=budgeted(20,[&]{double sum=0;for(auto face:body.faces())sum+=faceArea(body,face,1e-8);return sum;});
                    if(!std::isfinite(area))throw std::runtime_error("area non finita");
                    row["area"]=area;row["volume"]=QJsonValue();
                }else{
                    const auto mass=budgeted(20,[&]{return massProperties(body,1e-8);});
                    if(!std::isfinite(mass.area)||!std::isfinite(mass.volume))throw std::runtime_error("massa non finita");
                    row["area"]=mass.area;row["volume"]=mass.volume;
                }
            } catch (const OperationCancelled &) { row["mass_error"]="timeout integrali dopo 20 s: volume non confrontabile"; }
            catch (const std::exception &e) { row["mass_error"] = QString::fromUtf8(e.what()); }
            try {
                QJsonArray issues;
                progress("  validita' B-rep");
                for (const auto &issue : budgeted(20,[&]{return checkBody(body);})) issues.append(QJsonObject{
                    {"code",int(issue.code)},{"message",QString::fromStdString(issue.message)}});
                row["check_issues"] = issues;
                if (!issues.isEmpty()) ++invalid;
            } catch (const OperationCancelled &) { row["check_error"]="timeout checkBody dopo 20 s: validita' non certificata"; }
            catch (const std::exception &e) { row["check_error"] = QString::fromUtf8(e.what()); }
                QJsonObject measured;
                for(const auto *key:{"volume","area","mass_error","check_issues","check_error"})
                    if(row.contains(key))measured[key]=row[key];
                geometryMetrics[signature]=measured;
            }
            try {
                progress("  nomi topologici");
                const auto names = elementNamesOf(body);
                QJsonObject tables;
                int missing = 0;
                for (const auto kind : {EntityKind::Face,EntityKind::Edge,EntityKind::Vertex}) {
                    QJsonArray entries;
                    const auto add = [&](int id) {
                        QJsonArray aliases;
                        if (names && id < int(names->table(kind).size()))
                            for (const auto &name : names->table(kind)[id]) aliases.append(QString::fromStdString(name));
                        if (aliases.isEmpty()) ++missing;
                        entries.append(QJsonObject{{"id",id},{"names",aliases}});
                    };
                    if (kind == EntityKind::Face) for (auto id : body.faces()) add(id.index);
                    if (kind == EntityKind::Edge) for (auto id : body.edges()) add(id.index);
                    if (kind == EntityKind::Vertex) for (auto id : body.vertices()) add(id.index);
                    tables[QString::number(int(kind))] = entries;
                }
                row["topology_names"] = tables; row["unnamed_entities"] = missing;
            } catch (const std::exception &e) { row["names_error"] = QString::fromUtf8(e.what()); }
        }
        if(row.contains("mass_error")||row.contains("check_error")||row.contains("names_error"))++incomplete;
        features.append(row);
    }
    for (const auto &body : state.modelBodies) bodies.append(QJsonObject{
        {"id",QString::number(body.id)},{"tip_feature_id",QString::number(body.tipFeatureId)}});
    return QJsonObject{{"features",features},{"bodies",bodies},{"feature_errors",errors},{"invalid_shapes",invalid},{"incomplete_metrics",incomplete}};
}
inline QJsonArray compare(const QJsonObject &before, const QJsonObject &after) {
    std::map<QString,QJsonObject> old;
    for (const auto &v : before["features"].toArray()) { auto f=v.toObject(); old[f["id"].toString()]=f; }
    QJsonArray changes;
    for (const auto &v : after["features"].toArray()) {
        const auto f = v.toObject(); const auto id = f["id"].toString();
        if (!old.count(id)) { changes.append(QJsonObject{{"id",id},{"new_feature",true}}); continue; }
        const auto a = old.at(id); old.erase(id); QJsonObject delta{{"id",id},{"name",f["name"]}};
        for (const auto *key : {"error","has_shape","has_curve","suppressed","faces","edges","vertices", "sheet", "mass_error","check_error","names_error","unnamed_entities"})
            if (a[key] != f[key]) delta[key]=QJsonObject{{"before",a[key]},{"after",f[key]}};
        for (const auto *key : {"volume","area"}) {
            if (a[key].isDouble() && f[key].isDouble()) {
                const double x=a[key].toDouble(), y=f[key].toDouble();
                if (std::fabs(x-y)>std::max(1e-7,1e-6*std::max(std::fabs(x),std::fabs(y))))
                    delta[key]=QJsonObject{{"before",x},{"after",y},{"relative",std::fabs(x-y)/std::max(1e-30,std::fabs(x))}};
            } else if (a[key]!=f[key]) delta[key]=QJsonObject{{"before",a[key]},{"after",f[key]}};
        }
        if (a["check_issues"]!=f["check_issues"]) delta["check_issues_changed"]=true;
        if (a["topology_names"]!=f["topology_names"]) {
            int same=0, lost=0, gained=0;
            QJsonObject perKind;
            for (const auto kind : {"0","1","2"}) {
                std::map<QString,int> left,right;
                for(const auto &e:a["topology_names"].toObject()[kind].toArray())
                    for(const auto &n:e.toObject()["names"].toArray()) ++left[n.toString()];
                for(const auto &e:f["topology_names"].toObject()[kind].toArray())
                    for(const auto &n:e.toObject()["names"].toArray()) ++right[n.toString()];
                int kept=0,removed=0,added=0;
                for(const auto &[n,count]:left){kept+=std::min(count,right[n]);removed+=std::max(0,count-right[n]);}
                for(const auto &[n,count]:right)added+=std::max(0,count-left[n]);
                perKind[kind]=QJsonObject{{"kept",kept},{"lost",removed},{"gained",added}};
                same+=kept;lost+=removed;gained+=added;
            }
            delta["names"]=QJsonObject{{"kept",same},{"lost",lost},{"gained",gained},{"by_kind",perKind},
                {"assignments_changed",true}};
        }
        if (delta.size()>2) changes.append(delta);
    }
    for(const auto &[id,f]:old)changes.append(QJsonObject{{"id",id},{"removed_feature",true}});
    if(before["bodies"]!=after["bodies"])changes.append(QJsonObject{{"body_tips_changed",true}});
    return changes;
}
inline bool change(ExtrusionObject &f, double factor, QString &field, double &old, double &next) {
    if(f.operation>=0||f.suppressed)return false;
    double *value=nullptr;
    switch(f.feature){
    case BodyFeature::Primitive:value=&f.primitive.size[0];field="primitive.size[0]";break;
    case BodyFeature::Extrusion: if(f.extent==0){value=&f.distance;field="distance";}break;
    case BodyFeature::Revolution: if(std::fabs(f.revolveAngle)<359.9){value=&f.revolveAngle;field="revolveAngle";}break;
    case BodyFeature::Blend: case BodyFeature::SheetExtend:value=&f.blendSize;field="blendSize";break;
    case BodyFeature::Scale:value=&f.scaleFactor;field="scaleFactor";break;
    case BodyFeature::SurfaceOffset:case BodyFeature::Thicken:case BodyFeature::Shell:value=&f.distance;field="distance";break;
    case BodyFeature::Draft:value=&f.draftAngle;field="draftAngle";break;
    case BodyFeature::Helix: value=f.helix.mode==0?&f.helix.pitch:&f.helix.height;field="helix.pitch/height";break;
    case BodyFeature::Thread:value=&f.thread.pitch;field="thread.pitch";break;
    case BodyFeature::Loft: if(f.loftStartContinuity){value=&f.loftStartInfluence;field="loftStartInfluence";}
        else if(f.loftEndContinuity){value=&f.loftEndInfluence;field="loftEndInfluence";}
        break;
    default:break;
    }
    if(!value||!std::isfinite(*value)||std::fabs(*value)<1e-10)return false;
    old=*value;*value*=factor;next=*value;return next!=old;
}
inline void run(const QString &scenario,const QString &path,const QString &output,int maxBodies) {
    if(scenario=="variants"){
        QJsonArray cases;save(output,QJsonObject{{"source",path},{"scenario",scenario},{"cases",cases},{"status","running"}});
        for(const QString &variant:QStringList{"parameter-early-plus","parameter-early-minus","parameter-late-plus","parameter-late-minus","reorder","suppress"}){
            progress("scenario "+variant);const QString child=output+"-"+variant+".json";
            try{run(variant,path,child,maxBodies);}catch(const std::exception &e){save(child,QJsonObject{
                {"source",path},{"scenario",variant},{"status","exception"},{"error",QString::fromUtf8(e.what())}});}
            QFile input(child);if(!input.open(QIODevice::ReadOnly))throw std::runtime_error(input.errorString().toStdString());auto row=QJsonDocument::fromJson(input.readAll()).object();
            row["output"]=child;cases.append(row);save(output,QJsonObject{{"source",path},{"scenario",scenario},
                {"cases",cases},{"status","running"},{"source_hash",FORGECAD_SOURCE_HASH}});
        }
        save(output,QJsonObject{{"source",path},{"scenario",scenario},{"cases",cases},{"status","completed"},{"source_hash",FORGECAD_SOURCE_HASH}});
        return;
    }
    // Gli STEP di grandi assiemi si escludono prima dell'importazione.
    // Le entita' solide contano le definizioni, non le istanze: conserviamo
    // esplicitamente questa distinzione nel rapporto dell'inventario.
    if(QFileInfo(path).suffix().compare(QStringLiteral("prt"),Qt::CaseInsensitive)!=0){
        QFile source(path);
        if(source.open(QIODevice::ReadOnly)){
            const QString data=QString::fromLatin1(source.readAll());
            const QRegularExpression solids(QStringLiteral("\\b(?:MANIFOLD_SOLID_BREP|BREP_WITH_VOIDS)\\s*\\("));
            auto matches=solids.globalMatch(data);int count=0;while(matches.hasNext()){matches.next();++count;}
            if(count>maxBodies){save(output,QJsonObject{{"source",path},{"scenario",scenario},
                {"status","excluded_source_solid_limit"},{"solid_definition_count",count}});return;}
        }
    }
    QStringList notes; auto doc=read(path,notes);
    QJsonObject result{{"source_hash",FORGECAD_SOURCE_HASH},{"source",path},{"scenario",scenario},{"bodies",int(doc.modelBodies.size())},
        {"feature_count",int(doc.extrusions.size())},{"notes",QJsonArray::fromStringList(notes)},
        {"volume_relative_tolerance",1e-6},{"quadrature_relative_tolerance",1e-8}};
    if(doc.modelBodies.size()>maxBodies){result["status"]="excluded_body_limit";save(output,result);return;}
    if(scenario=="inventory"){result["status"]="eligible";save(output,result);return;}
    CadViewport view;view.loadDocument(doc);
    // Le varianti partono dalla definizione rigenerata, anche quando il file
    // conserva cache precedenti a un errore. Il confronto con quelle cache
    // resta nel baseline e nella sua verifica di riapertura.
    if(scenario!="baseline")view.rebuildAllFeatures();
    auto base=view.currentDocument(); auto before=snapshot(base);result["before"]=before;save(output,result);
    if(scenario=="baseline"){
        progress("rigenerazione completa 1");result["rebuild_errors"]=view.rebuildAllFeatures();
        auto first=snapshot(view.currentDocument());result["after"]=first;
        result["snapshot_changes"]=compare(before,first);save(output,result);
        progress("rigenerazione completa 2");view.rebuildAllFeatures();auto second=snapshot(view.currentDocument());
        result["repeat"]=second;result["repeat_changes"]=compare(first,second);
        const QString rebuilt=output+".prt";const auto err=saveDocumentFile(rebuilt,view.currentDocument());
        result["rebuilt_document"]=rebuilt;result["save_error"]=err;
        bool roundtripStable=err.isEmpty();
        if(roundtripStable){
            progress("riapertura e rigenerazione della copia salvata");
            QStringList reloadNotes;view.loadDocument(read(rebuilt,reloadNotes));view.rebuildAllFeatures();
            auto reloaded=snapshot(view.currentDocument());result["roundtrip"]=reloaded;
            result["roundtrip_changes"]=compare(second,reloaded);
            roundtripStable=result["roundtrip_changes"].toArray().isEmpty();
        }
        result["status"]=roundtripStable&&result["repeat_changes"].toArray().isEmpty()
            ? ((second["feature_errors"].toInt()||second["invalid_shapes"].toInt())?"stable_with_errors":
                (second["incomplete_metrics"].toInt()?"stable_with_limits":"stable")) : "unstable";
        save(output,result);return;
    }
    QString operationError;bool applied=false;int target=-1;
    if(scenario.startsWith("parameter")){
        const double factor=scenario.endsWith("minus")?0.999:1.001;
        int start=scenario.contains("late")?int(base.extrusions.size())-1:0;
        int step=scenario.contains("late")?-1:1;
        QString field;double old=0,next=0;
        for(int i=start;i>=0&&i<base.extrusions.size();i+=step){auto edited=base.extrusions[i];
            if(change(edited,factor,field,old,next)){target=i;operationError=view.updateBody(i,edited);applied=operationError.isEmpty();break;}}
        if(target<0){
            for(int i=base.extrusions.size()-1;i>=0;--i)if(base.extrusions[i].forgeBody&&base.extrusions[i].modelBodyId&&!base.extrusions[i].suppressed){
                target=i;operationError=view.createScale(i,factor,0,{},QStringLiteral("Scala temporanea di regressione"));
                applied=operationError.isEmpty();result["synthetic_scale"]=true;field="temporary.scaleFactor";old=1.0;next=factor;break;
            }
        }
        result["field"]=field;result["old_value"]=old;result["new_value"]=next;
    }else if(scenario=="suppress"){
        for(int i=base.extrusions.size()-1;i>=0;--i)if(base.extrusions[i].modelBodyId&&!base.extrusions[i].suppressed){
            target=i;operationError=view.setFeatureSuppressed(i,true);applied=operationError.isEmpty();break;}
    }else if(scenario=="reorder"){
        QJsonArray rejections;
        for(int i=base.extrusions.size()-1;i>=1&&!applied;--i){
            if(!base.extrusions[i].modelBodyId||base.extrusions[i].suppressed)continue;
            target=i;operationError=view.moveFeature(i,-1);applied=operationError.isEmpty();
            if(!applied)rejections.append(QJsonObject{{"feature_id",QString::number(base.extrusions[i].featureId)},{"error",operationError}});
        }
        result["rejections"]=rejections;
    }else throw std::runtime_error("scenario sconosciuto");
    result["target_index"]=target;
    if(target>=0){result["target_feature_id"]=QString::number(base.extrusions[target].featureId);result["target_name"]=base.extrusions[target].name;}
    result["operation_error"]=operationError;
    if(!applied){auto unchanged=snapshot(view.currentDocument());result["rejected_changes"]=compare(before,unchanged);
        result["status"]=target<0?"not_applicable":(result["rejected_changes"].toArray().isEmpty()?"rejected_atomic":"rejected_nonatomic");save(output,result);return;}
    auto mutated=snapshot(view.currentDocument());result["after"]=mutated;result["mutation_changes"]=compare(before,mutated);save(output,result);
    progress("rigenerazione della variante");view.rebuildAllFeatures();auto repeated=snapshot(view.currentDocument());
    result["repeat"]=repeated;result["repeat_changes"]=compare(mutated,repeated);save(output,result);
    progress("ripristino della feature");
    if(scenario=="suppress")operationError=view.setFeatureSuppressed(target,false);
    else if(scenario=="reorder"){
        int moved=-1;for(int i=0;i<view.extrusions().size();++i)if(view.extrusions()[i].featureId==base.extrusions[target].featureId)moved=i;
        operationError=view.moveFeature(moved,1);
    }else if(result["synthetic_scale"].toBool())view.deleteFeature(view.extrusions().size()-1);
    else operationError=view.updateBody(target,base.extrusions[target]);
    result["restore_error"]=operationError;
    view.rebuildAllFeatures();auto restored=snapshot(view.currentDocument());result["restored"]=restored;
    result["restore_changes"]=compare(before,restored);
    result["status"]=(!operationError.isEmpty()||!result["restore_changes"].toArray().isEmpty()||!result["repeat_changes"].toArray().isEmpty())?"unstable":(restored["incomplete_metrics"].toInt()?"stable_with_limits":"stable");
    save(output,result);
}
}
#endif
