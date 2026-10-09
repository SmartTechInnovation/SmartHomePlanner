#include "HandleItem.h"
#include "PlanScene.h"

#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QPainter>
#include <QTimer>
#include <algorithm>

static constexpr double kHandlePx = 9.0;

HandleItem::HandleItem(PolyItem *owner, int index)
    : QGraphicsItem(owner), m_owner(owner), m_index(index)
{
    // Not ItemIsMovable: Qt would also move every *selected* movable item (the room itself).
    // The handle is dragged by hand in the mouse events below.
    setFlags(ItemIgnoresTransformations);
    setZValue(1000);
    setCursor(Qt::SizeAllCursor);
    setAcceptedMouseButtons(Qt::LeftButton);
}

QRectF HandleItem::boundingRect() const
{
    return QRectF(-kHandlePx / 2, -kHandlePx / 2, kHandlePx, kHandlePx);
}

void HandleItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    auto *ps = qobject_cast<PlanScene *>(scene());
    if (ps && ps->isExporting())
        return;
    p->setPen(QPen(QColor(0, 120, 215), 1));
    p->setBrush(Qt::white);
    p->drawRect(boundingRect().adjusted(0.5, 0.5, -0.5, -0.5));
}

void HandleItem::dragTo(const QPointF &scenePos, bool snap)
{
    if (!m_owner)
        return;
    QPointF sp = scenePos;
    if (auto *ps = qobject_cast<PlanScene *>(scene())) {
        PlanScene::SnapKind kind = PlanScene::NoSnap;
        if (snap)   // room corners snap to the wall faces, wall vertices to other wall vertices
            sp = ps->snapPoint(sp, m_owner, true, m_owner->type() != ItemType::Wall, &kind);
        if (snap && (kind == PlanScene::NoSnap || kind == PlanScene::GridSnap)) {
            // horizontal / vertical with the neighbouring corners (and other outlines, lights for strips)
            const QPolygonF pts = m_owner->scenePoints();
            QVector<QPointF> refs;
            const int n = pts.size();
            if (n > 1) {
                if (m_index > 0 || m_owner->isClosed())
                    refs << pts[(m_index - 1 + n) % n];
                if (m_index < n - 1 || m_owner->isClosed())
                    refs << pts[(m_index + 1) % n];
            }
            refs += ps->alignRefs(m_owner, m_owner->type() == ItemType::Strip, true);
            sp = ps->alignToAxes(kind == PlanScene::GridSnap ? scenePos : sp, refs);
        } else {
            ps->hideGuides();
        }
        ps->showSnapMarker(sp, kind);
    }
    const QPointF local = m_owner->mapFromScene(sp);
    m_owner->setPoint(m_index, local);
    m_updating = true;
    setPos(local);
    m_updating = false;
}

void HandleItem::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        e->ignore();
        return;
    }
    m_dragging = true;
    m_moved = false;
    m_grabOffset = scenePos() - e->scenePos();
    e->accept();
}

void HandleItem::mouseMoveEvent(QGraphicsSceneMouseEvent *e)
{
    if (!m_dragging)
        return;
    if (!m_moved && (e->screenPos() - e->buttonDownScreenPos(Qt::LeftButton)).manhattanLength() < 3)
        return;
    m_moved = true;
    dragTo(e->scenePos() + m_grabOffset, !(e->modifiers() & Qt::AltModifier));
    e->accept();
}

void HandleItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *e)
{
    const bool moved = m_dragging && m_moved;
    m_dragging = m_moved = false;
    e->accept();
    if (auto *ps = qobject_cast<PlanScene *>(scene())) {
        ps->showSnapMarker(QPointF(), PlanScene::NoSnap);
        if (moved)
            ps->commit(QObject::tr("Edit points"));
    }
}

void HandleItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e)
{
    e->accept();
    auto *ps = qobject_cast<PlanScene *>(scene());
    PolyItem *owner = m_owner;
    const int index = m_index;
    if (!ps || !owner->canRemovePoint())
        return;
    // the handle itself gets deleted by removePoint(): do it after this event
    QTimer::singleShot(0, ps, [ps, owner, index] {
        if (!ps->items().contains(owner))
            return;
        if (owner->removePoint(index))
            ps->commit(QObject::tr("Delete corner"));
    });
}

