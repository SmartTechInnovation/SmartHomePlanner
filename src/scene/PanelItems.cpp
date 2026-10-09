#include "PanelItems.h"

#include "PlanScene.h"
#include "core/IconCache.h"

#include <QCursor>
#include <QFontMetricsF>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPathStroker>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {

QFont mmFont(double px, bool bold = false)
{
    QFont f;
    f.setPixelSize(std::max(1, int(std::round(px))));
    f.setBold(bold);
    return f;
}

PlanScene *ps(const QGraphicsItem *it)
{
    return qobject_cast<PlanScene *>(it->scene());
}

double dimOpacity() { return 0.28; }

} // namespace

// ================================================================= RailItem

RailItem::RailItem(double length) : m_length(std::max(20.0, length))
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setZValue(ZOrder::Rail);
    setAcceptHoverEvents(true);
}

void RailItem::setLength(double l)
{
    prepareGeometryChange();
    m_length = std::max(20.0, l);
    update();
}

QRectF RailItem::boundingRect() const
{
    return QRectF(-2, -8, m_length + 4, kHeight + 10);
}

void RailItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r(0, 0, m_length, kHeight);
    QLinearGradient g(0, 0, 0, kHeight);
    g.setColorAt(0, QColor(215, 218, 222));
    g.setColorAt(0.18, QColor(245, 246, 247));
    g.setColorAt(0.22, QColor(170, 174, 180));
    g.setColorAt(0.5, QColor(200, 204, 209));
    g.setColorAt(0.78, QColor(170, 174, 180));
    g.setColorAt(0.82, QColor(245, 246, 247));
    g.setColorAt(1, QColor(205, 208, 212));
    PlanScene *s = ps(this);
    const bool exporting = s && s->isExporting();
    p->setPen(QPen(isSelected() && !exporting ? QColor(0, 120, 215) : QColor(120, 124, 130), isSelected() && !exporting ? 1.2 : 0.5));
    p->setBrush(g);
    p->drawRect(r);
    // mounting slots
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(140, 144, 150));
    for (double x = 12; x + 15 < m_length; x += 25)
        p->drawRoundedRect(QRectF(x, kHeight / 2 - 2.6, 15, 5.2), 2.6, 2.6);
    if (!m_label.isEmpty()) {
        p->setFont(mmFont(5, true));
        p->setPen(QColor(70, 70, 70));
        p->drawText(QRectF(0, -8, m_length, 7.5), Qt::AlignLeft | Qt::AlignBottom, m_label);
    }
    if (isSelected() && !exporting) {
        // resize grip at the right end
        p->setPen(QPen(QColor(0, 120, 215), 0.8));
        p->setBrush(Qt::white);
        p->drawRect(QRectF(m_length - 3, kHeight / 2 - 6, 6, 12));
    }
}

bool RailItem::nearEnd(const QPointF &local) const
{
    const double vs = ps(this) ? std::max(1e-6, ps(this)->viewScale()) : 1.0;
    return isSelected() && std::abs(local.x() - m_length) < std::max(4.0, 8.0 / vs) && local.y() > -2 && local.y() < kHeight + 2;
}

void RailItem::hoverMoveEvent(QGraphicsSceneHoverEvent *e)
{
    setCursor(nearEnd(e->pos()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
    QGraphicsItem::hoverMoveEvent(e);
}

void RailItem::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
    m_resizing = e->button() == Qt::LeftButton && nearEnd(e->pos());
    m_riders.clear();
    if (m_resizing) {
        e->accept();
        return;
    }
    // equipment sitting on this rail moves with it
    if (PlanScene *s = ps(this))
        for (QGraphicsItem *it : s->items(sceneBoundingRect().adjusted(0, -60, 0, 60)))
            if (it->type() == ItemType::Equipment && !it->isSelected()) {
                const QRectF er = it->sceneBoundingRect();
                if (std::abs(er.center().y() - centerY()) < 30 && er.center().x() >= scenePos().x() - 5
                    && er.center().x() <= scenePos().x() + m_length + 5)
                    m_riders << it;
            }
    QGraphicsItem::mousePressEvent(e);
}

void RailItem::mouseMoveEvent(QGraphicsSceneMouseEvent *e)
{
    if (m_resizing) {
        double l = std::round(std::max(20.0, e->pos().x()) / 5.0) * 5.0;
        setLength(l);
        return;
    }
    QGraphicsItem::mouseMoveEvent(e);
}

void RailItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *e)
{
    const bool resized = m_resizing;
    m_resizing = false;
    m_riders.clear();
    QGraphicsItem::mouseReleaseEvent(e);
    if (resized)
        if (PlanScene *s = ps(this))
            s->commit(QObject::tr("Rail length"));
}

