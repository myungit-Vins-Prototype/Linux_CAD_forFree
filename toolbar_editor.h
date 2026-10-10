#pragma once

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDropEvent>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QWidgetAction>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <memory>

// One editor per toolbar. QAction identities are shared with the menus, so
// shortcuts, checked state and command availability remain unchanged.
class ToolbarEditor : public QObject {
public:
    static void refreshVisibility(QToolBar *bar) {
        if (!bar->property("toolbarManagedVisibility").toBool()) return;
        const bool enabled = QSettings().value(QStringLiteral("toolbar/visible/") + bar->objectName(), true).toBool();
        bar->setVisible(enabled && !bar->property("toolbarModeHidden").toBool()
                        && (!bar->property("toolbarHideWhenEmpty").toBool() || !bar->actions().isEmpty()));
    }
    using Commands = QVector<QPair<QString, QAction *>>;
    ToolbarEditor(QToolBar *bar, Commands commands, std::function<bool()> locked,
                  std::function<void()> saveCustom = {})
        : QObject(bar), bar_(bar), locked_(std::move(locked)), saveCustom_(std::move(saveCustom)) {
        for (const auto &command : commands)
            if (!keys_.contains(command.second)) {
                keys_.insert(command.second, QStringLiteral("command/") + command.first);
                available_.append({QStringLiteral("command/") + command.first, command.second});
                labels_.insert(command.second, command.first);
            }
        int index = 0;
        for (auto *action : bar_->actions()) {
            if (!keys_.contains(action)) {
                const QString key = QStringLiteral("builtin/%1").arg(index);
                keys_.insert(action, key);
                available_.append({key, action});
                auto *button = qobject_cast<QToolButton *>(bar_->widgetForAction(action));
                labels_.insert(action, action->isSeparator() ? QStringLiteral("— Separatore —")
                    : button ? button->toolTip().section(QLatin1Char('\n'), -1) : action->text());
            }
            defaults_.append(keys_.value(action));
            ++index;
        }
        if (QSettings().contains(settingsKey()))
            apply(QSettings().value(settingsKey()).toStringList(), false);
        bar_->setAcceptDrops(true);
        bar_->installEventFilter(this);
        watchButtons();
    }

