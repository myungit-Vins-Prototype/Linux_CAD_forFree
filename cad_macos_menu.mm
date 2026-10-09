#include "cad_macos_menu.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>

#import <AppKit/AppKit.h>

namespace {
void showNativeImages(NSMenu *menu) {
    // API pubblica introdotta in macOS 27. La ricerca a runtime permette
    // anche di compilare con SDK precedenti e di eseguire su macOS meno recenti.
    const SEL setter = NSSelectorFromString(@"setPreferredImageVisibility:");
    for (NSMenuItem *item in menu.itemArray) {
        if (item.image && [item respondsToSelector:setter]) {
            using SetVisibility = void (*)(id, SEL, NSInteger);
            const auto setVisibility = reinterpret_cast<SetVisibility>([item methodForSelector:setter]);
            // NSMenuItemImageVisibilityVisible = 1 (AppKit/NSMenuItem.h).
            setVisibility(item, setter, 1);
        }

    }
}

void prepareMenu(QMenu *menu) {
    for (QAction *action : menu->actions()) {
        if (!action->icon().isNull()) action->setIconVisibleInMenu(true);

    }
    showNativeImages(menu->toNSMenu());
}

void watchMenu(QMenu *menu) {
    // Non sostituisce il delegate Cocoa di Qt. Riapertura e aggiornamenti
    // delle azioni riapplicano la preferenza alle immagini native correnti.
    prepareMenu(menu);
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] { prepareMenu(menu); });
    for (QAction *action : menu->actions())
        if (QMenu *submenu = action->menu()) watchMenu(submenu);
}
}

void ForgeCad::enableMacMenuIcons(QMenuBar *menuBar) {
    menuBar->setNativeMenuBar(true);
    for (QAction *action : menuBar->actions()) {
        if (QMenu *menu = action->menu()) {
            watchMenu(menu);

        }
    }
}