QVariant RailItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionChange) {
        const QPointF np = value.toPointF();
        const QPointF snapped(std::round(np.x() / 5.0) * 5.0, std::round(np.y() / 5.0) * 5.0);
        const QPointF delta = snapped - pos();
        for (QGraphicsItem *r : std::as_const(m_riders))
            r->moveBy(delta.x(), delta.y());
        return snapped;
    }
    return QGraphicsItem::itemChange(change, value);
}

// ================================================================= EquipmentItem

EquipmentItem::EquipmentItem(const DeviceData &d, const QSizeF &sizeMm, const QVector<QPointF> &ports,
                             const QStringList &portNames)
    : m_d(d), m_size(sizeMm), m_ports(ports), m_portNames(portNames)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setZValue(ZOrder::Equipment);
    while (m_portNames.size() < m_ports.size())
        m_portNames << QString();
}

PlanScene *EquipmentItem::planScene() const
{
    return ps(this);
}

QString EquipmentItem::portName(int i) const
{
    const QString n = m_portNames.value(i);
    return n.isEmpty() ? QString::number(i + 1) : n;
}

int EquipmentItem::portAt(const QPointF &scenePos, double tolerance) const
{
    int best = -1;
    double bestD = tolerance;
    for (int i = 0; i < m_ports.size(); ++i) {
        const double d = QLineF(scenePos, portScenePos(i)).length();
        if (d <= bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

QRectF EquipmentItem::boundingRect() const
{
    const double w = std::max(m_size.width(), 34.0);
    const double x0 = (m_size.width() - w) / 2;
    return QRectF(x0 - 3, -10, w + 6, m_size.height() + 26);
}

QPainterPath EquipmentItem::shape() const
{
    QPainterPath p;
    p.addRect(QRectF(QPointF(0, 0), m_size));
    return p;
}

void EquipmentItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    PlanScene *s = planScene();
    const bool exporting = s && s->isExporting();
    const LinkState st = exporting ? LinkState::Normal : m_state;
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF body(QPointF(0, 0), m_size);
    if (IconCache::instance().has(m_d.iconPath)) {
        IconCache::instance().draw(p, m_d.iconPath, body);
    } else {
        p->setPen(QPen(QColor(90, 90, 90), 0.6));
        p->setBrush(QColor(245, 245, 245));
        p->drawRoundedRect(body, 1.5, 1.5);
    }

    // terminals
    for (int i = 0; i < m_ports.size(); ++i) {
        QColor fill = Qt::white;
        if (s) {
            for (WireItem *w : s->wires())
                if ((w->fromUid() == m_d.uid && w->fromPort() == i) || (w->toUid() == m_d.uid && w->toPort() == i)) {
                    fill = w->color();
                    break;
                }
        }
        p->setPen(QPen(QColor(40, 40, 40), 0.5));
        p->setBrush(fill);
        p->drawEllipse(m_ports[i], 2.2, 2.2);
        if (!m_portNames.value(i).isEmpty()) {
            p->setFont(mmFont(3));
            p->setPen(QColor(30, 30, 30));
            const bool up = portFacesUp(i);
            const QRectF tr(m_ports[i].x() - 6, m_ports[i].y() + (up ? 2.6 : -6.6), 12, 4);
            p->drawText(tr, Qt::AlignCenter, m_portNames[i]);
        }
    }

    // ID above, note below
    const double w = std::max(m_size.width(), 34.0);
    const double x0 = (m_size.width() - w) / 2;
    p->setFont(mmFont(5.5, true));
    p->setPen(QColor(20, 20, 20));
    p->drawText(QRectF(x0, -9, w, 8), Qt::AlignHCenter | Qt::AlignBottom, m_d.id);
    if (!m_d.note.isEmpty() && m_d.showNote && (!s || s->showNotes())) {
        p->setFont(mmFont(3.6));
        p->setPen(QColor(60, 60, 60));
        p->drawText(QRectF(x0, m_size.height() + 1, w, 15), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_d.note);
    }

    if (st == LinkState::Dim) {
        // faded, but still opaque (the rail must not show through)
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 170));
        p->drawRect(boundingRect());
    }
    if (!exporting && (isSelected() || st == LinkState::Selected || st == LinkState::Linked)) {
        const QColor c = st == LinkState::Linked ? QColor(20, 160, 70) : QColor(255, 140, 0);
        p->setPen(QPen(c, isSelected() ? 1.4 : 1.0, isSelected() ? Qt::SolidLine : Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawRect(body.adjusted(-1.5, -1.5, 1.5, 1.5));
    }
}

QVariant EquipmentItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    PlanScene *s = planScene();
    if (change == ItemPositionChange && s)
        return s->snapEquipment(this, value.toPointF());
    if ((change == ItemPositionHasChanged || change == ItemVisibleHasChanged) && s)
        s->scheduleLinkUpdate();
    return QGraphicsItem::itemChange(change, value);
}

// ================================================================= WireItem

WireItem::WireItem()
{
    setFlags(ItemIsSelectable);
    setZValue(ZOrder::Wire);
}

PlanScene *WireItem::planScene() const
{
    return ps(this);
}

QList<QPair<QString, QColor>> WireItem::standardColors()
{
    return {
        {QObject::tr("Brown (L / L1)"), QColor(139, 69, 19)},
        {QObject::tr("Black (L2)"), QColor(25, 25, 25)},
        {QObject::tr("Grey (L3)"), QColor(128, 128, 128)},
        {QObject::tr("Blue (N)"), QColor(30, 100, 220)},
        {QObject::tr("Yellow-green (PE)"), QColor(150, 200, 0)},
        {QObject::tr("Red (+24 V)"), QColor(220, 30, 30)},
        {QObject::tr("Dark blue (0 V)"), QColor(20, 40, 140)},
        {QObject::tr("White"), QColor(235, 235, 235)},
        {QObject::tr("Orange"), QColor(245, 130, 0)},
        {QObject::tr("Violet"), QColor(140, 60, 190)},
        {QObject::tr("Green (bus)"), QColor(0, 150, 70)},
    };
}

QString WireItem::colorName(const QColor &c)
{
    for (const auto &sc : standardColors())
        if (sc.second == c)
            return sc.first;
    return c.name().toUpper();
}

void WireItem::setEnds(const QString &fromUid, int fromPort, const QString &toUid, int toPort)
{
    m_fromUid = fromUid;
    m_fromPort = fromPort;
    m_toUid = toUid;
    m_toPort = toPort;
    rebuild();
}

EquipmentItem *WireItem::fromItem() const
{
    PlanScene *s = planScene();
    QGraphicsItem *it = s ? s->itemByUid(m_fromUid) : nullptr;
    return it && it->type() == ItemType::Equipment ? static_cast<EquipmentItem *>(it) : nullptr;
}

EquipmentItem *WireItem::toItem() const
{
    PlanScene *s = planScene();
    QGraphicsItem *it = s ? s->itemByUid(m_toUid) : nullptr;
    return it && it->type() == ItemType::Equipment ? static_cast<EquipmentItem *>(it) : nullptr;
}

QString WireItem::endText(bool from) const
{
    EquipmentItem *e = from ? fromItem() : toItem();
    if (!e)
        return QStringLiteral("?");
    const QString id = e->data().id.isEmpty() ? e->data().displayName() : e->data().id;
    return id + QStringLiteral(" · ") + e->portName(from ? m_fromPort : m_toPort);
}

// ---------------------------------------------------------------- grips on a selected wire

// Square grip = corner (moves freely, the neighbours keep the right angles),
// round grip = middle of a segment (moves the whole segment sideways).
class WireHandle : public QGraphicsItem
{
public:
    WireHandle(WireItem *wire, int index, bool segment)
        : QGraphicsItem(wire), m_wire(wire), m_index(index), m_segment(segment)
    {
        setFlags(ItemIgnoresTransformations);
        setZValue(1000);
        setAcceptedMouseButtons(Qt::LeftButton);
        setCursor(segment ? Qt::SizeAllCursor : Qt::CrossCursor);
        setToolTip(segment ? QObject::tr("Drag to move this segment") : QObject::tr("Drag to move this corner"));
    }
    int type() const override { return ItemType::WireHandle; }
    QRectF boundingRect() const override { return QRectF(-5, -5, 10, 10); }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        PlanScene *s = ps(this);
        if (s && s->isExporting())
            return;
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(0, 120, 215), 1.2));
        p->setBrush(Qt::white);
        if (m_segment)
            p->drawEllipse(QPointF(0, 0), 3.6, 3.6);
        else
            p->drawRect(QRectF(-4, -4, 8, 8));
    }
    void setIndex(int i, bool segment) { m_index = i; m_segment = segment; update(); }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        m_moved = false;
        m_wire->beginEdit();
        e->accept();
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (!m_moved && (e->screenPos() - e->buttonDownScreenPos(Qt::LeftButton)).manhattanLength() < 3)
            return;
        m_moved = true;
        if (m_segment)
            m_wire->moveSegment(m_index, e->scenePos());
        else
            m_wire->moveCorner(m_index, e->scenePos());
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *) override
    {
        WireItem *w = m_wire;
        const bool moved = m_moved;
        PlanScene *s = ps(w);
        if (!s)
            return;
        // endEdit() may delete this grip: do it after the event
        QTimer::singleShot(0, s, [s, w, moved] {
            if (!s->wires().contains(w))
                return;
            w->endEdit();
            if (moved)
                s->commit(QObject::tr("Edit wire"));
        });
    }

