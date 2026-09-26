#ifndef FORGECAD_ICONS_H
#define FORGECAD_ICONS_H

#include <QIcon>
#include <QString>
#include <QStringList>

// Icone dei comandi, disegnate con QPainter (vettoriali: nitide a qualsiasi
// dimensione e rapporto di pixel, niente file esterni). Colori per il tema
// scuro dell'interfaccia; disattivate in grigio.
namespace ForgeCad {

// Icona del comando `name` (vedi commandIconNames); vuota se il nome non esiste.
QIcon commandIcon(const QString &name);
QStringList commandIconNames();

}

#endif
