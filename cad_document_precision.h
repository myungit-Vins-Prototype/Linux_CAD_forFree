#pragma once
class QWidget;
class CadViewport;
namespace ForgeCad {
// Profilo di calcolo del documento: modifica esplicita con ricalcolo e Undo.
void editDocumentPrecision(QWidget *parent, CadViewport *viewport);
}