private:
    WireItem *m_wire;
    int m_index;
    bool m_segment;
    bool m_moved = false;
};

QPolygonF WireItem::orthogonalize(const QPolygonF &pts)
{
    QPolygonF out;
    for (int i = 0; i < pts.size(); ++i) {
        if (i > 0) {
            const QPointF p = out.last();
            const QPointF q = pts[i];
            if (std::abs(p.x() - q.x()) > 0.01 && std::abs(p.y() - q.y()) > 0.01) {
                // leave the first terminal vertically; arrive at the last terminal vertically
                if (i == 1)
                    out << QPointF(p.x(), q.y());
                else
                    out << QPointF(q.x(), p.y());
            }
        }
        out << pts[i];
    }
    return out;
}

QPolygonF WireItem::simplify(const QPolygonF &pts)
{
    QPolygonF out;
    for (const QPointF &p : pts) {
        if (!out.isEmpty() && QLineF(out.last(), p).length() < 0.05)
            continue;
        out << p;
        // drop the middle one of three collinear points
        while (out.size() >= 3) {
            const QPointF a = out[out.size() - 3], b = out[out.size() - 2], c = out.last();
            const bool vert = std::abs(a.x() - b.x()) < 0.05 && std::abs(b.x() - c.x()) < 0.05;
            const bool hor = std::abs(a.y() - b.y()) < 0.05 && std::abs(b.y() - c.y()) < 0.05;
            if (!vert && !hor)
                break;
            out.remove(out.size() - 2);
        }
    }
    return out;
}

