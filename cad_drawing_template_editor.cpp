#include "cad_drawing_template_editor.h"
#include "cad_drawing_editor.h"
#include "cad_drawing_page.h"
#include "cad_drawing_template.h"
#include "cad_icons.h"
#include <QAction>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <functional>
namespace ForgeCad {
namespace {
class TemplateCanvas final : public QGraphicsView {
  public:
    using QGraphicsView::QGraphicsView;

  protected:
    void wheelEvent(QWheelEvent *event) override {
        setTransformationAnchor(AnchorUnderMouse);
        const double factor = event->angleDelta().y() > 0 ? 1.2 : 1 / 1.2;
        const double next = transform().m11() * factor;
        if (next > .05 && next < 200)
            scale(factor, factor);
        event->accept();
    }
};
class FramePreview final : public QGraphicsItem {
  public:
    DrawingSheet *sheet;
    explicit FramePreview(DrawingSheet *s) : sheet(s) { setZValue(-10); }
    QRectF boundingRect() const override { return {QPointF(), sheet->size}; }
    void resize() { prepareGeometryChange(); }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->fillRect(boundingRect(), Qt::white);
        auto context = defaultDrawingPageContext(*sheet);
        context.modelName = "Modello di esempio.prt";
        paintDrawingPage(*p, *sheet, context);
        // Campione dello stile quote, indipendente da qualsiasi modello.
        p->save();
        const auto &s = sheet->style;
        const QPointF a(35, 40), b(95, 40);
        p->setPen(QPen(QColor("#294860"), .2));
        p->drawLine(a, b);
        p->drawPath(drawingArrowPath(s, a, b));
        p->drawPath(drawingArrowPath(s, b, a));
        QFont f(s.fontFamily);
        f.setPixelSize(100);
        p->setFont(f);
        p->translate(52, 32);
        p->scale(s.dimensionHeight / 100, s.dimensionHeight / 100);
        p->drawText(QPointF(), "60.00 mm");
        p->restore();
    }
};
class FrameHandle final : public QGraphicsRectItem {
  public:
    std::function<void(QPointF)> moved;
    FrameHandle() {
        setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
        setPen(QPen(QColor("#3688bf"), 0, Qt::DashLine));
        setBrush(QColor(54, 136, 191, 12));
        setCursor(Qt::SizeAllCursor);
        setZValue(5);
    }
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override {
        if (change == ItemPositionChange && scene()) {
            auto p = value.toPointF();
            p = {std::round(p.x() * 2) / 2, std::round(p.y() * 2) / 2};
            return p;
        }
        if (change == ItemPositionHasChanged && moved)
            moved(value.toPointF());
        return QGraphicsRectItem::itemChange(change, value);
    }
};
class TemplateEditor final : public QDialog {
    DrawingSheet draft_;
    DrawingSheetEditor *target_;
    QGraphicsScene *scene_;
    QGraphicsView *view_;
    FramePreview *preview_;
    FrameHandle *handle_ = nullptr;
    QListWidget *items_;
    QLabel *status_;
    QComboBox *format_, *orientation_, *alignment_, *field_, *arrow_;
    QDoubleSpinBox *paperWidth_, *paperHeight_, *scale_, *x_, *y_, *w_, *h_, *height_, *line_, *dimensionHeight_,
        *arrowLength_;
    QLineEdit *text_, *code_, *organization_, *author_, *date_;
    QCheckBox *bold_, *visible_, *preferred_;
    QFontComboBox *font_;
    bool updating_ = false;
    QString path_;
    QByteArray checkpoint_;
    void checkpoint() { encodeDrawingTemplate(drawingTemplateOf(draft_), checkpoint_); }
    bool discard() {
        QByteArray bytes;
        encodeDrawingTemplate(drawingTemplateOf(draft_), bytes);
        return bytes == checkpoint_ ||
               QMessageBox::question(
                   this, "Template modificato", "Le modifiche al template non sono state salvate. Scartarle?",
                   QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
    }
    DrawingFrameItem *selected() {
        const int i = items_->currentRow();
        return i >= 0 && i < draft_.titleBlock.items.size() ? &draft_.titleBlock.items[i] : nullptr;
    }
    QPointF origin() const { return {draft_.size.width() - 10, draft_.size.height() - 10}; }
    QDoubleSpinBox *spin(QFormLayout *form, const QString &label, const char *name, double minimum, double maximum,
                         double initial, int decimals = 2) {
        auto *s = new QDoubleSpinBox(this);
        s->setObjectName(name);
        s->setDecimals(decimals);
        s->setRange(minimum, maximum);
        s->setValue(initial);
        form->addRow(label, s);
        return s;
    }
    QPushButton *button(QHBoxLayout *row, const QString &label, const char *name, std::function<void()> callback) {
        auto *b = new QPushButton(label, this);
        b->setObjectName(name);
        row->addWidget(b);
        connect(b, &QPushButton::clicked, this, std::move(callback));
        return b;
    }
    void report(const QString &error, const QString &success = {}) {
        status_->setText(error.isEmpty() ? success : error);
        status_->setStyleSheet(error.isEmpty() ? QString() : "color:#c83838");
    }
    void refresh() {
        preview_->update();
        scene_->update();
        report(validateDrawingFrame(draft_));
    }
    void fit() { view_->fitInView(QRectF(QPointF(-4, -4), draft_.size + QSizeF(8, 8)), Qt::KeepAspectRatio); }
    void updateHandle() {
        if (handle_) {
            handle_->moved = {};
            scene_->removeItem(handle_);
            delete handle_;
            handle_ = nullptr;
        }
        const auto *i = selected();
        if (!i)
            return;
        handle_ = new FrameHandle;
        handle_->setRect(0, 0, std::max(1., i->rect.width()), std::max(1., i->rect.height()));
        handle_->setPos(origin() + i->rect.topLeft());
        scene_->addItem(handle_);
        handle_->setSelected(true);
        handle_->moved = [this](QPointF p) {
            auto *i = selected();
            if (!i)
                return;
            i->rect.moveTopLeft(p - origin());
            updating_ = true;
            x_->setValue(p.x());
            y_->setValue(p.y());
            updating_ = false;
            refresh();
        };
    }
    void select() {
        updating_ = true;
        const auto *i = selected();
        for (auto *widget : {static_cast<QWidget *>(text_), static_cast<QWidget *>(field_),
                             static_cast<QWidget *>(bold_), static_cast<QWidget *>(alignment_)})
            widget->setEnabled(i && i->kind == DrawingFrameKind::Text);
        if (i) {
            const auto p = origin() + i->rect.topLeft();
            x_->setValue(p.x());
            y_->setValue(p.y());
            w_->setValue(i->rect.width());
            h_->setValue(i->rect.height());
            text_->setText(i->text);
            height_->setValue(i->textHeight);
            line_->setValue(i->lineWidth);
            bold_->setChecked(i->bold);
            alignment_->setCurrentIndex(i->alignment);
        }
        updating_ = false;
        updateHandle();
    }
    void list() {
        const int current = items_->currentRow();
        items_->clear();
        const QStringList names = {"Testo", "Linea", "Rettangolo", "Logo"};
        for (const auto &i : draft_.titleBlock.items)
            items_->addItem(names[int(i.kind)] + (i.text.isEmpty() ? QString() : " — " + i.text.left(36)));
        items_->setCurrentRow(items_->count() ? std::clamp(current, 0, items_->count() - 1) : -1);
        select();
        refresh();
    }
    void loadControls() {
        if (!draft_.titleBlock.customLayout) {
            draft_.titleBlock.customLayout = true;
            draft_.titleBlock.items = standardDrawingFrame();
        }
        updating_ = true;
        format_->setCurrentText(drawingPaperName(draft_.size));
        orientation_->setCurrentIndex(draft_.size.width() >= draft_.size.height() ? 0 : 1);
        paperWidth_->setValue(draft_.size.width());
        paperHeight_->setValue(draft_.size.height());
        const bool custom = format_->currentIndex() == 5;
        paperWidth_->setEnabled(custom);
        paperHeight_->setEnabled(custom);
        orientation_->setEnabled(!custom);
        scale_->setValue(draft_.scale);
        visible_->setChecked(draft_.titleBlock.visible);
        code_->setText(draft_.titleBlock.code);
        organization_->setText(draft_.titleBlock.organization);
        author_->setText(draft_.titleBlock.author);
        date_->setText(draft_.titleBlock.date);
        font_->setCurrentFont(drawingFont(draft_.style));
        dimensionHeight_->setValue(draft_.style.dimensionHeight);
        arrowLength_->setValue(draft_.style.arrowLength);
        arrow_->setCurrentIndex(int(draft_.style.arrow));
        updating_ = false;
        scene_->setSceneRect(QRectF(QPointF(), draft_.size));
        list();
        fit();
    }
    void add(DrawingFrameKind kind) {
        DrawingFrameItem item;
        item.kind = kind;
        item.rect = {-100, -65, 60, 8};
        if (kind == DrawingFrameKind::Text)
            item.text = "Testo";
        if (kind == DrawingFrameKind::Line)
            item.rect.setHeight(0);
        if (kind == DrawingFrameKind::Image) {
            const auto path =
                QFileDialog::getOpenFileName(this, "Inserisci logo", {}, "Immagini (*.png *.jpg *.jpeg *.bmp *.webp)");
            if (path.isEmpty())
                return;
            QImageReader reader(path);
            const auto size = reader.size();
            if (!size.isValid() || size.width() > 4096 || size.height() > 4096) {
                report("Logo troppo grande: massimo 4096 pixel per lato.");
                return;
            }
            const auto image = reader.read();
            if (image.isNull()) {
                report(reader.errorString());
                return;
            }
            QBuffer b(&item.imagePng);
            b.open(QIODevice::WriteOnly);
            if (!image.save(&b, "PNG") || item.imagePng.size() > 4 * 1024 * 1024) {
                report("Logo troppo grande: massimo 4 MiB.");
                return;
            }
            item.rect.setSize({40, 20});
        }
        draft_.titleBlock.items.append(item);
        list();
        items_->setCurrentRow(items_->count() - 1);
    }
    bool valid() {
        QByteArray bytes;
        const auto error = encodeDrawingTemplate(drawingTemplateOf(draft_), bytes);
        report(error);
        return error.isEmpty();
    }
    void save() {
        if (!valid())
            return;
        const auto path = QFileDialog::getSaveFileName(this, "Salva template", path_, "Template ForgeCAD (*.drwt)");
        if (path.isEmpty())
            return;
        path_ = path.endsWith(".drwt", Qt::CaseInsensitive) ? path : path + ".drwt";
        const auto e = saveDrawingTemplate(path_, drawingTemplateOf(draft_));
        if (e.isEmpty())
            checkpoint();
        report(e, "Template salvato: " + path_);
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QDialog::resizeEvent(event);
        QTimer::singleShot(0, this, [this] { fit(); });
    }

  public:
    void reject() override {
        if (discard())
            QDialog::reject();
    }
    TemplateEditor(QWidget *parent, DrawingSheetEditor *target) : QDialog(parent), target_(target) {
        setObjectName("drawingTemplateEditor");
        setWindowTitle("Editor template dei fogli");
        resize(1280, 850);
        if (target)
            applyDrawingTemplate(draft_, drawingTemplateOf(target->sheet()));
        draft_.title = "Tavola di esempio";
        draft_.titleBlock.visible = true;
        if (!draft_.titleBlock.customLayout) {
            draft_.titleBlock.customLayout = true;
            draft_.titleBlock.items = standardDrawingFrame();
        }
        auto *root = new QVBoxLayout(this);
        auto *bar = new QToolBar(this);
        bar->setIconSize({24, 24});
        root->addWidget(bar);
        const auto action = [&](const QString &label, const char *name, const QString &icon,
                                std::function<void()> callback) {
            auto *a = bar->addAction(commandIcon(icon), label);
            a->setObjectName(name);
            connect(a, &QAction::triggered, this, std::move(callback));
        };
        action("Testo / campo", "templateAddText", "drawingNote", [this] { add(DrawingFrameKind::Text); });
        action("Linea", "templateAddLine", "line", [this] { add(DrawingFrameKind::Line); });
        action("Rettangolo", "templateAddRectangle", "rectangle", [this] { add(DrawingFrameKind::Rectangle); });
        action("Logo...", "templateAddLogo", "viewBackground", [this] { add(DrawingFrameKind::Image); });
        action("Elimina elemento", "templateDeleteItem", "delete", [this] {
            const int row = items_->currentRow();
            if (row >= 0) {
                draft_.titleBlock.items.removeAt(row);
                list();
            }
        });
        action("Adatta foglio", "templateFit", "zoomAll", [this] { fit(); });
        auto *split = new QSplitter(this);
        root->addWidget(split, 1);
        scene_ = new QGraphicsScene(this);
        view_ = new TemplateCanvas(scene_, split);
        view_->setDragMode(QGraphicsView::ScrollHandDrag);
        view_->setObjectName("templateCanvas");
        view_->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        view_->setBackgroundBrush(QColor("#394954"));
        preview_ = new FramePreview(&draft_);
        scene_->addItem(preview_);
        auto *tabs = new QTabWidget(split);
        tabs->setMinimumWidth(340);
        tabs->setMaximumWidth(450);
        auto *general = new QWidget(tabs);
        auto *form = new QFormLayout(general);
        tabs->addTab(general, "Foglio");
        format_ = new QComboBox(this);
        format_->setObjectName("templatePaperFormat");
        format_->addItems({"A4", "A3", "A2", "A1", "A0", "Personalizzato"});
        orientation_ = new QComboBox(this);
        orientation_->setObjectName("templatePaperOrientation");
        orientation_->addItems({"Orizzontale", "Verticale"});
        form->addRow("Formato:", format_);
        form->addRow("Orientamento:", orientation_);
        paperWidth_ = spin(form, "Larghezza (mm):", "templatePaperWidth", 50, 2000, 420);
        paperHeight_ = spin(form, "Altezza (mm):", "templatePaperHeight", 50, 2000, 297);
        scale_ = spin(form, "Scala foglio:", "templateScale", .000001, 1000000, 1, 6);
        visible_ = new QCheckBox("Mostra cartiglio", this);
        form->addRow(visible_);
        const auto edit = [&](const QString &label, const char *name, int max) {
            auto *e = new QLineEdit(this);
            e->setObjectName(name);
            e->setMaxLength(max);
            form->addRow(label, e);
            return e;
        };
        code_ = edit("Codice:", "templateCode", 128);
        organization_ = edit("Organizzazione:", "templateOrganization", 128);
        author_ = edit("Autore:", "templateAuthor", 128);
        date_ = edit("Data dichiarata:", "templateDate", 32);
        preferred_ = new QCheckBox("Usa questo formato per le nuove tavole", this);
        preferred_->setObjectName("templatePreferredFormat");
        form->addRow(preferred_);
        auto *defaults = new QHBoxLayout;
        form->addRow(defaults);
        button(defaults, "Carica predefinito", "templateLoadDefault", [this] {
            DrawingSheetTemplate t;
            bool found;
            const auto e = loadDefaultDrawingTemplate(draft_.size, t, found);
            if (!e.isEmpty() || !found) {
                report(e.isEmpty() ? QStringLiteral("Nessun predefinito per questo formato e orientamento.") : e);
                return;
            }
            preview_->resize();
            applyDrawingTemplate(draft_, t);
            loadControls();
        });
        button(defaults, "Ripristina standard", "templateStandard", [this] {
            draft_.titleBlock.customLayout = true;
            draft_.titleBlock.items = standardDrawingFrame();
            draft_.style = {};
            loadControls();
        });
        auto *setDefault = new QPushButton("Salva come predefinito per questo formato", this);
        setDefault->setObjectName("templateSetDefault");
        form->addRow(setDefault);
        connect(setDefault, &QPushButton::clicked, this, [this] {
            if (valid()) {
                const auto e = saveDefaultDrawingTemplate(drawingTemplateOf(draft_), preferred_->isChecked());
                if (e.isEmpty())
                    checkpoint();
                report(e, "Predefinito salvato per " + drawingPaperName(draft_.size) + " / " +
                              orientation_->currentText());
            }
        });
        auto *removeDefault = new QPushButton("Rimuovi predefinito di questo formato", this);
        removeDefault->setObjectName("templateRemoveDefault");
        form->addRow(removeDefault);
        connect(removeDefault, &QPushButton::clicked, this, [this] {
            report(removeDefaultDrawingTemplate(draft_.size),
                   "Predefinito rimosso; le tavole esistenti conservano il proprio cartiglio.");
        });
        auto *element = new QWidget(tabs);
        auto *ef = new QFormLayout(element);
        tabs->addTab(element, "Elementi");
        items_ = new QListWidget(this);
        items_->setObjectName("templateItems");
        ef->addRow(items_);
        x_ = spin(ef, "X sul foglio (mm):", "templateItemX", 0, 2000, 0);
        y_ = spin(ef, "Y sul foglio (mm):", "templateItemY", 0, 2000, 0);
        w_ = spin(ef, "Larghezza (mm):", "templateItemWidth", 0, 2000, 60);
        h_ = spin(ef, "Altezza (mm):", "templateItemHeight", 0, 2000, 8);
        text_ = new QLineEdit(this);
        text_->setObjectName("templateItemText");
        text_->setMaxLength(4096);
        ef->addRow("Testo / campi:", text_);
        field_ = new QComboBox(this);
        field_->addItems({"Inserisci campo...", "titolo", "codice", "modello", "organizzazione", "autore", "data",
                          "scala", "formato", "foglio", "fogli", "revisione_tavola", "revisione_modello"});
        ef->addRow("Campo automatico:", field_);
        height_ = spin(ef, "Altezza testo (mm):", "templateItemTextHeight", .5, 30, 2.6);
        line_ = spin(ef, "Spessore linea (mm):", "templateItemLineWidth", .05, 3, .25);
        bold_ = new QCheckBox("Grassetto", this);
        ef->addRow(bold_);
        alignment_ = new QComboBox(this);
        alignment_->addItems({"Sinistra", "Centro", "Destra"});
        ef->addRow("Allineamento:", alignment_);
        auto *style = new QWidget(tabs);
        auto *sf = new QFormLayout(style);
        tabs->addTab(style, "Stile quote");
        font_ = new QFontComboBox(this);
        font_->setObjectName("templateFont");
        sf->addRow("Carattere:", font_);
        dimensionHeight_ = spin(sf, "Altezza quote (mm):", "templateDimensionHeight", .5, 20, 3);
        arrowLength_ = spin(sf, "Lunghezza freccia (mm):", "templateArrowLength", .5, 20, 2);
        arrow_ = new QComboBox(this);
        arrow_->addItems({"Aperta", "Piena", "Chiusa", "Tratto"});
        sf->addRow("Freccia:", arrow_);
        status_ = new QLabel(this);
        status_->setObjectName("templateStatus");
        status_->setWordWrap(true);
        root->addWidget(status_);
        auto *footer = new QHBoxLayout;
        root->addLayout(footer);
        button(footer, "Apri template...", "templateOpen", [this] {
            const auto path = QFileDialog::getOpenFileName(this, "Apri template", path_, "Template ForgeCAD (*.drwt)");
            if (path.isEmpty())
                return;
            DrawingSheetTemplate t;
            auto e = loadDrawingTemplate(path, t);
            if (!e.isEmpty()) {
                report(e);
                return;
            }
            preview_->resize();
            applyDrawingTemplate(draft_, t);
            path_ = path;
            loadControls();
            checkpoint();
            report({}, "Template caricato: " + path);
        });
        button(footer, "Salva template...", "templateSave", [this] { save(); });
        footer->addStretch();
        auto *apply = button(footer, "Applica al foglio corrente", "templateApply", [this] {
            if (valid() && target_) {
                const auto e = target_->applyPageTemplate(drawingTemplateOf(draft_));
                if (e.isEmpty())
                    checkpoint();
                report(e, "Template applicato al foglio. Annullabile con Undo.");
            }
        });
        apply->setEnabled(target && !target->isReadOnly());
        button(footer, "Chiudi", "templateClose", [this] {
            if (discard())
                accept();
        });
        connect(items_, &QListWidget::currentRowChanged, this, [this] {
            if (!updating_)
                select();
        });
        const auto property = [this] {
            if (updating_)
                return;
            auto *i = selected();
            if (!i)
                return;
            i->rect = {QPointF(x_->value(), y_->value()) - origin(), QSizeF(w_->value(), h_->value())};
            i->text = text_->text();
            i->textHeight = height_->value();
            i->lineWidth = line_->value();
            i->bold = bold_->isChecked();
            i->alignment = alignment_->currentIndex();
            updateHandle();
            refresh();
        };
        for (auto *s : {x_, y_, w_, h_, height_, line_})
            connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, property);
        connect(text_, &QLineEdit::textChanged, this, property);
        connect(bold_, &QCheckBox::toggled, this, property);
        connect(alignment_, qOverload<int>(&QComboBox::currentIndexChanged), this, property);
        connect(field_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
            if (i > 0) {
                text_->insert("{" + field_->currentText() + "}");
                field_->setCurrentIndex(0);
            }
        });
        const auto page = [this] {
            if (updating_)
                return;
            preview_->resize();
            draft_.size = {paperWidth_->value(), paperHeight_->value()};
            draft_.scale = scale_->value();
            draft_.titleBlock.visible = visible_->isChecked();
            draft_.titleBlock.code = code_->text();
            draft_.titleBlock.organization = organization_->text();
            draft_.titleBlock.author = author_->text();
            draft_.titleBlock.date = date_->text();
            scene_->setSceneRect(QRectF(QPointF(), draft_.size));
            select();
            refresh();
        };
        for (auto *s : {paperWidth_, paperHeight_, scale_})
            connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, page);
        for (auto *e : {code_, organization_, author_, date_})
            connect(e, &QLineEdit::textChanged, this, page);
        connect(visible_, &QCheckBox::toggled, this, page);
        const auto paper = [this, page] {
            if (updating_)
                return;
            const QSizeF sizes[] = {{210, 297}, {297, 420}, {420, 594}, {594, 841}, {841, 1189}};
            const bool custom = format_->currentIndex() == 5;
            paperWidth_->setEnabled(custom);
            paperHeight_->setEnabled(custom);
            orientation_->setEnabled(!custom);
            if (!custom) {
                auto size = sizes[format_->currentIndex()];
                if (orientation_->currentIndex() == 0)
                    size.transpose();
                updating_ = true;
                paperWidth_->setValue(size.width());
                paperHeight_->setValue(size.height());
                updating_ = false;
            }
            page();
            fit();
        };
        connect(format_, qOverload<int>(&QComboBox::currentIndexChanged), this, paper);
        connect(orientation_, qOverload<int>(&QComboBox::currentIndexChanged), this, paper);
        const auto styles = [this] {
            if (updating_)
                return;
            draft_.style.fontFamily = font_->currentFont().family();
            draft_.style.dimensionHeight = dimensionHeight_->value();
            draft_.style.arrowLength = arrowLength_->value();
            draft_.style.arrow = DrawingArrowStyle(arrow_->currentIndex());
            refresh();
        };
        connect(font_, &QFontComboBox::currentFontChanged, this, styles);
        connect(dimensionHeight_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, styles);
        connect(arrowLength_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, styles);
        connect(arrow_, qOverload<int>(&QComboBox::currentIndexChanged), this, styles);
        split->setStretchFactor(0, 1);
        split->setStretchFactor(1, 0);
        loadControls();
        checkpoint();
        QTimer::singleShot(0, this, [this] { fit(); });
    }
};
} // namespace
void showDrawingTemplateEditor(QWidget *parent, DrawingSheetEditor *sheet) {
    TemplateEditor editor(parent, sheet);
    editor.exec();
}
} // namespace ForgeCad
