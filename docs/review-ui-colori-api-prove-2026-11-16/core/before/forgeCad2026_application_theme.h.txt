#ifndef FORGECAD2026_APPLICATION_THEME_H
#define FORGECAD2026_APPLICATION_THEME_H

class QApplication;
class QMenu;

namespace ForgeCad {
enum class ApplicationThemeMode { Desktop, Light, Dark };

// Preferenza persistente; Desktop e' il valore predefinito.
ApplicationThemeMode applicationThemeMode();
void setApplicationThemeMode(QApplication &application, ApplicationThemeMode mode);
void setupApplicationThemeMenu(QMenu &menu);

// Stile comune e palette dell'app; segue la preferenza o il chiaro/scuro del sistema.
// Chiamare dopo QApplication e prima di costruire le finestre. Idempotente.
void initializeApplicationTheme(QApplication &application);
}

#endif