QPolygonF WireItem::autoRoute(const QPointF &a, bool aUp, const QPointF &b, bool bUp, const QRectF &ra, const QRectF &rb) const
{
    // every wire gets its own lane, so parallel wires do not lie on top of each other
    PlanScene *s = planScene();
    const int lane = s ? std::max(0, int(s->wires().indexOf(const_cast<WireItem *>(this)))) % 8 : 0;
    const double stub = 8 + lane * 3.0;
    const QPointF sa = a + QPointF(0, aUp ? -stub : stub);
    const QPointF eb = b + QPointF(0, bUp ? -stub : stub);
    QPolygonF r;
    r << a;
    if (aUp == bUp) {
        // both terminals on the same side: one horizontal run beyond the outermost one
        const double y = aUp ? std::min(sa.y(), eb.y()) : std::max(sa.y(), eb.y());
        r << QPointF(a.x(), y) << QPointF(b.x(), y);
    } else if ((aUp && eb.y() <= sa.y()) || (!aUp && eb.y() >= sa.y())) {
        // the other terminal is in the direction we leave: a single horizontal run in between
        const double y = (sa.y() + eb.y()) / 2;
        r << QPointF(a.x(), y) << QPointF(b.x(), y);
    } else {
        // go out, sideways between the two, then in
        // vertical run in the gap between the two devices (not through them)
        double x;
        if (rb.left() >= ra.right())
            x = (ra.right() + rb.left()) / 2;
        else if (ra.left() >= rb.right())
            x = (rb.right() + ra.left()) / 2;
        else
            x = std::max(ra.right(), rb.right()) + 6 + lane * 3.0;
        r << sa << QPointF(x, sa.y()) << QPointF(x, eb.y()) << eb;
    }
    r << b;
    return simplify(r);
}

