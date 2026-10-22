#include "forgeCad2026_ribbon_preview.h"
#include "forgeCad2026_ribbon_editor.h"
#include "forgeCad2026_dialogs_common.h"
#include <QSizeGrip>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFrame>
#include <QListWidget>
#include <QCheckBox>
#include "forgeCad2026_viewport.h"
#include "cad_icons.h"
#include "cad_menu.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QKeyEvent>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QStyleOptionTab>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QFontInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QConicalGradient>
#include <QGraphicsOpacityEffect>
#include <QRegion>
#include <QPointer>
#include <QOpenGLFunctions>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ForgeCad {
namespace {
QString text(QString value) {return value.remove('&').trimmed();}
// Maschera e contorno condividono sagoma e campionamento: una sola curva
// esterna, rasterizzata a 4x prima della riduzione alla risoluzione del display.
QImage rasterizeCards(const QSize &logicalSize,qreal dpr,const QRect &workspace,const QRect &card,int radius,bool outline) {
    constexpr int samples=4;
    const QSize pixels(qCeil(logicalSize.width()*dpr),qCeil(logicalSize.height()*dpr));
    QImage high(pixels*samples,QImage::Format_ARGB32_Premultiplied);
    high.setDevicePixelRatio(dpr*samples);high.fill(Qt::transparent);
    QPainter painter(&high);painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(outline?QBrush(Qt::NoBrush):QBrush(Qt::white));
    painter.setPen(outline?QPen(Qt::white,0.35):QPen(Qt::NoPen));
    const qreal inset=outline?1.5:0;
    for(const auto &bounds:{workspace,card})if(!bounds.isEmpty()) {
        const QRectF edge=QRectF(bounds).adjusted(inset,inset,-inset,-inset);
        const qreal r=qMax(0.0,qMin(qreal(radius)-inset,qMin(edge.width(),edge.height())/2));
        painter.drawRoundedRect(edge,r,r);
    }
    painter.end();
    auto image=high.scaled(pixels,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    image.setDevicePixelRatio(dpr);return image;
}

struct Entry {QAction *action=nullptr;QString path;};
void collect(QMenu *menu,const QString &prefix,QVector<Entry> &out) {
    for(auto *action:menu->actions()) {
        if(action->isSeparator() || action->property("toolbarConfiguration").toBool())continue;
        const auto path=prefix+text(action->text());
        if(action->menu())collect(action->menu(),path+'/',out);
        else out.append({action,path});
    }
}
QString caption(QAction *action) {
    auto label=text(action->text());label.remove("...");label.remove(QChar(0x2026));
    if(label.startsWith("Vista in sezione"))return QStringLiteral("Vista in\nsezione");
    if(label.startsWith("Esci dalla"))return QStringLiteral("Termina\nschizzo");
    const QHash<QString,QString> shortLabels={{"Estrusione","Estrudi"},{"Parallelepipedo","Blocco"},
        {"Solo linee esterne","Linee\nesterne"},{"Mesh + linee esterne","Mesh +\nlinee"},{"Piani di riferimento","Piani"},
        {"Raccordo spigoli","Raccordo"},{"Smusso spigoli","Smusso"},{"Sposta / ruota corpo","Sposta /\nruota"},
        {"Ripetizione lineare","Serie\nlineare"},{"Ripetizione circolare","Serie\ncircolare"},{"Intersezione","Interseca"},
        {"Differenza A - B","Differenza"}};
    if(shortLabels.contains(label))return shortLabels.value(label);
    return label.section('(',0,0).trimmed();
}
QString buttonText(QAction *action,int width,const QFont &font) {
    const auto label=caption(action);const QFontMetrics fm(font);const int room=qMax(0,width-8);
    QStringList lines=label.split('\n');
    if(lines.size()==1 && fm.horizontalAdvance(label)>room) {
        const auto words=label.split(' ',Qt::SkipEmptyParts);QString first;
        int next=0;
        while(next<words.size()-1) {
            const auto candidate=first.isEmpty()?words[next]:first+' '+words[next];
            if(!first.isEmpty() && fm.horizontalAdvance(candidate)>room)break;
            first=candidate;++next;
        }
        if(next>0)lines={first,words.mid(next).join(' ')};
    }
    if(lines.size()>2)lines={lines.first(),lines.mid(1).join(' ')};
    for(auto &line:lines)line=fm.elidedText(line,Qt::ElideRight,room);
    return lines.join('\n');
}
// Conserva le etichette e la sovrapposizione nativa della scheda attiva,
// omettendo soltanto il bordo sinistro del primo slot.
class RibbonTabBar final : public QTabBar {
public:
    using QTabBar::QTabBar;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        for(int pass=0;pass<2;++pass)for(int i=0;i<count();++i) {
            if(!isTabVisible(i) || (i==currentIndex())!=(pass==1))continue;
            QStyleOptionTab label;initStyleOption(&label,i);auto shape=label;
            if(i==currentIndex())shape.rect.adjust(0,0,0,style()->pixelMetric(QStyle::PM_TabBarBaseOverlap,&shape,this));
            painter.save();
            if(i==0) {
                const QRect left(shape.rect.left(),shape.rect.top(),2,shape.rect.height());
                painter.setClipRegion(QRegion(shape.rect).subtracted(QRegion(left)),Qt::IntersectClip);
            }
            style()->drawControl(QStyle::CE_TabBarTabShape,&shape,&painter,this);
            painter.restore();style()->drawControl(QStyle::CE_TabBarTabLabel,&label,&painter,this);
        }
    }
};
// Il renderer nativo di QToolButton puo' aggiungere altri a-capo, anche
// dopo l'elisione. Disegniamo il testo in due rettangoli espliciti.
class RibbonCommandButton final : public QToolButton {
public:
    using QToolButton::QToolButton;
protected:
    void paintEvent(QPaintEvent *event) override {
        if(toolButtonStyle()==Qt::ToolButtonIconOnly){QToolButton::paintEvent(event);return;}
        QStyleOptionToolButton option;initStyleOption(&option);
        const QIcon image=option.icon;option.icon=QIcon();option.text.clear();
        QPainter painter(this);painter.setClipRect(rect());
        style()->drawComplexControl(QStyle::CC_ToolButton,&option,&painter,this);
        const QFontMetrics fm(font());const int lineHeight=fm.height();
        const int iconSide=qMax(0,qMin(28,height()-2*lineHeight-6));
        image.paint(&painter,QRect((width()-iconSide)/2,2,iconSide,iconSide),Qt::AlignCenter,
                    isEnabled()?QIcon::Normal:QIcon::Disabled,isChecked()?QIcon::On:QIcon::Off);
        painter.setFont(font());painter.setPen(palette().color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::ButtonText));
        const auto lines=text().split('\n');const int top=iconSide+4;
        for(int i=0;i<qMin(2,int(lines.size()));++i)
            painter.drawText(QRect(4,top+i*lineHeight,width()-8,lineHeight),Qt::AlignHCenter|Qt::AlignVCenter|Qt::TextSingleLine,lines[i]);
    }
};
}
// Sovrapposizione non interattiva: il tratto resta sopra la scheda Qt.
class RibbonCornerAccent final : public QWidget {
    QTabBar *tabs_;
    QRect card_,workspace_;
    QImage border_;
    qreal borderDpr_=0;
    int radius_=-1;
public:
    RibbonCornerAccent(QWidget *parent,QTabBar *tabs):QWidget(parent),tabs_(tabs) {
        setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        connect(tabs,&QTabBar::currentChanged,this,[this]{update();});
    }
    void setContours(const QRect &workspace,const QRect &card,int radius) {
        if(workspace_==workspace && card_==card && radius_==radius)return;
        workspace_=workspace;card_=card;radius_=radius;border_=QImage();update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        const qreal dpr=devicePixelRatioF();
        if(border_.isNull() || borderDpr_!=dpr) {
            border_=rasterizeCards(size(),dpr,workspace_,card_,radius_,true);borderDpr_=dpr;
        }
        painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(QPointF(),border_);
        painter.setBrush(Qt::NoBrush);
        const int selected=tabs_->currentIndex();if(selected<0)return;
        const QPoint origin=mapFromParent(tabs_->mapTo(parentWidget(),QPoint()));
        const auto tab=tabs_->tabRect(selected).translated(origin);
        const QColor accent(45,170,230);
        painter.setPen(QPen(accent,3,Qt::SolidLine,Qt::FlatCap));
        const qreal start=selected==0?qMax(qreal(tab.left()+2),qreal(card_.left()+radius_)):tab.left()+2;
        painter.save();painter.setClipRect(QRect(origin,tabs_->size()));
        painter.drawLine(QPointF(start,card_.top()+1.5),QPointF(tab.right()-2,card_.top()+1.5));painter.restore();
        if(radius_<=1 || selected!=0 || tabs_->tabRect(0).left()<0)return;
        const QRectF edge=QRectF(card_).adjusted(1.5,1.5,-1.5,-1.5);
        const qreal r=qMin(qreal(radius_-1.5),qMin(edge.width(),edge.height())/2);
        // L'alpha dipende dall'angolo, non dalla distanza diagonale:
        // rimane uniforme attraverso i tre pixel di spessore.
        QConicalGradient fade(QPointF(edge.left()+r,edge.top()+r),90);
        auto transparent=accent;transparent.setAlpha(0);
        fade.setColorAt(0,accent);fade.setColorAt(.18,accent);fade.setColorAt(.25,transparent);fade.setColorAt(1,transparent);
        QPainterPath curve;const QRectF arc(edge.topLeft(),QSizeF(2*r,2*r));
        curve.arcMoveTo(arc,90);curve.arcTo(arc,90,90);
        painter.setPen(QPen(QBrush(fade),3,Qt::SolidLine,Qt::FlatCap));painter.drawPath(curve);
    }
};
struct RibbonPreview::Impl {
    struct Group {QString name;QVector<QAction *> actions;bool finish=false;bool divider=false;int promoted=3;};
    struct Tab {QString id,label;QVector<Group> groups;};
    struct Plan {int shown=0,width=0;bool collapsed=false,more=false;};
    RibbonPreview *self;
    QMainWindow *window;
    CadViewport *viewport=nullptr;
    QVector<Tab> tabs;
    QJsonArray defaults;
    QTabBar *tabBar=nullptr;
    RibbonCornerAccent *cornerAccent=nullptr;
    QGraphicsOpacityEffect *roundedEffect=nullptr;
    QSize maskPixels;
    qreal maskDpr=0;
    int maskRadius=-1;
    QWidget *groupsHost=nullptr,*workspace=nullptr;
    QLabel *workspaceTitle=nullptr;
    QHBoxLayout *groupsLayout=nullptr;
    QVBoxLayout *bodyLayout=nullptr;
    int appliedPadding=-1;
    QToolButton *density=nullptr;
    QTimer contextTimer;
    QPointer<QAction> running;
    QPointer<QDialog> panel;
    QHash<QAction *,QString> paths;
    bool compact=false,sketch=false,layingOut=false,scheduled=false;
    QString lastOrdinary=QStringLiteral("model");
    // Due regioni indipendenti: il distacco tra le carte lascia la scena intatta.
    QRect glassRects[2];
    bool glassRegistered=false;
    int opacity=88,blur=12,radius=10;
    QElapsedTimer appearanceTimer;
    quintptr glassId(int i) const {return reinterpret_cast<quintptr>(&glassRects[i]);}
    // Il rettangolo inscritto nel quarto di cerchio richiede r*(1-1/sqrt(2))
    // su entrambi gli assi; due punti aggiuntivi separano i contenuti dal bordo.
    int contentPadding() const {return radius>0?qCeil(radius*(1.0-1.0/std::sqrt(2.0)))+2:0;}
    int workspaceWidth() const {return 88+2*contentPadding();}
    int ribbonHeight() const {return (compact?95:123)+2*contentPadding();}
    QRect cardRect(int i) const {
        const int width=workspaceWidth(),left=width+8;
        return i==0?QRect(0,0,width,self->height()):QRect(left,0,qMax(0,self->width()-left),self->height());
    }
    void syncPadding() {
        const int padding=contentPadding();
        if(!bodyLayout || !groupsLayout || !workspace || appliedPadding==padding)return;
        appliedPadding=padding;
        workspace->setFixedWidth(workspaceWidth());
        workspace->layout()->setContentsMargins(6+padding,8+padding,6+padding,6+padding);
        // Le schede restano a filo della carta; il margine superiore
        // protegge soltanto i gruppi di comandi, senza separare l'accento.
        bodyLayout->setContentsMargins(padding,0,padding,padding);
        groupsLayout->setContentsMargins(0,padding,0,0);
        self->resize(self->width(),ribbonHeight());
        schedule();
    }
    void clearGlass() {
        if(!glassRegistered)return;
        for(int i=0;i<2;++i)viewport->removeGlassPanel(glassId(i));
        glassRegistered=false;
    }
    void syncGlass() {
        bool appearanceChanged=false;
        if(!appearanceTimer.isValid() || appearanceTimer.elapsed()>=500) {
            QSettings settings;
            const int nextOpacity=qBound(25,settings.value("view/functionPanelOpacity",settings.value("loft/dialogOpacity",88)).toInt(),100);
            const int nextBlur=qBound(0,settings.value("view/functionPanelBlur",12).toInt(),40);
            const int nextRadius=qBound(0,settings.value("view/functionPanelCornerRadius",10).toInt(),40);
            appearanceChanged=opacity!=nextOpacity || blur!=nextBlur || radius!=nextRadius;
            opacity=nextOpacity;blur=nextBlur;radius=nextRadius;appearanceTimer.restart();
            if(appearanceChanged)self->update();
        }
        syncPadding();
        // Maschera alpha a risoluzione fisica: QRegion tagliava il bordo
        // antialias del tratto, creando gradini e spessore variabile.
        const qreal dpr=self->devicePixelRatioF();
        const QSize pixels(qCeil(self->width()*dpr),qCeil(self->height()*dpr));
        if(roundedEffect && (maskPixels!=pixels || maskDpr!=dpr || maskRadius!=radius)) {
            const auto mask=rasterizeCards(self->size(),dpr,cardRect(0),cardRect(1),radius,false);
            roundedEffect->setOpacityMask(QBrush(mask));
            maskPixels=pixels;maskDpr=dpr;maskRadius=radius;
        }
        if(cornerAccent) {
            cornerAccent->setGeometry(self->rect());cornerAccent->setContours(cardRect(0),cardRect(1),radius);cornerAccent->raise();
        }
        if(!self->isVisible() || opacity>=100){clearGlass();return;}
        for(int i=0;i<2;++i) {
            const QRect area=cardRect(i).translated(self->pos());
            if(!glassRegistered || appearanceChanged || glassRects[i]!=area) {
                glassRects[i]=area;
                viewport->setGlassPanel(glassId(i),area,blur,radius,true);
            }
        }
        glassRegistered=true;
    }
    explicit Impl(RibbonPreview *owner,QMainWindow *w):self(owner),window(w) {
        for(auto *gl:w->findChildren<QOpenGLWidget *>())if(auto *v=dynamic_cast<CadViewport *>(gl)){viewport=v;break;}
        if(!viewport)throw std::runtime_error("Viewport non disponibile per la prova ribbon");
        QVector<Entry> entries;
        for(auto *action:w->menuBar()->actions())if(action->menu())collect(action->menu(),text(action->text())+'/',entries);
        tabs={{"model",QStringLiteral("Modello"),{}},{"surface",QStringLiteral("Superfici"),{}},
              {"sketch",QStringLiteral("Schizzo"),{}},{"inspect",QStringLiteral("Analisi"),{}},{"view",QStringLiteral("Vista"),{}}};
        for(const auto &entry:entries) {
            paths.insert(entry.action,entry.path);
            const auto &p=entry.path;const auto label=text(entry.action->text());
            if(p.startsWith("Funzioni/Superfici/")) {
                const bool edit=label.startsWith("Taglia")||label.startsWith("Estendi")||label.startsWith("Offset")||label.startsWith("Elimina")||label.startsWith("Copia");
                add("surface",label.startsWith("Cuci")||label.startsWith("Spessore")?QStringLiteral("Unisci / converti"):edit?QStringLiteral("Modifica"):QStringLiteral("Crea"),entry.action);
            } else if(p.startsWith("Funzioni/")) {
                const auto group=p.contains("/Booleane/")?QStringLiteral("Booleane"):p.contains("/Ripetizione/")?QStringLiteral("Ripeti"):
                    label.contains("Raccordo")||label.contains("Smusso")||label.contains("Svuota")||label.contains("Sformo")||label.contains("Filett")?QStringLiteral("Modifica"):
                    label.contains("Sposta")||label.contains("Scala")?QStringLiteral("Trasforma"):QStringLiteral("Crea");
                add("model",group,entry.action);
                if(label.startsWith("Estrusione")||label.startsWith("Rivoluzione"))add("sketch",QStringLiteral("Solido"),entry.action);
            } else if(p.startsWith("Schizzo/")) {
                if(label.startsWith("Nuovo schizzo")||label.contains("su faccia")||label.contains("spaziale")||label.contains("Piano di riferimento")) {
                    add("model",QStringLiteral("Schizzi"),entry.action);
                    add("sketch",QStringLiteral("Crea schizzo"),entry.action);
                }
                if(label.startsWith("Esci dalla"))add("sketch",QStringLiteral("Termina"),entry.action,true);
                else if(p.contains("Strumento geometrico/"))add("sketch",QStringLiteral("Disegna"),entry.action);
                else if(p.contains("Modifica entita'/"))add("sketch",QStringLiteral("Modifica"),entry.action);
                else if(p.contains("Ripetizione/"))add("sketch",QStringLiteral("Ripeti"),entry.action);
                else if(p.contains("Riferimenti esterni/"))add("sketch",QStringLiteral("Riferimenti"),entry.action);
                else if(!label.startsWith("Nuovo")&&!label.contains("spaziale")&&!label.contains("su faccia"))add("sketch",QStringLiteral("Vincoli / quote"),entry.action);
            } else if(p.startsWith("Analisi/"))add("inspect",label.contains("Misura")?QStringLiteral("Misura"):QStringLiteral("Proprietà / verifiche"),entry.action);
            else if(p.startsWith("Visualizza/Zoom/"))add("view",QStringLiteral("Zoom"),entry.action);
            else if(p.startsWith("Visualizza/Stile visualizzazione/")||p.startsWith("Visualizza/Qualita")||p.startsWith("Visualizza/Luci"))add("view",QStringLiteral("Rappresentazione"),entry.action);
            else if(p.startsWith("Visualizza/")&&(label.startsWith("Vista ")||label.contains("isometrica")||label.contains("trimetrica")))add("view",QStringLiteral("Orientamento"),entry.action);
            else if(p.startsWith("Visualizza/")&&(label.contains("Griglia")||label.contains("Assi")||label.contains("Piani")||label.startsWith("Vista in sezione")))add("view",QStringLiteral("Riferimenti"),entry.action);
        }
        // Ordine esplicito, indipendente dall'ordine dei menu originali.
        const QStringList modelOrder={"Schizzi","Crea","Modifica","Trasforma","Ripeti","Booleane"};
        const QStringList sketchOrder={"Crea schizzo","Disegna","Modifica","Ripeti","Vincoli / quote","Riferimenti","Solido","Termina"};
        for(auto &tab:tabs) {
            const auto order=tab.id=="model"?modelOrder:tab.id=="sketch"?sketchOrder:QStringList{};
            if(!order.isEmpty())std::stable_sort(tab.groups.begin(),tab.groups.end(),[&](const Group &a,const Group &b){return order.indexOf(a.name)<order.indexOf(b.name);});
        }
        for(auto &tab:tabs)for(auto &g:tab.groups)g.promoted=tab.id=="model"?(g.name=="Schizzi"||g.name=="Booleane"?1:g.name=="Trasforma"||g.name=="Ripeti"?2:3):3;
        defaults=layout();
        const auto saved=QJsonDocument::fromJson(QSettings().value("ribbon/layout").toByteArray());if(saved.isArray())applyLayout(saved.array());
    }
    QJsonArray layout() const {
        QJsonArray pages;
        for(const auto &tab:tabs){QJsonArray groups;for(const auto &g:tab.groups){QJsonArray items;for(auto *a:g.actions)items.append(a?paths.value(a):QString{});groups.append(QJsonObject{{"name",g.name},{"items",items},{"finish",g.finish},{"divider",g.divider},{"promoted",g.promoted},{"visible",true}});}pages.append(QJsonObject{{"id",tab.id},{"title",tab.label},{"groups",groups}});}return pages;
    }
    void applyLayout(const QJsonArray &pages) {
        QHash<QString,QAction *> lookup;for(auto it=paths.cbegin();it!=paths.cend();++it)lookup.insert(it.value(),it.key());
        for(auto &tab:tabs)for(const auto &value:pages){const auto page=value.toObject();if(page["id"].toString()!=tab.id || !page["groups"].isArray())continue;
            QVector<Group> groups;Group required;bool hasRequired=false;for(const auto &g:tab.groups)if(g.finish){required=g;hasRequired=true;}
            bool finishAdded=false;
            for(const auto &v:page["groups"].toArray()){const auto g=v.toObject();if(g["finish"].toBool()){if(hasRequired&&!finishAdded){groups.append(required);finishAdded=true;}continue;}
                if(!g["visible"].toBool(true))continue;
                Group group;group.name=g["name"].toString().trimmed().left(100);if(group.name.isEmpty())continue;
                group.divider=g["divider"].toBool();group.promoted=std::clamp(g["promoted"].toInt(3),0,100);
                for(const auto &id:g["items"].toArray()){const auto key=id.toString();if(key.isEmpty())group.actions.append(nullptr);else if(lookup.contains(key))group.actions.append(lookup.value(key));}
                groups.append(group);
            }
            if(hasRequired&&!finishAdded)groups.append(required);
            tab.groups=groups;break;
        }
    }
    void customize() {
        auto stored=QJsonDocument::fromJson(QSettings().value("ribbon/layout").toByteArray());
        QJsonArray draft=defaults;
        if(stored.isArray())for(int i=0;i<draft.size();++i)for(const auto &page:stored.array())if(page.toObject()["id"]==draft[i].toObject()["id"] && page.toObject()["groups"].isArray()){draft[i]=page;break;}
        QMap<QString,QString> commands;for(auto it=paths.cbegin();it!=paths.cend();++it)commands.insert(it.value(),it.value());
        if(editRibbonLayout(window,draft,defaults,commands)){applyLayout(draft);QSettings().setValue("ribbon/layout",QJsonDocument(draft).toJson(QJsonDocument::Compact));schedule();}
    }
    void add(const QString &tabId,const QString &name,QAction *action,bool finish=false) {
        for(auto &tab:tabs)if(tab.id==tabId) {
            auto it=std::find_if(tab.groups.begin(),tab.groups.end(),[&](const Group &g){return g.name==name;});
            if(it==tab.groups.end()){tab.groups.append({name,{},finish});it=std::prev(tab.groups.end());}
            if(!it->actions.contains(action))it->actions.append(action);
        }
    }
    Tab &current() {const auto id=tabBar->tabData(tabBar->currentIndex()).toString();for(auto &t:tabs)if(t.id==id)return t;return tabs[0];}
    void selectTab(const QString &id) {for(int i=0;i<tabBar->count();++i)if(tabBar->tabData(i)==id){tabBar->setCurrentIndex(i);return;}}
    void schedule() {if(scheduled)return;scheduled=true;QTimer::singleShot(0,self,[this]{scheduled=false;rebuild();});}
    void place() {
        const QRect desired(8,8,std::max(1,viewport->width()-16),ribbonHeight());
        if(self->geometry()!=desired)self->setGeometry(desired);
        self->raise();
        syncGlass();
        if(panel && panel->isVisible())placePanel(panel);
    }
    void placePanel(QDialog *dialog) {
        if(layingOut||dialog->parentWidget()!=viewport)return;
        layingOut=true;
        const int top=self->isHidden()?8:self->geometry().bottom()+9;
        const int maxHeight=std::max(1,viewport->height()-top-8);
        // Il pannello resta lo stesso, con selezioni/anteprima e conferma originali.
        if(dialog->height()>maxHeight && dialog->minimumHeight()<=maxHeight)dialog->resize(dialog->width(),maxHeight);
        dialog->move(std::clamp(dialog->x(),8,std::max(8,viewport->width()-dialog->width()-8)),std::max(top,dialog->y()));
        layingOut=false;
    }
    int groupWidth(const Group &group,int count,int buttonWidth,bool collapsed) const {
        int width=8;for(int i=0;i<count;++i)width+=(group.actions[i]?buttonWidth:8)+(i?3:0);
        return (collapsed?72:std::max(72,width))+(group.divider?9:0);
    }
    QVector<Plan> plans(const Tab &tab,int room,int &buttonWidth) const {
        buttonWidth=compact?40:76;
        QVector<Plan> result;
        for(const auto &g:tab.groups) {
            const int promoted=g.promoted;
            result.append({std::min(promoted,int(g.actions.size())),0,promoted==0,false});
        }
        const auto used=[&]{int sum=0;for(int i=0;i<result.size();++i)if(!result[i].more)sum+=std::max(tab.groups[i].finish?80:0,groupWidth(tab.groups[i],result[i].shown,buttonWidth,result[i].collapsed))+1;return sum;};
        while(!compact && buttonWidth>54 && used()>room)--buttonWidth;
        while(used()>room) {
            int pick=-1;
            for(int i=0;i<result.size();++i)if(!tab.groups[i].finish && !result[i].collapsed && result[i].shown>1 && (pick<0||result[i].shown>=result[pick].shown))pick=i;
            if(pick<0)break;
            auto &p=result[pick];--p.shown;if(p.shown==1)p.collapsed=true;
        }
        if(used()>room)for(int i=result.size()-1;i>=0;--i) {
            if(tab.groups[i].finish)continue;
            result[i].more=true;
            if(used()+40<=room)break;
        }
        for(int i=0;i<result.size();++i)result[i].width=std::max(tab.groups[i].finish?80:0,groupWidth(tab.groups[i],result[i].shown,buttonWidth,result[i].collapsed));
        return result;
    }
    QToolButton *button(QAction *action,QWidget *parent,int width,int height) {
        auto *b=new RibbonCommandButton(parent);b->setDefaultAction(action);b->setIconSize(QSize(28,28));
        b->setFixedSize(width,height);b->setAutoRaise(true);
        if(!compact) {
            auto labelFont=b->font();int pixels=QFontInfo(labelFont).pixelSize();
            const int lineRoom=(height-34)/2;
            while(QFontMetrics(labelFont).height()>lineRoom && pixels>8)labelFont.setPixelSize(--pixels);
            b->setFont(labelFont);
        }
        b->setFocusPolicy(Qt::StrongFocus);
        b->setToolButtonStyle(compact?Qt::ToolButtonIconOnly:Qt::ToolButtonTextUnderIcon);
        b->setText(buttonText(action,width,b->font()));
        b->setToolTip(text(action->text())+QStringLiteral("\n")+action->toolTip());
        b->setProperty("ribbonActionPath",paths.value(action));
        b->setProperty("ribbonRunning",running==action);
        QObject::connect(action,&QAction::changed,b,[b,action,width]{b->setText(buttonText(action,width,b->font()));});
        return b;
    }
    void rebuild() {
        if(layingOut)return;
        QLayoutItem *item=nullptr;
        while((item=groupsLayout->takeAt(0))) {if(item->widget()){item->widget()->hide();item->widget()->deleteLater();}delete item;}
        auto &tab=current();int width=0;
        const auto layout=plans(tab,groupsHost->width(),width);
        QMenu *overflow=nullptr;
        for(int i=0;i<tab.groups.size();++i) {
            const auto &g=tab.groups[i];const auto &p=layout[i];
            if(p.more) {
                if(!overflow)overflow=new CadMenu(groupsHost);
                auto *menu=overflow->addMenu(g.name);for(auto *action:g.actions){if(action)menu->addAction(action);else menu->addSeparator();}
                menu->setEnabled(tab.id!="sketch" || sketch || g.name==QStringLiteral("Crea schizzo"));
                continue;
            }
            auto *group=new QWidget(groupsHost);group->setObjectName("ribbonGroup_"+g.name);group->setProperty("ribbonGlassSurface",true);group->setFixedWidth(p.width-(g.divider?9:0));
            auto *column=new QVBoxLayout(group);column->setContentsMargins(4,2,4,2);column->setSpacing(2);
            auto *row=new QHBoxLayout;row->setContentsMargins(0,0,0,0);row->setSpacing(3);
            const bool usable=tab.id!="sketch" || sketch || g.name==QStringLiteral("Crea schizzo");
            auto *menu=new CadMenu(group);for(auto *action:g.actions){if(action)menu->addAction(action);else menu->addSeparator();}
            group->setEnabled(usable);
            if(p.collapsed) {
                auto *b=new QToolButton(group);b->setFixedSize(group->width()-8,compact?38:66);for(auto *a:g.actions)if(a){b->setIcon(a->icon());break;}b->setIconSize(QSize(28,28));
                b->setAutoRaise(true);b->setMenu(menu);b->setPopupMode(QToolButton::InstantPopup);b->setToolTip(g.name);row->addWidget(b);
            } else for(int j=0;j<p.shown;++j){if(g.actions[j])row->addWidget(button(g.actions[j],group,g.finish?p.width-8:width,compact?38:66));else{auto *line=new QFrame(group);line->setFrameShape(QFrame::VLine);line->setFixedWidth(8);row->addWidget(line);}}
            row->addStretch();column->addLayout(row);
            auto *footer=new QToolButton(group);footer->setAutoRaise(true);footer->setText(QFontMetrics(footer->font()).elidedText(g.name,Qt::ElideRight,p.width-24));footer->setToolTip(g.name);footer->setFixedWidth(group->width()-8);
            footer->setFixedHeight(20);footer->setMenu(menu);footer->setPopupMode(QToolButton::InstantPopup);column->addWidget(footer);
            group->setProperty("ribbonFinish",g.finish);groupsLayout->addWidget(group);if(g.divider){auto *line=new QFrame(groupsHost);line->setFrameShape(QFrame::VLine);line->setFixedWidth(8);groupsLayout->addWidget(line);}
        }
        groupsLayout->addStretch();
        if(overflow) {
            auto *more=new QToolButton(groupsHost);more->setObjectName("ribbonMore");more->setText(QStringLiteral("»"));more->setToolTip(QStringLiteral("Altri gruppi"));
            more->setAutoRaise(true);more->setFixedWidth(36);more->setMenu(overflow);overflow->setParent(more);more->setPopupMode(QToolButton::InstantPopup);groupsLayout->addWidget(more);
        }
        self->setProperty("ribbonOverflow",overflow!=nullptr);
    }
    void dialogShown(QDialog *dialog) {
        if(dialog->objectName()!=QStringLiteral("functionDialogPanel")||dialog->parentWidget()!=viewport)return;
        panel=dialog;
        const auto title=dialog->windowTitle().toLower();
        // Ponte sperimentale per riaperture dallo storyboard che non emettono QAction::triggered.
        const QList<QPair<QString,QString>> hints={{"estrusion","model"},{"rivoluz","model"},{"raccord","model"},{"smuss","model"},
            {"svuota","model"},{"sform","model"},{"filett","model"},{"superfic","surface"},{"cucitur","surface"},{"misur","inspect"},{"massa","inspect"}};
        for(const auto &hint:hints)if(title.contains(hint.first)){selectTab(hint.second);break;}
        for(auto it=paths.cbegin();it!=paths.cend();++it) {
            const auto word=caption(it.key()).toLower().split(' ').first();
            if(word.size()>4 && title.contains(word)){running=it.key();break;}
        }
        schedule();
        QObject::connect(dialog,&QDialog::finished,self,[this,dialog]{if(panel==dialog){panel.clear();running.clear();schedule();self->syncContext();}});
        QTimer::singleShot(0,self,[this,p=QPointer<QDialog>(dialog)]{if(p)placePanel(p);});
    }
};
RibbonPreview::RibbonPreview(QMainWindow *window):QWidget(nullptr),impl_(std::make_unique<Impl>(this,window)) {
    setParent(impl_->viewport);setObjectName("experimentalRibbon");setAttribute(Qt::WA_NoMousePropagation);
    setAttribute(Qt::WA_TranslucentBackground);setAutoFillBackground(false);
    impl_->roundedEffect=new QGraphicsOpacityEffect(this);impl_->roundedEffect->setOpacity(1);
    setGraphicsEffect(impl_->roundedEffect);
    setStyleSheet(QStringLiteral("#experimentalRibbon QWidget[ribbonGlassSurface=\"true\"], #experimentalRibbon QLabel, #ribbonTabs {background:transparent;} #ribbonTabs::tab:!selected {background:transparent;} #experimentalRibbon QToolButton[ribbonRunning=\"true\"] {background: palette(highlight);color: palette(highlighted-text);border-radius:5px;}"));
    auto *layout=new QHBoxLayout(this);layout->setSizeConstraint(QLayout::SetNoConstraint);layout->setContentsMargins(0,0,0,0);layout->setSpacing(8);
    impl_->workspace=new QWidget(this);impl_->workspace->setProperty("ribbonGlassSurface",true);impl_->workspace->setFixedWidth(88);
    auto *space=new QVBoxLayout(impl_->workspace);space->setContentsMargins(6,8,6,6);space->setSpacing(3);
    auto *icon=new QLabel(impl_->workspace);icon->setPixmap(commandIcon("box").pixmap(28,28));icon->setAlignment(Qt::AlignCenter);space->addWidget(icon);
    auto *title=new QLabel(QStringLiteral("ForgeCAD\nRibbon"),impl_->workspace);title->setAlignment(Qt::AlignCenter);space->addWidget(title);
    impl_->workspaceTitle=title;
    impl_->density=new QToolButton(impl_->workspace);impl_->density->setObjectName("ribbonDensity");impl_->density->setText(QStringLiteral("Compatta"));impl_->density->setCheckable(true);impl_->density->setAutoRaise(true);space->addWidget(impl_->density);
    connect(impl_->density,&QToolButton::toggled,this,&RibbonPreview::setCompact);layout->addWidget(impl_->workspace);
    auto *body=new QWidget(this);body->setProperty("ribbonGlassSurface",true);auto *column=new QVBoxLayout(body);impl_->bodyLayout=column;column->setSizeConstraint(QLayout::SetNoConstraint);column->setContentsMargins(0,0,0,0);column->setSpacing(0);
    impl_->tabBar=new RibbonTabBar(body);impl_->tabBar->setObjectName("ribbonTabs");impl_->tabBar->setDrawBase(false);impl_->tabBar->setExpanding(false);impl_->tabBar->setFixedHeight(30);impl_->tabBar->setUsesScrollButtons(true);impl_->tabBar->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
    for(const auto &tab:impl_->tabs){const int i=impl_->tabBar->addTab(tab.label);impl_->tabBar->setTabData(i,tab.id);}
    connect(impl_->tabBar,&QTabBar::currentChanged,this,[this]{if(!impl_->sketch && selectedTab()!="sketch")impl_->lastOrdinary=selectedTab();impl_->schedule();});column->addWidget(impl_->tabBar,0,Qt::AlignLeft);
    impl_->groupsHost=new QWidget(body);impl_->groupsHost->setProperty("ribbonGlassSurface",true);impl_->groupsHost->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);impl_->groupsLayout=new QHBoxLayout(impl_->groupsHost);impl_->groupsLayout->setSizeConstraint(QLayout::SetNoConstraint);impl_->groupsLayout->setContentsMargins(0,0,0,0);impl_->groupsLayout->setSpacing(1);column->addWidget(impl_->groupsHost);layout->addWidget(body,1);
    impl_->cornerAccent=new RibbonCornerAccent(this,impl_->tabBar);impl_->cornerAccent->show();
    if(auto *menu=window->findChild<QMenu *>("toolbarsMenu")) {
        menu->addSeparator();
        auto *visible=menu->addAction(QStringLiteral("Ribbon"));visible->setObjectName("ribbonVisibleAction");visible->setProperty("toolbarConfiguration",true);visible->setCheckable(true);visible->setChecked(QSettings().value("ribbon/visible",true).toBool());
        connect(visible,&QAction::toggled,this,[this](bool on){setVisible(on);QSettings().setValue("ribbon/visible",on);impl_->syncGlass();});
        auto *edit=menu->addAction(QStringLiteral("Personalizza ribbon…"));edit->setObjectName("ribbonEditorAction");edit->setProperty("toolbarConfiguration",true);connect(edit,&QAction::triggered,this,[this]{impl_->customize();});
        setContextMenuPolicy(Qt::CustomContextMenu);connect(this,&QWidget::customContextMenuRequested,this,[this,menu](const QPoint &point){menu->exec(mapToGlobal(point));});
        impl_->density->setContextMenuPolicy(Qt::CustomContextMenu);connect(impl_->density,&QWidget::customContextMenuRequested,this,[this]{impl_->customize();});
        impl_->density->setToolTip(QStringLiteral("Riduci la ribbon. Clic destro per personalizzarla."));
    }
    for(auto it=impl_->paths.cbegin();it!=impl_->paths.cend();++it) {
        auto *action=it.key();
        connect(action,&QAction::triggered,this,[this]{if(!impl_->panel){impl_->running.clear();impl_->schedule();}});
        // I menu dinamici (per esempio File recenti) eliminano le proprie azioni
        // al caricamento di un documento. Non conservare puntatori a oggetti distrutti.
        connect(action,&QObject::destroyed,this,[this,action]{
            impl_->paths.remove(action);
            bool changed=false;
            for(auto &tab:impl_->tabs)for(auto &group:tab.groups)
                changed=group.actions.removeAll(action)>0 || changed;
            if(changed)impl_->schedule();
        });
    }
    qApp->installEventFilter(this);
    impl_->contextTimer.setInterval(50);connect(&impl_->contextTimer,&QTimer::timeout,this,&RibbonPreview::syncContext);impl_->contextTimer.start();
    setCompact(QSettings().value("ribbon/compact",false).toBool());syncContext();setVisible(QSettings().value("ribbon/visible",true).toBool());impl_->schedule();
}
RibbonPreview::~RibbonPreview(){impl_->clearGlass();qApp->removeEventFilter(this);}
QString RibbonPreview::selectedTab() const {return impl_->tabBar->tabData(impl_->tabBar->currentIndex()).toString();}
bool RibbonPreview::compact() const {return impl_->compact;}
void RibbonPreview::setCompact(bool value) {
    impl_->compact=value;
    impl_->workspaceTitle->setText(value?QStringLiteral("Ribbon"):QStringLiteral("ForgeCAD\nRibbon"));
    if(impl_->density->isChecked()!=value)impl_->density->setChecked(value);
    QSettings().setValue("ribbon/compact",value);impl_->place();impl_->schedule();
}
void RibbonPreview::syncContext() {
    const bool active=impl_->viewport->sketchModeActive();
    if(active!=impl_->sketch) {
        if(active && selectedTab()!=QStringLiteral("sketch"))impl_->lastOrdinary=selectedTab();
        impl_->sketch=active;
        impl_->selectTab(active?QStringLiteral("sketch"):impl_->lastOrdinary);
        impl_->running.clear();impl_->schedule();
    }
    impl_->place();
}
void RibbonPreview::selectForAction(QAction *action) {
    if(!action||!action->isEnabled())return;
    const auto contains=[&](const Impl::Tab &t){for(const auto &g:t.groups)if(g.actions.contains(action))return true;return false;};
    if(!contains(impl_->current()))for(const auto &tab:impl_->tabs)if(contains(tab)){impl_->selectTab(tab.id);break;}
    impl_->running=action;
    // Non ricostruire nel mousePress: il pulsante che ricevera' mouseRelease deve restare vivo.
    for(auto *button:findChildren<QToolButton *>())if(button->defaultAction()) {
        button->setProperty("ribbonRunning",button->defaultAction()==action);
        button->style()->unpolish(button);button->style()->polish(button);button->update();
    }
}
bool RibbonPreview::eventFilter(QObject *object,QEvent *event) {
    if(object==this) {
        if(event->type()==QEvent::Hide)impl_->clearGlass();
        if(event->type()==QEvent::Show || event->type()==QEvent::Move || event->type()==QEvent::Resize)impl_->syncGlass();
    }
    if(object==impl_->viewport && event->type()==QEvent::Resize){impl_->place();impl_->schedule();}
    if(auto *dialog=qobject_cast<QDialog *>(object)) {
        if(event->type()==QEvent::Show)impl_->dialogShown(dialog);
        if(dialog==impl_->panel && (event->type()==QEvent::Move||event->type()==QEvent::Resize))impl_->placePanel(dialog);
    }
    QAction *action=nullptr;
    if(event->type()==QEvent::Shortcut)action=qobject_cast<QAction *>(object);
    const auto activateKey=[&]{
        if(event->type()!=QEvent::KeyPress)return false;
        const int key=static_cast<QKeyEvent *>(event)->key();return key==Qt::Key_Return||key==Qt::Key_Enter||key==Qt::Key_Space;
    };
    if(auto *button=qobject_cast<QToolButton *>(object)) {
        if(isAncestorOf(button)) {
            if((event->type()==QEvent::MouseButtonPress && static_cast<QMouseEvent *>(event)->button()==Qt::LeftButton)||activateKey())action=button->defaultAction();
            if(event->type()==QEvent::MouseButtonRelease)QTimer::singleShot(0,this,[this]{if(!impl_->panel){impl_->running.clear();impl_->schedule();}});
        }
    }
    if(auto *menu=qobject_cast<QMenu *>(object)) {
        if(menu->window()!=impl_->window && !impl_->window->isAncestorOf(menu))return QWidget::eventFilter(object,event);
        if(event->type()==QEvent::MouseButtonRelease)action=menu->actionAt(static_cast<QMouseEvent *>(event)->position().toPoint());
        if(activateKey())action=menu->activeAction();
    }
    if(action)selectForAction(action);
    return QWidget::eventFilter(object,event);
}
void RibbonPreview::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);if(impl_->groupsHost)impl_->schedule();}
void RibbonPreview::wheelEvent(QWheelEvent *event){event->accept();}
void RibbonPreview::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    auto fill=palette().color(QPalette::Window);fill.setAlpha(qRound(255.0*impl_->opacity/100.0));
    // La curva esterna viene applicata una sola volta dalla maschera alpha.
    // Un secondo rettangolo arrotondato sfalsato creava aloni negli angoli.
    for(int i=0;i<2;++i)p.fillRect(impl_->cardRect(i),fill);
}
void RibbonPreview::runSelfTest() {
    const auto require=[](bool pass,const char *message){if(!pass)throw std::runtime_error(message);};
    for(qreal dpr:{1.0,1.5})for(int radius:{10,40})for(bool outline:{false,true}) {
        const auto image=rasterizeCards(QSize(96,128),dpr,QRect(0,0,96,128),QRect(),radius,outline);
        int fractional=0;
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            const int alpha=qAlpha(image.pixel(x,y));
            fractional+=alpha>0 && alpha<255;
            require(qAbs(alpha-qAlpha(image.pixel(image.width()-1-x,y)))<=3,"simmetria dei raggi destro e sinistro");
            require(qAbs(alpha-qAlpha(image.pixel(x,image.height()-1-y)))<=3,"simmetria dei raggi superiore e inferiore");
        }
        require(fractional>0,"raggi e contorno con copertura antialias");
    }
    const auto menuRadius=QSettings().value("view/functionPanelCornerRadius",10);
    for(int radius:{0,10,40})for(qreal dpr:{1.0,1.5}) {
        QSettings().setValue("view/functionPanelCornerRadius",radius);
        CadMenu menu(impl_->window);menu.addAction(QStringLiteral("Proprietà di massa…"))->setShortcut(QKeySequence("Ctrl+M"));
        auto *checked=menu.addAction(QStringLiteral("Opzione selezionata"));checked->setCheckable(true);checked->setChecked(true);
        auto *sub=addCadMenu(&menu,QStringLiteral("Sottomenu"));sub->addAction(QStringLiteral("Misura…"));
        menu.ensurePolished();menu.adjustSize();
        QImage image(QSize(qCeil(menu.width()*dpr),qCeil(menu.height()*dpr)),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);image.fill(Qt::transparent);menu.render(&image);
        require(radius==0 || qAlpha(image.pixel(0,0))==0,"angoli del popup trasparenti");
        require(qAlpha(image.pixel(image.width()/2,image.height()/2))==255,"corpo del menu opaco e leggibile");
        const auto r=menu.actionGeometry(menu.actions().first());
        QPainterPath safe;safe.addRoundedRect(QRectF(menu.rect()),radius,radius);
        require(safe.contains(QPointF(r.topLeft())) && safe.contains(QPointF(r.bottomRight())),"padding menu adeguato al raggio");
        bool fractional=false;for(int y=0;y<image.height()&&!fractional;++y)for(int x=0;x<image.width();++x){int alpha=qAlpha(image.pixel(x,y));if(alpha>0 && alpha<255){fractional=true;break;}}
        require(radius==0 || fractional,"contorno popup antialias");
        if(radius==40 && dpr==1.0){const auto capture=qEnvironmentVariable("FORGECAD_MENU_CAPTURE");if(!capture.isEmpty())image.save(capture);}
    }
    QSettings().setValue("view/functionPanelCornerRadius",menuRadius);
    // Riproduce la distruzione delle azioni di File recenti al caricamento.
    // Anche un comando personalizzato deve perdere il riferimento prima di riaprire un pannello.
    QAction *transient=nullptr;
    for(auto it=impl_->paths.cbegin();it!=impl_->paths.cend();++it)
        if(it.value().startsWith(QStringLiteral("File/File recenti/"))){transient=it.key();break;}
    require(transient,"azione dinamica presente nel catalogo iniziale");
    impl_->tabs[0].groups[0].actions.append(transient);
    delete transient;
    require(!impl_->paths.contains(transient),"azione distrutta rimossa dal catalogo");
    for(const auto &tab:impl_->tabs)for(const auto &group:tab.groups)
        require(!group.actions.contains(transient),"azione distrutta rimossa anche dai gruppi personalizzati");
    impl_->window->resize(1200,800);QApplication::processEvents();syncContext();QApplication::processEvents();
    const int originalRadius=QSettings().value("view/functionPanelCornerRadius",10).toInt();
    for(const auto &sample:QList<QPair<int,int>>{{0,0},{10,5},{40,14}}) {
        QSettings().setValue("view/functionPanelCornerRadius",sample.first);impl_->appearanceTimer.invalidate();syncContext();
        for(bool dense:{false,true}) {
            setCompact(dense);layout()->activate();impl_->bodyLayout->activate();impl_->workspace->layout()->activate();
            QApplication::processEvents();QApplication::processEvents();
            require(height()==(dense?95:123)+2*sample.second,"altezza adeguata al raggio");
            require(impl_->workspace->width()==88+2*sample.second,"blocco laterale adeguato al raggio");
            QPainterPath mainPath,workspacePath;
            mainPath.addRoundedRect(QRectF(impl_->cardRect(1)),sample.first,sample.first);
            workspacePath.addRoundedRect(QRectF(impl_->cardRect(0)),sample.first,sample.first);
            const auto bounds=[&](QWidget *widget){return QRectF(QRect(widget->mapTo(this,QPoint()),widget->size())).adjusted(.5,.5,-.5,-.5);};
            const QPoint tabOrigin=impl_->tabBar->mapTo(this,QPoint());
            require(tabOrigin.y()==impl_->cardRect(1).top(),"schede a filo del bordo per raccordare la linea blu");
            require(tabOrigin.x()==impl_->cardRect(1).left()+sample.second && impl_->tabBar->tabRect(0).left()<=2,"schede allineate a sinistra anche con temi che centrano le tab");
            for(int tab=0;tab<impl_->tabBar->count();++tab) {
                const auto label=QFontMetrics(impl_->tabBar->font()).boundingRect(impl_->tabBar->tabRect(tab),Qt::AlignCenter|Qt::TextSingleLine,impl_->tabBar->tabText(tab));
                require(mainPath.contains(QRectF(label.translated(tabOrigin))),"testi delle schede protetti dagli angoli arrotondati");
            }
            require(workspacePath.contains(bounds(impl_->density)),"tasto laterale contenuto negli angoli arrotondati");
            for(int i=0;i<impl_->groupsLayout->count();++i)if(auto *group=impl_->groupsLayout->itemAt(i)->widget())
                for(auto *button:group->findChildren<QToolButton *>())require(mainPath.contains(bounds(button)),"pulsante contenuto negli angoli arrotondati");
        }
    }
    QSettings().setValue("view/functionPanelCornerRadius",originalRadius);impl_->appearanceTimer.invalidate();setCompact(false);syncContext();QApplication::processEvents();
    require(height()==123+2*impl_->contentPadding(),"altezza normale con padding dinamico");
    require(impl_->tabBar->count()==5 && impl_->tabBar->isTabVisible(2),"scheda schizzo visibile prima di iniziare");
    impl_->selectTab("sketch");
    layout()->activate();impl_->groupsHost->parentWidget()->layout()->activate();
    QApplication::processEvents();QApplication::processEvents();
    QWidget *create=nullptr;
    for(int i=0;i<impl_->groupsLayout->count();++i)if(auto *group=impl_->groupsLayout->itemAt(i)->widget()) {
        if(group->objectName()==QStringLiteral("ribbonGroup_Crea schizzo"))create=group;
        else if(group->objectName().startsWith("ribbonGroup_"))require(!group->isEnabled(),"strumenti schizzo inattivi senza schizzo aperto");
    }
    require(create && create->isEnabled(),"creazione schizzo disponibile prima di iniziare");
    for(int tab=0;tab<impl_->tabBar->count();++tab) {
        impl_->tabBar->setCurrentIndex(tab);QApplication::processEvents();QApplication::processEvents();
        for(auto *button:findChildren<QToolButton *>())if(dynamic_cast<RibbonCommandButton *>(button)) {
            const auto lines=button->text().split('\n');require(lines.size()<=2,"etichette entro due righe");
            for(const auto &line:lines)require(QFontMetrics(button->font()).horizontalAdvance(line)<=button->width()-8,"etichetta contenuta nella larghezza del pulsante");
        }
    }
    auto *extrude=impl_->paths.key(QStringLiteral("Funzioni/Estrusione..."));require(extrude,"azione estrusione riutilizzata");
    impl_->selectTab("view");selectForAction(extrude);require(selectedTab()=="model","scheda selezionata per comando");
    setCompact(true);QApplication::processEvents();require(height()==95+2*impl_->contentPadding(),"altezza compatta con padding dinamico");setCompact(false);
    impl_->viewport->createSketch(0,QStringLiteral("Prova ribbon"));syncContext();QApplication::processEvents();
    require(selectedTab()=="sketch" && impl_->tabBar->isTabVisible(2),"entrata schizzo seleziona scheda");
    // Il hit-test richiede la finestra visibile; offscreen non mostra il viewport OpenGL.
    if(QGuiApplication::platformName()!=QStringLiteral("offscreen")) {
    FloatingPanel *constraints=nullptr;
    for(auto *dialog:impl_->window->findChildren<QDialog *>())if(auto *floating=dynamic_cast<FloatingPanel *>(dialog)){constraints=floating;break;}
    require(constraints,"pannello vincoli reale presente");
    auto *constraintGrip=constraints->findChild<QSizeGrip *>("functionPanelResizeGrip");
    require(constraintGrip && constraintGrip->isVisibleTo(constraints),"maniglia vincoli visibile");
    require(!constraints->isWindow(),"vincoli resta incorporato nel viewport");
    const int originalConstraintRadius=constraints->cornerRadius();
    for(int radius:{0,10,40}) {
        constraints->setCornerRadius(radius);QApplication::processEvents();
        require(constraints->childAt(constraintGrip->geometry().center())==constraintGrip,"maniglia vincoli raggiungibile anche con angoli arrotondati");
    }
    constraints->setCornerRadius(originalConstraintRadius);
    if(QGuiApplication::platformName()!=QStringLiteral("offscreen")) {
        const auto state=impl_->window->windowState();impl_->window->showMaximized();QApplication::processEvents();
        require(constraintGrip->isVisibleTo(constraints),"maniglia vincoli disponibile con finestra massimizzata");
        impl_->window->setWindowState(state);impl_->window->resize(1200,800);QApplication::processEvents();
    }
    constraints->resize(430,360);constraints->move(16,geometry().bottom()+10);QApplication::processEvents();
    const auto panelBefore=constraints->size(),viewportBefore=impl_->viewport->size();
    const QPointF gripLocal=constraintGrip->rect().center(),gripGlobal=constraintGrip->mapToGlobal(gripLocal.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,gripLocal,gripGlobal,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove,gripLocal+QPointF(45,35),gripGlobal+QPointF(45,35),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,gripLocal+QPointF(45,35),gripGlobal+QPointF(45,35),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(constraintGrip,&press);QApplication::sendEvent(constraintGrip,&move);QApplication::sendEvent(constraintGrip,&release);QApplication::processEvents();
    require(constraints->size()==panelBefore+QSize(45,35) && impl_->viewport->size()==viewportBefore,"drag ridimensiona solo il pannello vincoli");
    const auto capture=qEnvironmentVariable("FORGECAD_CONSTRAINT_CAPTURE");if(!capture.isEmpty())constraints->grab().save(capture);
    const auto savedConstraintGeometry=QSettings().value("view/constraintPanel").toRect();
    require(savedConstraintGeometry.size()==constraints->size(),"dimensioni vincoli memorizzate");
    {
        FloatingPanel restored(impl_->viewport,QStringLiteral("Vincoli"),QStringLiteral("view/constraintPanel"),QSize(380,520));
        restored.show();QApplication::processEvents();require(restored.size()==savedConstraintGeometry.size(),"dimensioni vincoli ripristinate");restored.hide();
    }

    }
    impl_->selectTab("view");syncContext();require(selectedTab()=="view","scelta manuale conservata nel contesto");
    impl_->selectTab("sketch");impl_->window->resize(800,650);QApplication::processEvents();syncContext();QApplication::processEvents();
    require(findChild<QWidget *>("ribbonGroup_Termina"),"termina schizzo raggiungibile a800");
    require(width()<=impl_->viewport->width()-16,"ribbon contenuta nel viewport dopo resize");
    int fittedWidth=0;const auto narrow=impl_->plans(impl_->current(),300,fittedWidth);
    require(std::any_of(narrow.begin(),narrow.end(),[](const Impl::Plan &p){return p.more;}),"gruppi eccedenti nel menu aggiuntivo");
    for(int i=0;i<narrow.size();++i)if(impl_->current().groups[i].finish)require(!narrow[i].more,"uscita schizzo esclusa dall'overflow");
    impl_->viewport->endSketchMode();syncContext();QApplication::processEvents();
    require(impl_->tabBar->isTabVisible(2) && selectedTab()!="sketch","uscita ripristina scheda ordinaria senza nascondere Schizzo");
    impl_->selectTab("sketch");QApplication::processEvents();
    impl_->viewport->selectSketch(0);syncContext();QApplication::processEvents();
    require(selectedTab()=="sketch","riapertura dalla scheda gia visibile");
    for(int i=0;i<impl_->groupsLayout->count();++i)if(auto *group=impl_->groupsLayout->itemAt(i)->widget())
        if(group->objectName().startsWith("ribbonGroup_"))require(group->isEnabled(),"strumenti disponibili nello schizzo aperto");
    impl_->viewport->endSketchMode();syncContext();QApplication::processEvents();
    require(selectedTab()!="sketch","uscita corretta anche entrando da Schizzo");
    QDialog panel(impl_->viewport);panel.setObjectName("functionDialogPanel");panel.setWindowTitle(QStringLiteral("Modifica raccordo"));panel.resize(400,280);panel.show();QApplication::processEvents();
    require(selectedTab()=="model","riapertura feature seleziona contesto");
    require(panel.y()>geometry().bottom(),"pannello sotto la ribbon");panel.reject();QApplication::processEvents();
    for(auto *bar:impl_->window->findChildren<QToolBar *>())require(bar->isHidden(),"barre classiche nascoste solo nella prova");
    // Ogni pulsante di comando punta a una QAction della finestra originale.
    int real=0;for(auto *button:findChildren<QToolButton *>())if(auto *a=button->defaultAction()){require(impl_->paths.contains(a),"azione reale e stato condiviso");++real;}
    // A larghezza ridotta tutti i gruppi ordinari possono essere collassati:
    // i comandi devono restare gli stessi anche nei menu.
    for(auto *menu:findChildren<QMenu *>())for(auto *a:menu->actions())if(!a->isSeparator()&&!a->menu()) {
        require(impl_->paths.contains(a),"menu del gruppo conserva le azioni reali");++real;
    }
    require(real>0,"comandi reali disponibili anche nei gruppi collassati");
    auto *wheel=impl_->paths.key(QStringLiteral("Visualizza/Zoom/Zoom con rotella"));
    require(wheel,"azione rotella originale disponibile");const bool wasEnabled=impl_->viewport->wheelZoomEnabled();
    wheel->trigger();require(impl_->viewport->wheelZoomEnabled()!=wasEnabled,"azione reale modifica il viewport");wheel->trigger();
    if(QGuiApplication::platformName()!=QStringLiteral("offscreen")) {
        require(impl_->viewport->gpuGlassAvailable(),"compositore GPU disponibile nella prova nativa");
        // Il framebuffer contiene la scena, senza i figli Qt: verifica il blur
        // effettivo e che il distacco tra le carte non venga sfocato.
        hide();QApplication::processEvents();const QImage raw=impl_->viewport->grabFramebuffer();
        show();QApplication::processEvents();const QImage glass=impl_->viewport->grabFramebuffer();
        require(!raw.isNull() && raw.size()==glass.size(),"framebuffer del vetro acquisito");
        const qreal dpr=impl_->viewport->devicePixelRatioF();
        auto changed=[&](const QRect &logical) {
            const QRect pixels(qRound(logical.x()*dpr),qRound(logical.y()*dpr),qRound(logical.width()*dpr),qRound(logical.height()*dpr));
            int count=0;const QRect clipped=pixels.intersected(raw.rect());
            for(int y=clipped.top();y<=clipped.bottom();++y)for(int x=clipped.left();x<=clipped.right();++x)
                if(raw.pixel(x,y)!=glass.pixel(x,y))++count;
            return count;
        };
        require(changed(impl_->cardRect(1).translated(pos()))>0,"blur modifica realmente la scena sotto la ribbon");
        require(changed(QRect(x()+impl_->workspaceWidth()+2,y()+16,4,height()-32))==0,"distacco tra le carte senza blur");
        const auto originalBackground=impl_->viewport->background();
        auto neutral=originalBackground;neutral.startColor=QColor(80,80,80);neutral.endColor=Qt::white;
        neutral.gradient=true;neutral.angle=0;neutral.position=.5f;
        const bool originalGrid=impl_->viewport->gridVisible(),originalPlanes=impl_->viewport->referencePlanesVisible();
        const int originalAA=impl_->viewport->antialiasing();
        impl_->viewport->setGridVisible(false);impl_->viewport->setReferencePlanesVisible(false);
        impl_->viewport->setBackground(neutral);
        for(int frame=0;frame<8;++frame) {
            impl_->viewport->setAntialiasing(frame<4?0:4);
            impl_->viewport->setViewPreset(frame%4);
            if(frame%2)hide();else show();
            impl_->viewport->update();QApplication::processEvents();
            impl_->viewport->makeCurrent();
            auto *gl=impl_->viewport->context()->functions();
            // Stato lasciato da QPainter con una penna colorata: deve essere
            // irrilevante per il disegno opaco dello sfondo del nuovo frame.
            gl->glEnable(GL_BLEND);gl->glBlendColor(1.f,.66f,.55f,1.f);
            gl->glBlendFunc(GL_CONSTANT_COLOR,GL_ONE_MINUS_SRC_COLOR);
            const auto image=impl_->viewport->grabFramebuffer();
            impl_->viewport->doneCurrent();
            for(const QPoint point:{QPoint(20,image.height()-20),QPoint(image.width()-20,image.height()-20)}) {
                const auto color=image.pixelColor(point);
                const int expected=qRound(80.0+175.0*(point.x()+.5)/image.width());
                if(qAbs(color.red()-expected)>3 || qAbs(color.red()-color.green())>2 || qAbs(color.green()-color.blue())>2) {
                    image.save(QStringLiteral("/tmp/ribbon-background-failure.png"));
                    throw std::runtime_error(QStringLiteral("sfondo neutro alterato al frame %1: %2").arg(frame).arg(color.name()).toStdString());
                }
            }
        }
        show();impl_->viewport->setBackground(originalBackground);
        impl_->viewport->setGridVisible(originalGrid);impl_->viewport->setReferencePlanesVisible(originalPlanes);
        impl_->viewport->setAntialiasing(originalAA);
        auto *box=impl_->paths.key(QStringLiteral("Funzioni/Primitive/Parallelepipedo..."));require(box,"primitiva reale disponibile");
        bool opened=false,placed=false;
        QTimer::singleShot(0,this,[&]{
            for(auto *dialog:impl_->viewport->findChildren<QDialog *>())if(dialog->objectName()=="functionDialogPanel" && dialog->isVisible()) {
                opened=true;impl_->placePanel(dialog);placed=dialog->y()>geometry().bottom();dialog->reject();
            }
        });
        const int bodies=impl_->viewport->extrusions().size();selectForAction(box);box->trigger();
        require(opened && placed,"comando reale apre pannello sotto la ribbon");
        require(impl_->viewport->extrusions().size()==bodies,"annullamento non crea geometria");
    }
    const auto originalLayout=impl_->defaults;
    const auto originalBytes=QSettings().value("ribbon/layout");
    auto *editorAction=impl_->window->findChild<QAction *>("ribbonEditorAction");
    require(editorAction,"editor ribbon disponibile nel menu");
    bool edited=false;
    QTimer::singleShot(0,this,[&]{
        auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog)return;
        auto *sections=dialog->findChild<QListWidget *>("ribbonEditorSections");
        auto *commands=dialog->findChild<QListWidget *>("ribbonEditorCommands");
        if(!sections||!commands){dialog->reject();return;}
        sections->setCurrentRow(0);commands->setCurrentRow(0);
        dialog->findChild<QPushButton *>("ribbonAddCommand")->click();
        dialog->findChild<QPushButton *>("ribbonAddSeparator")->click();
        dialog->findChild<QCheckBox *>("ribbonSectionSeparator")->setChecked(true);
        if(sections->count()>1)sections->item(1)->setCheckState(Qt::Unchecked);
        const auto capture=qEnvironmentVariable("FORGECAD_RIBBON_EDITOR_CAPTURE");
        if(!capture.isEmpty())dialog->grab().save(capture);
        edited=true;dialog->accept();
    });
    editorAction->trigger();require(edited,"editor modifica comandi e separatori");
    const auto savedBytes=QSettings().value("ribbon/layout").toByteArray();
    const auto savedPages=QJsonDocument::fromJson(savedBytes).array();
    require(savedPages.size()==5,"layout salvato per tutte le schede");
    const auto savedGroups=savedPages[0].toObject()["groups"].toArray();
    require(savedGroups[0].toObject()["divider"].toBool(),"separatore sezione persistito");
    require(!savedGroups[1].toObject()["visible"].toBool(),"sezione nascosta persistita");
    impl_->applyLayout(originalLayout);impl_->applyLayout(savedPages);
    require(impl_->tabs[0].groups.size()==originalLayout[0].toObject()["groups"].toArray().size()-1,"ricaricamento mantiene sezione nascosta");
    require(impl_->tabs[0].groups[0].actions.contains(nullptr),"separatore comando ricaricato");
    impl_->rebuild();QApplication::processEvents();
    QTimer::singleShot(0,this,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(dialog){dialog->findChild<QPushButton *>("ribbonReset")->click();dialog->reject();}});
    editorAction->trigger();require(QSettings().value("ribbon/layout").toByteArray()==savedBytes,"Annulla conserva configurazione dopo ripristino");
    QTimer::singleShot(0,this,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(dialog){dialog->findChild<QPushButton *>("ribbonReset")->click();dialog->accept();}});
    editorAction->trigger();require(impl_->layout()==originalLayout,"ripristino confermato ricrea disposizione iniziale");
    auto hostile=originalLayout;auto page=hostile[2].toObject();page["groups"]=QJsonArray{};hostile[2]=page;impl_->applyLayout(hostile);
    require(impl_->tabs[2].groups.size()==1 && impl_->tabs[2].groups[0].finish,"uscita schizzo protetta anche con layout incompleto");
    impl_->applyLayout(originalLayout);QSettings().setValue("ribbon/layout",originalBytes);impl_->schedule();

}
}
