#pragma once
#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>
#include <QToolBar>

namespace ForgeCad {
// Aggiornamento dei percorsi salvati per i comandi di schizzo sostituiti.
inline void migrateSketchToolCommands() {
    // Aggiornare anche i comandi salvati nelle barre e nella ribbon personalizzate.
    // Le chiavi sono percorsi completi: le Linee delle tavole restano distinte.
    const QString oldLine=QStringLiteral("Schizzo/Strumento geometrico/Linea");
    const QString newLine=QStringLiteral("Schizzo/Strumento geometrico/Polilinea");
    std::function<QJsonValue(const QJsonValue &)> migrateLine;
    migrateLine=[&](const QJsonValue &value)->QJsonValue {
        if(value.isString()) {
            const QString key=value.toString();
            if(key==oldLine)return newLine;
            if(key==QStringLiteral("command/")+oldLine)return QStringLiteral("command/")+newLine;
            if(key==QStringLiteral("Schizzo/Disegna segmenti"))return QStringLiteral("Schizzo/Disegna polilinea");
            if(key==QStringLiteral("command/Schizzo/Disegna segmenti"))return QStringLiteral("command/Schizzo/Disegna polilinea");
            if(key.startsWith(QStringLiteral("group/"))) {
                const auto data=QJsonDocument::fromJson(QByteArray::fromBase64(key.mid(6).toLatin1()));
                if(data.isArray())return QStringLiteral("group/")+QString::fromLatin1(
                    QJsonDocument(migrateLine(data.array()).toArray()).toJson(QJsonDocument::Compact).toBase64());
            }
        } else if(value.isArray()) {
            QJsonArray result;
            for(const auto &item:value.toArray()) {
                const auto next=migrateLine(item);
                if(next.isString() && (next.toString()==newLine || next.toString()==QStringLiteral("command/")+newLine)
                    && result.contains(next))continue;
                result.append(next);
            }
            return result;
        } else if(value.isObject()) {
            auto result=value.toObject();
            for(auto it=result.begin();it!=result.end();++it)it.value()=migrateLine(it.value());
            return result;
        }
        return value;
    };
    QSettings toolSettings;
    for(const auto &key:toolSettings.allKeys()) {
        if(!key.startsWith(QStringLiteral("toolbar/")) && key!=QStringLiteral("ribbon/layout"))continue;
        const auto value=toolSettings.value(key);
        if(value.metaType().id()==QMetaType::QStringList) {
            const auto next=migrateLine(QJsonArray::fromStringList(value.toStringList())).toArray();
            QStringList keys;for(const auto &entry:next)keys.append(entry.toString());
            if(keys!=value.toStringList())toolSettings.setValue(key,keys);
        } else if(value.metaType().id()==QMetaType::QByteArray) {
            const auto data=QJsonDocument::fromJson(value.toByteArray());
            if(data.isArray()) {
                const auto next=migrateLine(data.array()).toArray();
                if(next!=data.array())toolSettings.setValue(key,QJsonDocument(next).toJson(QJsonDocument::Compact));
            }
        }
    }
}

// Stessa densita' e preferenza di blocco per modellazione e tavole.
inline void configureCommandBar(QToolBar *bar) {
    bar->setMovable(!QSettings().value(QStringLiteral("toolbar/locked"), false).toBool());
    bar->setFloatable(bar->isMovable());
    bar->setIconSize(QSize(24, 24));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
}
}