QPolygonF WireItem::routePoints() const
{
    EquipmentItem *a = fromItem();
    EquipmentItem *b = toItem();
    if (!a || !b)
        return {};
    const QPointF pa = a->portScenePos(m_fromPort);
    const QPointF pb = b->portScenePos(m_toPort);
    if (!m_manual)
        return autoRoute(pa, a->portFacesUp(m_fromPort), pb, b->portFacesUp(m_toPort),
                         a->mapToScene(QRectF(QPointF(), a->size())).boundingRect(),
                         b->mapToScene(QRectF(QPointF(), b->size())).boundingRect());
    QPolygonF pts;
    pts << pa;
    for (const QPointF &p : m_bends)
        pts << p;
    pts << pb;
    return m_editing ? orthogonalize(pts) : simplify(orthogonalize(pts));
}

void WireItem::setManualBends(const QVector<QPointF> &bends)
{
    m_bends = bends;
    m_manual = !bends.isEmpty();
    rebuild();
}

void WireItem::resetRoute()
{
    m_bends.clear();
    m_manual = false;
    rebuild();
}

void WireItem::translateBends(const QPointF &d)
{
    for (QPointF &p : m_bends)
        p += d;
    rebuild();
}

void WireItem::beginEdit()
{
    // freeze the current route as manual corners
    const QPolygonF r = routePoints();
    if (r.size() < 2)
        return;
    m_bends.clear();
    for (int i = 1; i < r.size() - 1; ++i)
        m_bends << r[i];
    m_manual = true;
    m_editing = true;
}

static QPointF snapMm(const QPointF &p)
{
    return QPointF(std::round(p.x()), std::round(p.y()));
}

void WireItem::moveCorner(int index, const QPointF &scenePos)
{
    // index in routePoints(): 0 and last are the terminals, m_bends[index - 1] is the corner
    const int bi = index - 1;
    if (bi < 0 || bi >= m_bends.size())
        return;
    const QPolygonF r = routePoints();
    const QPointF old = m_bends[bi];
    const QPointF q = snapMm(scenePos);
    m_bends[bi] = q;
    // neighbours that are corners follow, so that the angles stay right
    for (int nb : {bi - 1, bi + 1}) {
        if (nb < 0 || nb >= m_bends.size())
            continue;
        QPointF &n = m_bends[nb];
        if (std::abs(n.x() - old.x()) < 0.05)
            n.setX(q.x());          // the segment between them was vertical
        else
            n.setY(q.y());          // horizontal
    }
    Q_UNUSED(r);
    rebuild();
}

void WireItem::moveSegment(int index, const QPointF &scenePos)
{
    // segment between routePoints()[index] and [index + 1]; both must be corners
    const int b0 = index - 1, b1 = index;
    if (b0 < 0 || b1 >= m_bends.size())
        return;
    const QPointF q = snapMm(scenePos);
    const bool vertical = std::abs(m_bends[b0].x() - m_bends[b1].x()) < 0.05;
    if (vertical) {
        m_bends[b0].setX(q.x());
        m_bends[b1].setX(q.x());
    } else {
        m_bends[b0].setY(q.y());
        m_bends[b1].setY(q.y());
    }
    rebuild();
}

void WireItem::endEdit()
{
    m_editing = false;
    const QPolygonF r = simplify(routePoints());
    m_bends.clear();
    for (int i = 1; i < r.size() - 1; ++i)
        m_bends << r[i];
    rebuild();
    updateHandles();
}

