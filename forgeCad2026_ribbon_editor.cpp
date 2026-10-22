#include "forgeCad2026_ribbon_editor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <functional>

namespace ForgeCad {
bool editRibbonLayout(QWidget *parent,QJsonArray &layout,const QJsonArray &defaults,const QMap<QString,QString> &commands) {
    QJsonArray draft=layout;
    QDialog dialog(parent);dialog.setObjectName("ribbonEditor");dialog.setWindowTitle(QStringLiteral("Personalizza ribbon"));dialog.resize(1000,600);
    auto *root=new QVBoxLayout(&dialog);
    auto *pages=new QComboBox(&dialog);pages->setObjectName("ribbonEditorTabs");root->addWidget(pages);
    for(const auto &v:draft)pages->addItem(v.toObject()["title"].toString());
    auto *columns=new QHBoxLayout;root->addLayout(columns,1);
    auto *sections=new QListWidget(&dialog);sections->setObjectName("ribbonEditorSections");
    auto *items=new QListWidget(&dialog);items->setObjectName("ribbonEditorItems");
    auto *available=new QListWidget(&dialog);available->setObjectName("ribbonEditorCommands");
    auto panel=[&](const QString &title,QListWidget *list){auto *col=new QVBoxLayout;col->addWidget(new QLabel(title,&dialog));col->addWidget(list,1);columns->addLayout(col,1);return col;};
    auto *left=panel(QStringLiteral("Sezioni (spunta per mostrare)"),sections);
    auto *middle=panel(QStringLiteral("Comandi della sezione"),items);
    auto *right=panel(QStringLiteral("Comandi disponibili"),available);
    auto *filter=new QLineEdit(&dialog);filter->setPlaceholderText(QStringLiteral("Cerca comando…"));right->insertWidget(1,filter);
    for(auto it=commands.cbegin();it!=commands.cend();++it){auto *item=new QListWidgetItem(it.value(),available);item->setData(Qt::UserRole,it.key());item->setToolTip(it.key());}
    QObject::connect(filter,&QLineEdit::textChanged,&dialog,[&](const QString &value){for(int i=0;i<available->count();++i)available->item(i)->setHidden(!available->item(i)->toolTip().contains(value,Qt::CaseInsensitive));});
    auto addButton=[&](QVBoxLayout *col,const QString &label,const QString &id,std::function<void()> callback){auto *b=new QPushButton(label,&dialog);b->setObjectName(id);col->addWidget(b);QObject::connect(b,&QPushButton::clicked,&dialog,std::move(callback));return b;};
    auto *promoted=new QSpinBox(&dialog);promoted->setObjectName("ribbonPromotedCount");promoted->setRange(0,100);promoted->setPrefix(QStringLiteral("Pulsanti principali: "));middle->addWidget(promoted);
    auto *divider=new QCheckBox(QStringLiteral("Separatore dopo la sezione"),&dialog);divider->setObjectName("ribbonSectionSeparator");middle->addWidget(divider);
    bool filling=false;
    auto groups=[&]{return draft[pages->currentIndex()].toObject()["groups"].toArray();};
    auto group=[&]{auto g=groups();return sections->currentRow()>=0 && sections->currentRow()<g.size()?g[sections->currentRow()].toObject():QJsonObject{};};
    auto store=[&](const QJsonObject &value){auto p=draft[pages->currentIndex()].toObject();auto g=p["groups"].toArray();if(sections->currentRow()<0)return;g[sections->currentRow()]=value;p["groups"]=g;draft[pages->currentIndex()]=p;};
    std::function<void()> fillItems=[&]{filling=true;items->clear();auto g=group();for(const auto &v:g["items"].toArray()){const auto id=v.toString();auto *item=new QListWidgetItem(id.isEmpty()?QStringLiteral("── Separatore ──"):commands.value(id,id),items);item->setData(Qt::UserRole,id);}promoted->setValue(g["promoted"].toInt(3));divider->setChecked(g["divider"].toBool());const bool editable=!g.isEmpty()&&!g["finish"].toBool();items->setEnabled(editable);promoted->setEnabled(editable);divider->setEnabled(editable);filling=false;};
    std::function<void(int)> fillSections=[&](int row){filling=true;sections->clear();for(const auto &v:groups()){auto g=v.toObject();auto *item=new QListWidgetItem(g["name"].toString(),sections);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(g["visible"].toBool(true)?Qt::Checked:Qt::Unchecked);if(g["finish"].toBool())item->setFlags(item->flags()&~Qt::ItemIsUserCheckable);}filling=false;sections->setCurrentRow(qMin(qMax(0,row),sections->count()-1));fillItems();};
    auto storeItems=[&]{auto g=group();if(g.isEmpty()||g["finish"].toBool())return;QJsonArray a;for(int i=0;i<items->count();++i)a.append(items->item(i)->data(Qt::UserRole).toString());g["items"]=a;g["promoted"]=promoted->value();g["divider"]=divider->isChecked();store(g);};
    QObject::connect(pages,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&]{fillSections(0);});
    QObject::connect(sections,&QListWidget::currentRowChanged,&dialog,[&]{if(!filling)fillItems();});
    QObject::connect(sections,&QListWidget::itemChanged,&dialog,[&](QListWidgetItem *item){if(filling)return;auto p=draft[pages->currentIndex()].toObject();auto a=p["groups"].toArray();auto g=a[sections->row(item)].toObject();g["visible"]=item->checkState()==Qt::Checked;a[sections->row(item)]=g;p["groups"]=a;draft[pages->currentIndex()]=p;});
    QObject::connect(promoted,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&]{if(!filling)storeItems();});
    QObject::connect(divider,&QCheckBox::toggled,&dialog,[&]{if(!filling)storeItems();});
    addButton(left,QStringLiteral("Aggiungi sezione…"),"ribbonAddSection",[&]{bool ok=false;auto name=QInputDialog::getText(&dialog,QStringLiteral("Nuova sezione"),QStringLiteral("Nome:"),QLineEdit::Normal,{},&ok).trimmed();if(!ok||name.isEmpty())return;auto p=draft[pages->currentIndex()].toObject();auto a=groups();a.append(QJsonObject{{"name",name},{"items",QJsonArray{}},{"visible",true},{"promoted",3}});p["groups"]=a;draft[pages->currentIndex()]=p;fillSections(a.size()-1);});
    addButton(left,QStringLiteral("Rinomina…"),"ribbonRenameSection",[&]{auto g=group();if(g.isEmpty()||g["finish"].toBool())return;bool ok=false;auto name=QInputDialog::getText(&dialog,QStringLiteral("Rinomina sezione"),QStringLiteral("Nome:"),QLineEdit::Normal,g["name"].toString(),&ok).trimmed();if(ok&&!name.isEmpty()){g["name"]=name;store(g);fillSections(sections->currentRow());}});
    addButton(left,QStringLiteral("Rimuovi sezione"),"ribbonRemoveSection",[&]{if(group().isEmpty()||group()["finish"].toBool())return;auto p=draft[pages->currentIndex()].toObject();auto a=groups();int row=sections->currentRow();a.removeAt(row);p["groups"]=a;draft[pages->currentIndex()]=p;fillSections(row);});
    for(int delta:{-1,1})addButton(left,delta<0?QStringLiteral("Sposta su"):QStringLiteral("Sposta giù"),delta<0?"ribbonSectionUp":"ribbonSectionDown",[&,delta]{int row=sections->currentRow(),next=row+delta;auto a=groups();if(row<0||next<0||next>=a.size())return;auto v=a.takeAt(row);a.insert(next,v);auto p=draft[pages->currentIndex()].toObject();p["groups"]=a;draft[pages->currentIndex()]=p;fillSections(next);});
    auto insert=[&](const QString &id){if(group().isEmpty()||group()["finish"].toBool())return;int row=items->currentRow()+1;auto *item=new QListWidgetItem(id.isEmpty()?QStringLiteral("── Separatore ──"):commands.value(id,id));item->setData(Qt::UserRole,id);items->insertItem(row,item);items->setCurrentRow(row);promoted->setValue(qMax(promoted->value()+1,row+1));storeItems();};
    addButton(right,QStringLiteral("← Aggiungi comando"),"ribbonAddCommand",[&]{if(available->currentItem())insert(available->currentItem()->data(Qt::UserRole).toString());});
    addButton(middle,QStringLiteral("Inserisci separatore"),"ribbonAddSeparator",[&]{insert({});});
    addButton(middle,QStringLiteral("Rimuovi comando"),"ribbonRemoveCommand",[&]{if(group()["finish"].toBool()||items->currentRow()<0)return;delete items->takeItem(items->currentRow());storeItems();});
    for(int delta:{-1,1})addButton(middle,delta<0?QStringLiteral("Sposta su"):QStringLiteral("Sposta giù"),delta<0?"ribbonItemUp":"ribbonItemDown",[&,delta]{int row=items->currentRow(),next=row+delta;if(group()["finish"].toBool()||row<0||next<0||next>=items->count())return;auto *item=items->takeItem(row);items->insertItem(next,item);items->setCurrentRow(next);storeItems();});
    auto *hint=new QLabel(QStringLiteral("I pulsanti principali appaiono nella ribbon; tutti i comandi restano nel menu della sezione. Termina schizzo resta sempre disponibile."),&dialog);hint->setWordWrap(true);root->addWidget(hint);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::RestoreDefaults,&dialog);root->addWidget(buttons);buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Conferma"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annulla"));buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("Ripristina disposizione iniziale"));buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName("ribbonReset");
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&]{draft=defaults;fillSections(0);});
    fillSections(0);
    if(dialog.exec()!=QDialog::Accepted)return false;
    layout=draft;return true;
}
}
