#ifndef FORGECAD2026_APPLICATION_THEME_H
#define FORGECAD2026_APPLICATION_THEME_H

class QApplication;

namespace ForgeCad {
// Stile comune e palette dell'app; dal sistema segue solo il chiaro/scuro.
// Chiamare dopo QApplication e prima di costruire le finestre. Idempotente.
void initializeApplicationTheme(QApplication &application);
}

#endif
