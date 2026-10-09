#ifndef PLANVIEW_H
#define PLANVIEW_H

#include <QGraphicsView>

class PlanScene;

// Navigation
//   mouse:    wheel = zoom under the cursor, Shift+wheel = horizontal pan,
//             drag with middle / right button (right only in Select mode) or Space + drag = pan
//   trackpad: two-finger scroll = pan, pinch = zoom, Cmd/Ctrl + scroll = zoom,
//             two-finger double tap = zoom to fit
class PlanView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit PlanView(PlanScene *scene, QWidget *parent = nullptr);

    void setPlanScene(PlanScene *scene);
    PlanScene *planScene() const { return m_scene; }
    void zoomBy(double factor);
    void zoomToFit();
    double zoom() const;

signals:
    void zoomChanged(double scale);

protected:
    void wheelEvent(QWheelEvent *e) override;
    bool viewportEvent(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    void scaleChanged();
    PlanScene *m_scene;
    bool m_panning = false;
    Qt::MouseButton m_panButton = Qt::NoButton;
    QPoint m_pressPos;          // to tell a right click (menu) from a right drag (pan)
    bool m_spaceDown = false;
    QPoint m_panStart;
};

#endif // PLANVIEW_H