// ---------------------------------------------------------------- PolyItem

PolyItem::PolyItem(QGraphicsItem *parent) : QGraphicsItem(parent)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
}

PolyItem::~PolyItem() = default;

void PolyItem::setPoints(const QPolygonF &pts)
{
    prepareGeometryChange();
    m_pts = pts;
    pointsChanged();
    updateHandles();
    update();
}

void PolyItem::setPoint(int i, const QPointF &p)
{
    if (i < 0 || i >= m_pts.size())
        return;
    prepareGeometryChange();
    m_pts[i] = p;
    pointsChanged();
    update();
}

void PolyItem::normalizeOrigin()
{
    if (m_pts.isEmpty())
        return;
    const QPointF o = m_pts.first();
    if (o.isNull())
        return;
    prepareGeometryChange();
    m_pts.translate(-o);
    setPos(pos() + o);
    updateHandles();
}

void PolyItem::updateHandles()
{
    const bool want = showHandles() && scene();
    if (!want) {
        qDeleteAll(m_handles);
        m_handles.clear();
        return;
    }
    while (m_handles.size() > m_pts.size())
        delete m_handles.takeLast();
    while (m_handles.size() < m_pts.size())
        m_handles.append(new HandleItem(this, m_handles.size()));
    for (int i = 0; i < m_handles.size(); ++i) {
        HandleItem *h = m_handles[i];
        h->m_index = i;
        h->m_updating = true;
        h->setPos(m_pts[i]);
        h->m_updating = false;
    }
}

int PolyItem::edgeAt(const QPointF &scenePos, double tolerance, QPointF *projected) const
{
    const QPointF p = mapFromScene(scenePos);
    const int n = m_pts.size();
    const int edges = (m_closed && n > 2) ? n : n - 1;
    int best = -1;
    double bestD = tolerance;
    for (int i = 0; i < edges; ++i) {
        const QPointF a = m_pts[i];
        const QPointF b = m_pts[(i + 1) % n];
        const QPointF d = b - a;
        const double len2 = d.x() * d.x() + d.y() * d.y();
        if (len2 < 1e-9)
            continue;
        const double t = std::clamp(((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / len2, 0.0, 1.0);
        const QPointF q = a + d * t;
        const double dist = QLineF(p, q).length();
        if (dist < bestD) {
            bestD = dist;
            best = i;
            if (projected)
                *projected = q;
        }
    }
    return best;
}

bool PolyItem::insertPointAt(const QPointF &scenePos, double tolerance)
{
    QPointF q;
    const int edge = edgeAt(scenePos, tolerance, &q);
    if (edge < 0)
        return false;
    prepareGeometryChange();
    m_pts.insert(edge + 1, q);
    pointsChanged();
    updateHandles();
    update();
    return true;
}

bool PolyItem::removePoint(int index)
{
    if (index < 0 || index >= m_pts.size() || !canRemovePoint())
        return false;
    prepareGeometryChange();
    m_pts.remove(index);
    pointsChanged();
    updateHandles();
    update();
    return true;
}

void PolyItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e)
{
    auto *ps = qobject_cast<PlanScene *>(scene());
    if (ps && isSelected() && e->button() == Qt::LeftButton) {
        const double tol = 10.0 / std::max(1e-6, ps->viewScale()) + extraEdgeTolerance();
        if (insertPointAt(e->scenePos(), tol)) {
            ps->commit(QObject::tr("Add corner"));
            e->accept();
            return;
        }
    }
    QGraphicsItem::mouseDoubleClickEvent(e);
}

QVariant PolyItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSceneHasChanged)
        if (auto *ps = qobject_cast<PlanScene *>(scene()))
            ps->applyEditability(this);
    if (change == ItemSelectedHasChanged || change == ItemSceneHasChanged)
        updateHandles();
    if (change == ItemPositionHasChanged)
        pointsChanged();
    return QGraphicsItem::itemChange(change, value);
}