    void edit() {
        if (locked_()) return;
        QDialog dialog(bar_->window());
        dialog.setWindowTitle(QStringLiteral("Personalizza: %1").arg(bar_->windowTitle()));
        auto *layout = new QVBoxLayout(&dialog);
        auto *hint = new QLabel(QStringLiteral("Spunta i comandi da mostrare. Trascina le righe oppure usa Su e Giù per cambiarne l'ordine. Puoi anche trascinare i pulsanti direttamente nella barra quando è sbloccata."), &dialog);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *filter = new QLineEdit(&dialog);
        filter->setPlaceholderText(QStringLiteral("Cerca un comando..."));
        layout->addWidget(filter);
        auto *list = new QListWidget(&dialog);
        list->setObjectName(QStringLiteral("toolbarEditorCommands"));
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        layout->addWidget(list);
        const auto populate = [this, list](const QStringList &selected) {
            list->clear();
            QStringList order = selected;
            for (const auto &entry : available_) if (!order.contains(entry.first)) order.append(entry.first);
            for (const QString &key : order) {
                QAction *action = resolveCommand(key);
                const auto group = groupData(key);
                if (!action && group.isEmpty()) continue;
                auto *item = new QListWidgetItem(action ? action->icon() : QIcon(),
                    group.isEmpty() ? labels_.value(action) : group.first().toString() + QStringLiteral(" ▾"), list);
                item->setData(Qt::UserRole, key);
                item->setCheckState(selected.contains(key) ? Qt::Checked : Qt::Unchecked);
            }
        };
        populate(currentKeys());
        auto *groupRow = new QHBoxLayout;
        auto *groupName = new QLineEdit(&dialog);
        groupName->setObjectName(QStringLiteral("toolbarGroupName"));
        groupName->setPlaceholderText(QStringLiteral("Nome del gruppo"));
        auto *groupButton = new QPushButton(QStringLiteral("Raggruppa selezionati"), &dialog);
        groupButton->setObjectName(QStringLiteral("toolbarGroupCreate"));
        auto *ungroup = new QPushButton(QStringLiteral("Separa gruppo"), &dialog);
        ungroup->setObjectName(QStringLiteral("toolbarGroupSeparate"));
        groupRow->addWidget(groupName);
        groupRow->addWidget(groupButton);
        groupRow->addWidget(ungroup);
        layout->addLayout(groupRow);
        auto *groupHint = new QLabel(QStringLiteral("Seleziona più righe con Ctrl/Cmd o Maiusc, scrivi un nome e premi Raggruppa selezionati. Le spunte indicano i pulsanti visibili."), &dialog);
        groupHint->setWordWrap(true);
        layout->addWidget(groupHint);
        connect(groupButton, &QPushButton::clicked, &dialog, [this, list, groupName] {
            QStringList members;
            QList<QListWidgetItem *> selected;
            int position = -1;
            for (int row = 0; row < list->count(); ++row) {
                auto *item = list->item(row);
                if (!item->isSelected() || item->isHidden()) continue;
                const QString key = item->data(Qt::UserRole).toString();
                const auto commands = groupMembers(key);
                if (commands.isEmpty()) continue;
                if (position < 0) position = row;
                selected.append(item);
                for (const auto &command : commands) if (!members.contains(command)) members.append(command);
            }
            if (members.size() < 2) return;
            const QString name = groupName->text().trimmed().isEmpty() ? QStringLiteral("Gruppo comandi") : groupName->text().trimmed();
            for (auto *item : selected) delete list->takeItem(list->row(item));
            auto *item = new QListWidgetItem(name + QStringLiteral(" ▾"));
            item->setData(Qt::UserRole, groupKey(name, members));
            item->setCheckState(Qt::Checked);
            item->setToolTip(members.join(QLatin1Char('\n')));
            list->insertItem(position, item);
            list->setCurrentItem(item);
        });
        connect(ungroup, &QPushButton::clicked, &dialog, [this, list] {
            const auto selected = list->selectedItems();
            for (auto *item : selected) {
                const QString key = item->data(Qt::UserRole).toString();
                if (groupData(key).isEmpty()) continue;
                int row = list->row(item);
                const auto state = item->checkState();
                delete list->takeItem(row);
                for (const auto &member : groupMembers(key)) {
                    auto *action = resolveCommand(member);
                    if (!action) continue;
                    // A command may also appear separately in the checklist.
                    for (int i = list->count() - 1; i >= 0; --i) {
                        if (list->item(i)->data(Qt::UserRole).toString() == member) {
                            delete list->takeItem(i);
                            if (i < row) --row;
                        }
                    }
                    auto *command = new QListWidgetItem(action->icon(), labels_.value(action));
                    command->setData(Qt::UserRole, member);
                    command->setCheckState(state);
                    list->insertItem(row++, command);
                }
            }
        });
        auto *row = new QHBoxLayout;
        for (int step : {-1, 1}) {
            auto *button = new QPushButton(step < 0 ? QStringLiteral("Su") : QStringLiteral("Giù"), &dialog);
            row->addWidget(button);
            connect(button, &QPushButton::clicked, &dialog, [list, step] {
                const int source = list->currentRow(), target = source + step;
                if (source < 0 || target < 0 || target >= list->count()) return;
                auto *item = list->takeItem(source);
                list->insertItem(target, item);
                list->setCurrentRow(target);
            });
        }
        auto *reset = new QPushButton(QStringLiteral("Ripristina questa barra"), &dialog);
        row->addWidget(reset);
        layout->addLayout(row);
        connect(reset, &QPushButton::clicked, &dialog, [this, populate, filter] { filter->clear(); populate(defaults_); });
        connect(filter, &QLineEdit::textChanged, &dialog, [list](const QString &text) {
            for (int i = 0; i < list->count(); ++i)
                list->item(i)->setHidden(!list->item(i)->text().contains(text, Qt::CaseInsensitive));
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(650, 600);
        if (dialog.exec() != QDialog::Accepted || locked_()) return;
        QStringList selected;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Checked) selected.append(list->item(i)->data(Qt::UserRole).toString());
        apply(selected, true);
    }

    bool moveAction(QAction *action, QAction *before) {
        if (locked_() || !bar_->actions().contains(action) || action == before
            || (before && !bar_->actions().contains(before))) return false;
        // insertAction moves an existing action, including QWidgetAction flyouts.
        bar_->insertAction(before, action);
        save();
        watchButtons();
        return true;
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::ActionAdded) {
            // Tool buttons are created after the action event is processed.
            QTimer::singleShot(0, this, [this] { watchButtons(); });
        }
        auto *button = qobject_cast<QToolButton *>(object);
        if (button && event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            pressed_ = !locked_() && mouse->button() == Qt::LeftButton ? button : nullptr;
            pressPosition_ = mouse->position().toPoint();
        } else if (button && event->type() == QEvent::MouseMove && pressed_ == button && !locked_()) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if ((mouse->buttons() & Qt::LeftButton)
                && (mouse->position().toPoint() - pressPosition_).manhattanLength() >= QApplication::startDragDistance()) {
                dragged_ = nullptr;
                for (auto *action : bar_->actions()) if (bar_->widgetForAction(action) == button) dragged_ = action;
                pressed_ = nullptr;
                if (!dragged_) return false;
                button->setDown(false);
                QDrag drag(bar_);
                auto *mime = new QMimeData;
                mime->setData("application/x-forgecad-toolbar-button", "move");
                drag.setMimeData(mime);
                drag.setPixmap(button->grab());
                drag.exec(Qt::MoveAction);
                dragged_ = nullptr;
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            pressed_ = nullptr;
        }
        if (object == bar_ && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop)) {
            auto *drop = static_cast<QDropEvent *>(event);
            if (locked_() || !dragged_ || drop->source() != bar_) return false;
            drop->setDropAction(Qt::MoveAction);
            drop->accept();
            if (event->type() == QEvent::Drop) {
                QAction *before = nullptr;
                const bool horizontal = bar_->orientation() == Qt::Horizontal;
                const int position = horizontal ? drop->position().x() : drop->position().y();
                for (auto *action : bar_->actions()) {
                    if (action == dragged_ || !action->isVisible()) continue;
                    const QRect rect = bar_->actionGeometry(action);
                    if (rect.isEmpty()) continue;
                    const int center = horizontal ? rect.center().x() : rect.center().y();
                    if (horizontal && bar_->layoutDirection() == Qt::RightToLeft ? position > center : position < center) {
                        before = action;
                        break;
                    }
                }
                moveAction(dragged_, before);
            }
            return true;
        }
        return QObject::eventFilter(object, event);
    }

