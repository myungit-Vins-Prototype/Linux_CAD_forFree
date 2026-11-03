#ifndef FORGECAD_STORYBOARD_SELECTION_H
#define FORGECAD_STORYBOARD_SELECTION_H
class QMainWindow;
class CadViewport;
class StoryboardTree;
namespace ForgeCad {
// Collega la storyboard ai picker dei pannelli senza attivare comandi di modifica.
void installStoryboardSelection(QMainWindow *window, CadViewport *viewport, StoryboardTree *tree);
}
#endif
