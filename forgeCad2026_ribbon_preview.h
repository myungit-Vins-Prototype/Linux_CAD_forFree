#ifndef FORGECAD2026_RIBBON_PREVIEW_H
#define FORGECAD2026_RIBBON_PREVIEW_H
#include <QWidget>
#include <memory>
class QMainWindow;
class QAction;
namespace ForgeCad {
// Ribbon del viewport, condivisa tra app e runner con preferenze isolate.
class RibbonPreview final : public QWidget {
public:
    explicit RibbonPreview(QMainWindow *window);
    ~RibbonPreview() override;
    QString selectedTab() const;
    bool compact() const;
    void setCompact(bool compact);
    void syncContext();
    void selectForAction(QAction *action);
    void runSelfTest();
protected:
    bool eventFilter(QObject *object,QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
