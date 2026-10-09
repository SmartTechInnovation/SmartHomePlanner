#include "GroupItem.h"

#include "DeviceItem.h"
#include "PlanScene.h"
#include "StripItem.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <cmath>

namespace {
constexpr double kPen = 3.0;            // cm on the plan
constexpr int kLabelPx = 15;            // cm

QFont labelFont()
{
    QFont f;
    f.setPixelSize(kLabelPx);
    f.setBold(true);
    return f;
}
} // namespace

GroupItem::GroupItem()
{
    setFlags(ItemIsSelectable);
    setZValue(ZOrder::Group);
}

PlanScene *GroupItem::planScene() const
{
    return qobject_cast<PlanScene *>(scene());
}

QColor GroupItem::paletteColor(int index)
{
    static const QColor colors[] = {
        QColor(220, 40, 40), QColor(30, 110, 230), QColor(20, 150, 60), QColor(240, 130, 0),
        QColor(150, 50, 200), QColor(0, 150, 160), QColor(220, 0, 140), QColor(120, 80, 40),
        QColor(90, 90, 90), QColor(170, 170, 0)};
    const int n = int(sizeof(colors) / sizeof(colors[0]));
    return colors[((index % n) + n) % n];
}

bool GroupItem::hasLink(const QString &a, const QString &b) const
{
    for (const Link &l : m_links)
        if ((l.first == a && l.second == b) || (l.first == b && l.second == a))
            return true;
    return false;
}

bool GroupItem::addLink(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty() || a == b || hasLink(a, b))
        return false;
    m_links.push_back({a, b});
    rebuild();
    return true;
}

void GroupItem::removeLink(int index)
{
    if (index < 0 || index >= m_links.size())
        return;
    m_links.remove(index);
    rebuild();
}

int GroupItem::linkAt(const QPointF &scenePos) const
{
    QPainterPathStroker st;
    const PlanScene *ps = planScene();
    const double vs = ps ? std::max(1e-6, ps->viewScale()) : 1.0;
    st.setWidth(std::max(12.0, 14.0 / vs));
    for (int i = 0; i < m_linkPaths.size(); ++i)
        if (st.createStroke(m_linkPaths[i]).contains(mapFromScene(scenePos)))
            return m_linkIndex[i];
    return -1;
}

QStringList GroupItem::members() const
{
    QStringList out;
    for (const Link &l : m_links) {
        if (!out.contains(l.first))
            out << l.first;
        if (!out.contains(l.second))
            out << l.second;
    }
    for (const QString &u : m_chain)
        if (!out.contains(u))
            out << u;
    return out;
}

bool GroupItem::contains(const QString &uid) const
{
    for (const Link &l : m_links)
        if (l.first == uid || l.second == uid)
            return true;
    return m_chain.contains(uid);
}

void GroupItem::addMember(const QString &uid)
{
    if (uid.isEmpty() || contains(uid))
        return;
    PlanScene *ps = planScene();
    QGraphicsItem *it = ps ? ps->itemByUid(uid) : nullptr;
    QString nearest;
    double best = 1e18;
    if (it)
        for (QGraphicsItem *m : memberItems()) {
            const double d = QLineF(anchorOf(m), anchorOf(it)).length();
            if (d < best) {
                best = d;
                nearest = PlanScene::uidOf(m);
            }
        }
    if (!nearest.isEmpty())
        m_links.push_back({nearest, uid});
    rebuild();
}

void GroupItem::removeMember(const QString &uid)
{
    for (int i = m_links.size() - 1; i >= 0; --i)
        if (m_links[i].first == uid || m_links[i].second == uid)
            m_links.remove(i);
    m_chain.removeAll(uid);
    rebuild();
}

QList<QGraphicsItem *> GroupItem::memberItems() const
{
    QList<QGraphicsItem *> out;
    PlanScene *ps = planScene();
    if (!ps)
        return out;
    for (const QString &uid : members())
        if (QGraphicsItem *it = ps->itemByUid(uid))
            out << it;
    return out;
}

QPointF GroupItem::anchorOf(const QGraphicsItem *it)
{
    if (it->type() == ItemType::Strip) {
        // middle of the strip
        auto *s = static_cast<const StripItem *>(it);
        const QPolygonF pts = s->scenePoints();
        if (s->mode() == StripItem::Circle || pts.size() < 2)
            return s->sceneBoundingRect().center();
        double total = 0;
        for (int i = 1; i < pts.size(); ++i)
            total += QLineF(pts[i - 1], pts[i]).length();
        double half = total / 2;
        for (int i = 1; i < pts.size(); ++i) {
            const QLineF l(pts[i - 1], pts[i]);
            if (half <= l.length())
                return l.pointAt(l.length() > 0 ? half / l.length() : 0);
            half -= l.length();
        }
        return pts.last();
    }
    return it->scenePos();
}

double GroupItem::radiusOf(const QGraphicsItem *it)
{
    if (it->type() == ItemType::Device)
        return static_cast<const DeviceItem *>(it)->symbolSize() / 2 + 3;
    return 6;
}