void WireItem::insertJog(const QPointF &scenePos)
{
    beginEdit();
    m_editing = false;
    const QPolygonF r = routePoints();
    int best = -1;
    double bestD = 1e18;
    QPointF proj;
    for (int i = 0; i + 1 < r.size(); ++i) {
        const QLineF l(r[i], r[i + 1]);
        const QPointF d = l.p2() - l.p1();
        const double len2 = d.x() * d.x() + d.y() * d.y();
        const double t = len2 > 0 ? std::clamp(((scenePos.x() - l.x1()) * d.x() + (scenePos.y() - l.y1()) * d.y()) / len2, 0.0, 1.0) : 0;
        const QPointF q = l.p1() + d * t;
        const double dist = QLineF(q, scenePos).length();
        if (dist < bestD) {
            bestD = dist;
            best = i;
            proj = q;
        }
    }
    if (best < 0)
        return;
    // a small step: two new corners 6 mm apart on that segment, then a 4 mm offset
    const QPointF dir = (r[best + 1] - r[best]) / std::max(1e-6, QLineF(r[best], r[best + 1]).length());
    const QPointF n(-dir.y(), dir.x());
    const QPointF c1 = snapMm(proj - dir * 3), c2 = snapMm(proj + dir * 3);
    QVector<QPointF> pts;
    for (int i = 1; i < r.size() - 1; ++i)
        pts << r[i];
    // insert after route point 'best' => bends index best
    const QVector<QPointF> add = {c1, snapMm(c1 + n * 4), snapMm(c2 + n * 4), c2};
    for (int k = 0; k < add.size(); ++k)
        pts.insert(best + k, add[k]);
    m_bends = pts;
    m_manual = true;
    rebuild();
    updateHandles();
}

void WireItem::updateHandles()
{
    PlanScene *s = planScene();
    const bool want = isSelected() && s && s->tool() == PlanScene::SelectTool && !m_route.isEmpty();
    if (!want) {
        qDeleteAll(m_handles);
        m_handles.clear();
        return;
    }
    // corners 1..n-2, segments whose both ends are corners
    struct H { QPointF pos; int index; bool segment; };
    QVector<H> hs;
    const int n = m_route.size();
    for (int i = 1; i < n - 1; ++i)
        hs.push_back({m_route[i], i, false});
    for (int i = 1; i + 1 < n - 1; ++i)
        hs.push_back({(m_route[i] + m_route[i + 1]) / 2, i, true});
    while (m_handles.size() > hs.size())
        delete m_handles.takeLast();
    for (int i = 0; i < hs.size(); ++i) {
        if (i >= m_handles.size())
            m_handles << new WireHandle(this, hs[i].index, hs[i].segment);
        m_handles[i]->setIndex(hs[i].index, hs[i].segment);
        m_handles[i]->setPos(hs[i].pos);
    }
}

QVariant WireItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedHasChanged || change == ItemSceneHasChanged)
        updateHandles();
    return QGraphicsItem::itemChange(change, value);
}

void WireItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e)
{
    // double-click on a selected wire: add a step that can be dragged
    if (isSelected() && e->button() == Qt::LeftButton) {
        insertJog(e->scenePos());
        if (PlanScene *s = planScene())
            s->commit(QObject::tr("Edit wire"));
        e->accept();
        return;
    }
    QGraphicsItem::mouseDoubleClickEvent(e);
}

void WireItem::rebuild()
{
    prepareGeometryChange();
    m_path = QPainterPath();
    m_route.clear();
    EquipmentItem *a = fromItem();
    EquipmentItem *b = toItem();
    if (a && b && a->isVisible() && b->isVisible()) {
        m_route = routePoints();
        if (m_route.size() >= 2) {
            m_path.moveTo(m_route.first());
            for (int i = 1; i < m_route.size(); ++i)
                m_path.lineTo(m_route[i]);
        }
    }
    m_bounds = m_path.boundingRect().adjusted(-4, -4, 4, 4);
    if (!m_path.isEmpty()) {
        // room for the end labels (shown while highlighted) and the wire label
        const QPointF pa = m_route.first(), pb = m_route.last();
        m_bounds = m_bounds.united(QRectF(pa - QPointF(6, 50), QSizeF(12, 100)));
        m_bounds = m_bounds.united(QRectF(pb - QPointF(6, 50), QSizeF(12, 100)));
        const QPointF mid = labelAnchor();
        m_bounds = m_bounds.united(QRectF(mid - QPointF(40, 8), QSizeF(80, 16)));
    }
    // grips follow the corners
    if (!m_handles.isEmpty()) {
        const int n = m_route.size();
        int k = 0;
        for (int i = 1; i < n - 1 && k < m_handles.size(); ++i, ++k)
            m_handles[k]->setPos(m_route[i]);
        for (int i = 1; i + 1 < n - 1 && k < m_handles.size(); ++i, ++k)
            m_handles[k]->setPos((m_route[i] + m_route[i + 1]) / 2);
    }
    update();
}