private:
    QString settingsKey() const { return QStringLiteral("toolbar/layout/") + bar_->objectName(); }
    static QJsonArray groupData(const QString &key) {
        if (!key.startsWith(QStringLiteral("group/"))) return {};
        const auto array = QJsonDocument::fromJson(QByteArray::fromBase64(key.mid(6).toLatin1())).array();
        return array.size() >= 3 ? array : QJsonArray();
    }
    static QString groupKey(const QString &name, const QStringList &members) {
        QJsonArray data{name};
        for (const auto &member : members) data.append(member);
        return QStringLiteral("group/") + QString::fromLatin1(QJsonDocument(data).toJson(QJsonDocument::Compact).toBase64());
    }
    QStringList groupMembers(const QString &key) const {
        const auto data = groupData(key);
        QStringList result;
        if (!data.isEmpty()) {
            for (int i = 1; i < data.size(); ++i) if (resolveCommand(data[i].toString())) result.append(data[i].toString());
        } else if (auto *action = resolveCommand(key)) {
            if (key.startsWith(QStringLiteral("command/"))) result.append(key);
            else if (auto *button = qobject_cast<QToolButton *>(bar_->widgetForAction(action))) {
                if (button->menu()) for (auto *member : button->menu()->actions())
                    if (keys_.value(member).startsWith(QStringLiteral("command/"))) result.append(keys_.value(member));
            }
        }
        return result;
    }
    QAction *resolveCommand(const QString &key) const {
        for (const auto &entry : available_) if (entry.first == key) return entry.second;
        return nullptr;
    }
    QStringList currentKeys() const {
        QStringList result;
        for (auto *action : bar_->actions()) if (keys_.contains(action)) result.append(keys_.value(action));
        return result;
    }
    void apply(const QStringList &keys, bool persist) {
        for (auto *action : bar_->actions()) bar_->removeAction(action);
        for (const auto &action : groups_) { keys_.remove(action.data()); delete action.data(); }
        groups_.clear();
        for (const QString &key : keys) {
            const auto data = groupData(key);
            if (data.isEmpty()) {
                if (auto *action = resolveCommand(key)) bar_->addAction(action);
                continue;
            }
            const auto members = groupMembers(key);
            if (members.isEmpty()) continue;
            auto *button = new QToolButton(bar_);
            auto *menu = new QMenu(button);
            for (const auto &member : members) menu->addAction(resolveCommand(member));
            button->setMenu(menu);
            button->setPopupMode(QToolButton::MenuButtonPopup);
            button->setAutoRaise(true);
            button->setIconSize(bar_->iconSize());
            const QString name = data.first().toString();
            auto current = std::make_shared<QPointer<QAction>>(menu->actions().first());
            const auto refresh = [button, menu, name, current] {
                if (!*current) return;
                button->setIcon((*current)->icon());
                button->setText((*current)->text());
                button->setToolTip(name + QStringLiteral("\n") + (*current)->text());
                bool enabled = false;
                for (auto *command : menu->actions()) enabled |= command->isEnabled() && command->isVisible();
                button->setEnabled(enabled);
                button->setCheckable((*current)->isCheckable());
                button->setChecked((*current)->isChecked());
            };
            refresh();
            connect(button, &QToolButton::clicked, button, [current, refresh] {
                if (*current && (*current)->isEnabled() && (*current)->isVisible()) (*current)->trigger();
                refresh();
            });
            connect(menu, &QMenu::triggered, button, [current, refresh](QAction *action) { *current = action; refresh(); });
            for (auto *command : menu->actions()) connect(command, &QAction::changed, button, refresh);
            connect(bar_, &QToolBar::iconSizeChanged, button, &QToolButton::setIconSize);
            auto *action = bar_->addWidget(button);
            action->setText(name);
            keys_.insert(action, key);
            groups_.append(action);
        }
        refreshVisibility(bar_);
        watchButtons();
        if (persist) save();
    }
    void save() {
        QSettings().setValue(settingsKey(), currentKeys());
        if (saveCustom_ && groups_.isEmpty()) saveCustom_();
    }
    void watchButtons() {
        for (auto *action : bar_->actions())
            if (auto *button = qobject_cast<QToolButton *>(bar_->widgetForAction(action))) button->installEventFilter(this);
    }
    QToolBar *bar_;
    std::function<bool()> locked_;
    std::function<void()> saveCustom_;
    QVector<QPair<QString, QPointer<QAction>>> available_;
    QHash<QAction *, QString> keys_, labels_;
    QStringList defaults_;
    QList<QPointer<QAction>> groups_;
    QPointer<QToolButton> pressed_;
    QPointer<QAction> dragged_;
    QPoint pressPosition_;
};