void GroupItem::rebuild()
{
    prepareGeometryChange();
    m_path = QPainterPath();
    m_linkPaths.clear();
    m_linkIndex.clear();
    PlanScene *ps = planScene();

    // chain given as a list (Ctrl+G, older files): link nearest neighbours one after the other
    if (!m_chain.isEmpty() && ps) {
        QList<QGraphicsItem *> left;
        for (const QString &uid : std::as_const(m_chain))
            if (QGraphicsItem *it = ps->itemByUid(uid))
                left << it;
        QList<QGraphicsItem *> chain;
        if (!left.isEmpty())
            chain << left.takeFirst();
        while (!left.isEmpty()) {
            const QPointF last = anchorOf(chain.last());
            int best = 0;
            double bestD = 1e18;
            for (int i = 0; i < left.size(); ++i) {
                const double d = QLineF(last, anchorOf(left[i])).length();
                if (d < bestD) {
                    bestD = d;
                    best = i;
                }
            }
            chain << left.takeAt(best);
        }
        for (int i = 1; i < chain.size(); ++i) {
            const Link l{PlanScene::uidOf(chain[i - 1]), PlanScene::uidOf(chain[i])};
            if (!hasLink(l.first, l.second))
                m_links.push_back(l);
        }
        m_chain.clear();
    }

    bool labelSet = false;
    for (int li = 0; li < m_links.size(); ++li) {
        QGraphicsItem *ia = ps ? ps->itemByUid(m_links[li].first) : nullptr;
        QGraphicsItem *ib = ps ? ps->itemByUid(m_links[li].second) : nullptr;
        // both ends visible (chapter filter)
        if (!ia || !ib || !ia->isVisible() || !ib->isVisible())
            continue;
        QPointF a = anchorOf(ia);
        QPointF b = anchorOf(ib);
        QLineF l(a, b);
        const double len = l.length();
        if (len < 1)
            continue;
        const double ra = radiusOf(ia);
        const double rb = radiusOf(ib);
        if (len > ra + rb + 4) {
            a = l.pointAt(ra / len);
            b = l.pointAt(1 - rb / len);
        }
        QPainterPath seg(a);
        const QPointF mid = (a + b) / 2;
        const QPointF dir = (b - a) / std::max(1e-6, QLineF(a, b).length());
        const QPointF normal(dir.y(), -dir.x());
        if (m_style == Curved) {
            // bulge to the left of the direction of travel, like a hand-drawn circuit arc
            seg.quadTo(mid + normal * (QLineF(a, b).length() * 0.28), b);
            if (!labelSet)
                m_labelPos = mid + normal * (QLineF(a, b).length() * 0.14 + kLabelPx * 0.9);
        } else {
            seg.lineTo(b);
            if (!labelSet)
                m_labelPos = mid + normal * (kLabelPx * 0.9);
        }
        labelSet = true;
        m_path.addPath(seg);
        m_linkPaths << seg;
        m_linkIndex << li;
    }
    m_bounds = m_path.boundingRect().adjusted(-6, -6, 6, 6);
    if (labelSet && !m_name.isEmpty()) {
        QFontMetricsF fm(labelFont());
        QRectF lr = fm.boundingRect(m_name);
        lr.moveCenter(m_labelPos);
        m_bounds = m_bounds.united(lr.adjusted(-4, -4, 4, 4));
    }
    update();
}

QRectF GroupItem::boundingRect() const
{
    return m_bounds;
}

QPainterPath GroupItem::shape() const
{
    QPainterPathStroker st;
    const PlanScene *ps = planScene();
    const double vs = ps ? std::max(1e-6, ps->viewScale()) : 1.0;
    st.setWidth(std::max(10.0, 10.0 / vs));
    return st.createStroke(m_path);
}

void GroupItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (m_path.isEmpty())
        return;
    const PlanScene *ps = planScene();
    const bool exporting = ps && ps->isExporting();
    bool hot = isSelected() && !exporting;
    if (!exporting && !hot)
        for (QGraphicsItem *it : memberItems())
            if (it->isSelected()) {
                hot = true;
                break;
            }
    p->setRenderHint(QPainter::Antialiasing);
    if (hot) {
        QColor glow = m_color;
        glow.setAlpha(60);
        p->setPen(QPen(glow, kPen * 4, Qt::SolidLine, Qt::RoundCap));
        p->drawPath(m_path);
    }
    QPen pen(m_color, hot ? kPen * 1.5 : kPen, Qt::CustomDashLine, Qt::RoundCap);
    pen.setDashPattern({4, 2.5});
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawPath(m_path);

    if (!m_name.isEmpty()) {
        p->setFont(labelFont());
        QFontMetricsF fm(p->font());
        QRectF lr = fm.boundingRect(m_name);
        lr.moveCenter(m_labelPos);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 220));
        p->drawRoundedRect(lr.adjusted(-3, -1, 3, 1), 3, 3);
        p->setPen(m_color.darker(115));
        p->drawText(lr, Qt::AlignCenter, m_name);
    }
}

QVariant GroupItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSceneHasChanged && scene())
        rebuild();
    return QGraphicsItem::itemChange(change, value);
}