QPointF WireItem::labelAnchor() const
{
    // middle of the longest horizontal segment (labels read best there)
    if (m_route.size() < 2)
        return {};
    int best = -1;
    double bestLen = 0;
    for (int i = 0; i + 1 < m_route.size(); ++i) {
        const QLineF l(m_route[i], m_route[i + 1]);
        if (std::abs(l.dy()) < 0.05 && l.length() > bestLen) {
            bestLen = l.length();
            best = i;
        }
    }
    if (best < 0)
        return m_path.pointAtPercent(0.5);
    return (m_route[best] + m_route[best + 1]) / 2;
}

QRectF WireItem::boundingRect() const
{
    return m_bounds;
}

QPainterPath WireItem::shape() const
{
    QPainterPathStroker st;
    const PlanScene *s = planScene();
    const double vs = s ? std::max(1e-6, s->viewScale()) : 1.0;
    st.setWidth(std::max(4.0, 8.0 / vs));
    return st.createStroke(m_path);
}

void WireItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (m_path.isEmpty())
        return;
    PlanScene *s = planScene();
    const bool exporting = s && s->isExporting();
    const LinkState st = exporting ? LinkState::Normal : m_state;
    const bool hot = !exporting && (isSelected() || st == LinkState::Selected || st == LinkState::Linked);
    p->setRenderHint(QPainter::Antialiasing);
    if (st == LinkState::Dim)
        p->setOpacity(dimOpacity());
    if (hot) {
        QColor glow(255, 170, 0, 110);
        p->setPen(QPen(glow, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(m_path);
    }
    const double w = hot ? 2.2 : 1.6;
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(m_color.lightness() > 200 ? QColor(120, 120, 120) : m_color.darker(150), w + 0.8, Qt::SolidLine,
                   Qt::RoundCap, Qt::MiterJoin));
    p->drawPath(m_path);
    p->setPen(QPen(m_color, w, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin));
    p->drawPath(m_path);
    // dot on the terminals
    p->setPen(Qt::NoPen);
    p->setBrush(m_color.darker(130));
    p->drawEllipse(m_route.first(), 1.3, 1.3);
    p->drawEllipse(m_route.last(), 1.3, 1.3);

    // wire label (cross-section / text) in the middle
    QString mid = m_label;
    if (!m_section.isEmpty())
        mid = (mid.isEmpty() ? QString() : mid + QStringLiteral("  ")) + m_section + QStringLiteral(" mm²");
    auto tag = [&](const QPointF &at, const QString &text, const QColor &border) {
        p->setFont(mmFont(3.6, true));
        QFontMetricsF fm(p->font());
        QRectF r = fm.boundingRect(text);
        r.moveCenter(at);
        r.adjust(-1.5, -0.6, 1.5, 0.6);
        p->setPen(QPen(border, 0.5));
        p->setBrush(QColor(255, 255, 255, 235));
        p->drawRoundedRect(r, 1.2, 1.2);
        p->setPen(QColor(20, 20, 20));
        p->drawText(r, Qt::AlignCenter, text);
    };
    if ((hot || !m_label.isEmpty()) && !mid.isEmpty())
        tag(labelAnchor(), mid, m_color);
    if (hot) {
        // where the wire goes, written at each end
        EquipmentItem *a = fromItem();
        EquipmentItem *b = toItem();
        // vertical tags beyond the terminal, so neighbouring terminals do not overlap
        auto endTag = [&](const QPointF &at, bool up, const QString &text) {
            p->save();
            p->translate(at);
            p->rotate(up ? -90 : 90);
            p->setFont(mmFont(3.4, true));
            QFontMetricsF fm(p->font());
            const QRectF r(3.2, -2.4, fm.horizontalAdvance(text) + 2.4, 4.8);
            p->setPen(QPen(QColor(255, 140, 0), 0.5));
            p->setBrush(QColor(255, 255, 255, 240));
            p->drawRoundedRect(r, 1.2, 1.2);
            p->setPen(QColor(20, 20, 20));
            p->drawText(r, Qt::AlignCenter, text);
            p->restore();
        };
        if (a && b) {
            endTag(m_route.first(), a->portFacesUp(m_fromPort), QStringLiteral("→ ") + endText(false));
            endTag(m_route.last(), b->portFacesUp(m_toPort), QStringLiteral("→ ") + endText(true));
        }
    }
}
