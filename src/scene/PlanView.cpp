#include "PlanView.h"
#include "PlanScene.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

// Debug output of the navigation input:  QT_LOGGING_RULES="shp.input.debug=true"
Q_LOGGING_CATEGORY(lcInput, "shp.input", QtWarningMsg)

PlanView::PlanView(PlanScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent), m_scene(scene)
{
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    setDragMode(RubberBandDrag);
    setViewportUpdateMode(FullViewportUpdate);
    setAcceptDrops(true);
    setMouseTracking(true);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(Qt::white);
    scale(0.5, 0.5);
    setPlanScene(scene);
}

void PlanView::setPlanScene(PlanScene *scene)
{
    if (m_scene && m_scene != scene)
        disconnect(m_scene, nullptr, this, nullptr);
    m_scene = scene;
    setScene(scene);
    scaleChanged();
    auto applyTool = [this](PlanScene::Tool t) {
        setDragMode(t == PlanScene::SelectTool ? RubberBandDrag : NoDrag);
        viewport()->setCursor(t == PlanScene::SelectTool ? Qt::ArrowCursor : Qt::CrossCursor);
    };
    connect(scene, &PlanScene::toolChanged, this, applyTool);
    applyTool(scene->tool());
}

double PlanView::zoom() const
{
    return transform().m11();
}

void PlanView::scaleChanged()
{
    m_scene->setViewScale(zoom());
    emit zoomChanged(zoom());
}

void PlanView::zoomBy(double factor)
{
    const double z = std::clamp(zoom() * factor, 0.002, 40.0);
    factor = z / zoom();
    if (std::abs(factor - 1.0) > 1e-9)
        scale(factor, factor);
    scaleChanged();
}

void PlanView::zoomToFit()
{
    QRectF r = m_scene->contentRect();
    if (r.isNull())
        r = QRectF(-200, -200, 1400, 1000);
    fitInView(r.adjusted(-100, -100, 100, 100), Qt::KeepAspectRatio);
    scaleChanged();
}

void PlanView::wheelEvent(QWheelEvent *e)
{
    const bool zoomModifier = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    // Trackpads (and the Magic Mouse) send scroll phases (begin / update / end / momentum).
    // A classic mouse wheel never does - on macOS it can still carry a pixel delta, so that is not a criterion.
    const bool trackpad = e->phase() != Qt::NoScrollPhase;
    qCDebug(lcInput) << "wheel" << "angle" << e->angleDelta() << "pixel" << e->pixelDelta() << "phase" << e->phase()
                     << "mods" << e->modifiers() << (trackpad ? "-> trackpad" : "-> mouse wheel");

    if (trackpad && !zoomModifier) {
        QPoint d = e->pixelDelta();
        if (d.isNull())
            d = e->angleDelta() / 8;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        e->accept();
        return;
    }
    if (!zoomModifier && (e->modifiers() & Qt::ShiftModifier)) {
        const int d = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d / 2);
        e->accept();
        return;
    }
    double delta = e->angleDelta().y();
    if (delta == 0)
        delta = e->angleDelta().x();
    if (trackpad && !e->pixelDelta().isNull())
        delta = e->pixelDelta().y() * 4.0;      // Cmd/Ctrl + two-finger scroll
    if (delta == 0) {
        e->ignore();
        return;
    }
    zoomBy(std::pow(1.0015, delta));            // one wheel notch (120) = ~20 %
    e->accept();
}

bool PlanView::viewportEvent(QEvent *e)
{
    if (e->type() == QEvent::NativeGesture) {
        auto *g = static_cast<QNativeGestureEvent *>(e);
        qCDebug(lcInput) << "gesture" << g->gestureType() << g->value();
        switch (g->gestureType()) {
        case Qt::ZoomNativeGesture: {
            // keep the point under the fingers fixed
            const QPoint at = g->position().toPoint();
            const QPointF before = mapToScene(at);
            const ViewportAnchor old = transformationAnchor();
            setTransformationAnchor(NoAnchor);
            zoomBy(1.0 + g->value());
            setTransformationAnchor(old);
            const QPoint moved = mapFromScene(before) - at;
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() + moved.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() + moved.y());
            return true;
        }
        case Qt::SmartZoomNativeGesture:
            zoomToFit();
            return true;
        default:
            break;
        }
    }
    return QGraphicsView::viewportEvent(e);
}

void PlanView::mousePressEvent(QMouseEvent *e)
{
    qCDebug(lcInput) << "press" << e->button() << "tool" << m_scene->tool() << "space" << m_spaceDown;
    // right button pans in Select mode and in the Room / Wall tools when nothing is being drawn
    // (while drawing it finishes the shape); a short right click opens the item menu
    const PlanScene::Tool tool = m_scene->tool();
    const bool idle = tool == PlanScene::SelectTool
                      || ((tool == PlanScene::RoomTool || tool == PlanScene::WallTool) && !m_scene->isDrawing());
    const bool rightPan = e->button() == Qt::RightButton && idle;
    if (e->button() == Qt::MiddleButton || rightPan || (m_spaceDown && e->button() == Qt::LeftButton)) {
        m_panning = true;
        m_panButton = e->button();
        m_panStart = e->pos();
        m_pressPos = e->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void PlanView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_panning) {
        const QPoint d = e->pos() - m_panStart;
        m_panStart = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void PlanView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning && e->button() == m_panButton) {
        m_panning = false;
        m_panButton = Qt::NoButton;
        if (e->button() == Qt::RightButton && (e->pos() - m_pressPos).manhattanLength() < 5) {
            // a short right click (no drag) opens the menu of the item under the cursor
            viewport()->setCursor(Qt::ArrowCursor);
            m_scene->showContextMenu(mapToScene(e->pos()), e->globalPosition().toPoint());
            e->accept();
            return;
        }
        viewport()->setCursor(m_spaceDown ? Qt::OpenHandCursor
                              : m_scene->tool() == PlanScene::SelectTool ? Qt::ArrowCursor : Qt::CrossCursor);
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void PlanView::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = true;
        viewport()->setCursor(Qt::OpenHandCursor);
        e->accept();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void PlanView::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = false;
        viewport()->setCursor(m_scene->tool() == PlanScene::SelectTool ? Qt::ArrowCursor : Qt::CrossCursor);
        e->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(e);
}

void PlanView::contextMenuEvent(QContextMenuEvent *e)
{
    // Right click: drawing tools finish the shape; in Select mode the menu opens on release
    // (so that a right drag can pan). Ctrl+click on macOS arrives here without a right press.
    const PlanScene::Tool tool = m_scene->tool();
    const bool idle = tool == PlanScene::SelectTool
                      || ((tool == PlanScene::RoomTool || tool == PlanScene::WallTool) && !m_scene->isDrawing());
    if (idle && e->reason() == QContextMenuEvent::Mouse
        && !(QGuiApplication::mouseButtons() & Qt::RightButton) && !m_panning)
        m_scene->showContextMenu(mapToScene(e->pos()), e->globalPos());
    e->accept();
}
