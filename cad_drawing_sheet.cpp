#include "cad_drawing_sheet.h"
#include "cad_drawing_dimension.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace ForgeCad {
namespace {
void require(bool condition,const char *message){if(!condition)throw std::invalid_argument(message);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
bool finite(const QPointF &point){return std::isfinite(point.x())&&std::isfinite(point.y())&&std::abs(point.x())<=1e7&&std::abs(point.y())<=1e7;}
QJsonArray pointJson(const QPointF &point){return {point.x(),point.y()};}
QPointF readPoint(const QJsonValue &value){
    require(value.isArray(),"Posizione del foglio assente.");const auto array=value.toArray();
    require(array.size()==2&&array[0].isDouble()&&array[1].isDouble(),"Posizione del foglio non valida.");
    const QPointF point(array[0].toDouble(),array[1].toDouble());require(finite(point),"Posizione del foglio non finita.");return point;
}
QUuid readId(const QJsonValue &value){require(value.isString(),"Identita' del foglio assente.");const QUuid id(value.toString());require(!id.isNull(),"Identita' del foglio non valida.");return id;}
void validate(const DrawingSheet &sheet){
    require(!sheet.title.trimmed().isEmpty()&&sheet.title.size()<=256,"Titolo della tavola non valido.");
    require(std::isfinite(sheet.size.width())&&std::isfinite(sheet.size.height())&&sheet.size.width()>=50&&sheet.size.height()>=50
            &&sheet.size.width()<=2000&&sheet.size.height()<=2000,"Dimensioni del foglio non valide.");
    require(sheet.views.size()==qsizetype(sheet.drawing.views.size())&&sheet.dimensions.size()==qsizetype(sheet.drawing.dimensions.size()),"Presentazione del foglio incompleta.");
    QSet<QUuid> viewIds,dimensionIds;
    for(const auto &view:sheet.drawing.views)viewIds.insert(view.id);
    for(const auto &dimension:sheet.drawing.dimensions)dimensionIds.insert(dimension.id);
    for(const auto &layout:sheet.views){
        require(viewIds.remove(layout.viewId)&&finite(layout.position)&&!layout.title.trimmed().isEmpty()&&layout.title.size()<=256
                &&!layout.features.isEmpty()&&layout.features.size()<=10000,"Presentazione della vista non valida.");
        QSet<quint64> features;for(auto id:layout.features){require(id&&!features.contains(id),"Sorgente della vista nulla o duplicata.");features.insert(id);}
    }
    for(const auto &layout:sheet.dimensions)require(dimensionIds.remove(layout.dimensionId)&&finite(layout.position),"Presentazione della quota non valida.");
    for(const auto &dimension:sheet.drawing.dimensions){
        const auto layout=std::find_if(sheet.views.cbegin(),sheet.views.cend(),[&](const auto &view){return view.viewId==dimension.viewId;});
        require(layout!=sheet.views.cend()&&layout->features.contains(dimension.section.featureId),"La quota riferisce un corpo estraneo alla vista.");
        for(const auto &ref:dimension.references)if(!drawingDatum(ref.role))require(layout->features.contains(ref.curve.featureId),"Un aggancio della quota riferisce un corpo estraneo alla vista.");
    }
}
}
QString encodeDrawingSheet(const DrawingSheet &sheet,QByteArray &bytes){
    try{
        validate(sheet);QByteArray document;ok(encodeDrawingDocument(sheet.drawing,document));QJsonArray views,dimensions;
        for(const auto &view:sheet.views){QJsonArray features;for(auto id:view.features)features.append(QString::number(id));
            QJsonObject object{{"id",view.viewId.toString(QUuid::WithoutBraces)},{"title",view.title},{"position",pointJson(view.position)},{"features",features}};
            if(view.showHiddenLines)object.insert("showHiddenLines",true);
            views.append(object);}
        for(const auto &dimension:sheet.dimensions)dimensions.append(QJsonObject{{"id",dimension.dimensionId.toString(QUuid::WithoutBraces)},{"position",pointJson(dimension.position)}});
        auto prepared=QJsonDocument(QJsonObject{{"format","ForgeCAD.DrawingSheet"},{"schema",1},{"title",sheet.title},
            {"size",QJsonArray{sheet.size.width(),sheet.size.height()}},{"drawing",QJsonDocument::fromJson(document).object()},
            {"views",views},{"dimensions",dimensions}}).toJson(QJsonDocument::Compact);
        require(prepared.size()<=8*1024*1024,"Foglio oltre i limiti del prototipo.");bytes=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString decodeDrawingSheet(const QByteArray &bytes,DrawingSheet &sheet){
    try{
        require(!bytes.isEmpty()&&bytes.size()<=8*1024*1024,"Dimensione del foglio non valida.");
        const auto json=QJsonDocument::fromJson(bytes);const auto object=json.object();
        require(json.isObject()&&object["format"].toString()==QLatin1String("ForgeCAD.DrawingSheet")&&object["schema"].toInt(-1)==1,"Formato del foglio non supportato.");
        require(object["title"].isString()&&object["drawing"].isObject()&&object["views"].isArray()&&object["dimensions"].isArray(),"Dati del foglio incompleti.");
        DrawingSheet prepared;prepared.title=object["title"].toString();const auto size=readPoint(object["size"]);prepared.size=QSizeF(size.x(),size.y());
        ok(decodeDrawingDocument(QJsonDocument(object["drawing"].toObject()).toJson(QJsonDocument::Compact),prepared.drawing));
        require(object["views"].toArray().size()<=10000&&object["dimensions"].toArray().size()<=10000,"Troppi elementi nel foglio.");
        for(const auto &value:object["views"].toArray()){
            require(value.isObject(),"Vista del foglio non valida.");const auto view=value.toObject();DrawingViewLayout layout;
            layout.viewId=readId(view["id"]);require(view["title"].isString()&&view["features"].isArray(),"Vista senza titolo o sorgenti.");
            layout.title=view["title"].toString();layout.position=readPoint(view["position"]);
            require(!view.contains("showHiddenLines")||view["showHiddenLines"].isBool(),"Visibilita' delle linee nascoste non valida.");layout.showHiddenLines=view["showHiddenLines"].toBool(false);
            const auto features=view["features"].toArray();require(features.size()<=10000,"Troppe sorgenti nella vista.");
            for(const auto &feature:features){require(feature.isString(),"ID sorgente non valido.");bool valid=false;const auto id=feature.toString().toULongLong(&valid);require(valid,"ID sorgente non valido.");layout.features.append(id);}
            prepared.views.append(std::move(layout));
        }
        for(const auto &value:object["dimensions"].toArray()){require(value.isObject(),"Quota del foglio non valida.");const auto dimension=value.toObject();
            prepared.dimensions.append({readId(dimension["id"]),readPoint(dimension["position"])});}
        validate(prepared);sheet=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
}
