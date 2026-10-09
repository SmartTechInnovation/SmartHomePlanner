#include "RoomItem.h"
#include "PlanScene.h"
#include "core/Geometry.h"

#include <QCursor>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

// Name + area of the room. Can be dragged away from the elements; the offset is saved.
class RoomLabel : public QGraphicsItem
{
public:
    explicit RoomLabel(RoomItem *room) : QGraphicsItem(room), m_room(room)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges);
        setCursor(Qt::OpenHandCursor);
        setToolTip(QObject::tr("Drag to move the room label"));
    }
    QRectF boundingRect() const override { return QRectF(-250, -60, 500, 120); }
    QPainterPath shape() const override
    {
        // only the text itself is grabbable, so clicks elsewhere still select the room
        QFont f;
        f.setPixelSize(TextSize::Room);
        f.setBold(true);
        QFontMetricsF fm(f);
        const QRectF r = fm.boundingRect(boundingRect(), Qt::AlignCenter, m_room->labelText());
        QPainterPath p;
        p.addRect(r.adjusted(-4, -2, 4, 2));
        return p;
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        QFont f = p->font();
        f.setPixelSize(TextSize::Room);
        f.setBold(true);
        p->setFont(f);
        p->setPen(QColor(40, 60, 90));
        p->drawText(boundingRect(), Qt::AlignCenter, m_room->labelText());
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        if (change == ItemPositionHasChanged && !m_updating)
            m_room->m_labelOffset = pos() - Geo::polygonCentroid(m_room->points());
        return QGraphicsItem::itemChange(change, value);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override
    {
        QGraphicsItem::mouseReleaseEvent(e);
        if (auto *ps = qobject_cast<PlanScene *>(scene()))
            ps->commit(QObject::tr("Move room label"));
    }

public:
    RoomItem *m_room;
    bool m_updating = false;
};

RoomItem::RoomItem()
{
    m_closed = true;
    setZValue(ZOrder::Room);
    m_label = new RoomLabel(this);
}

void RoomItem::setLabelOffset(const QPointF &o)
{
    m_labelOffset = o;
    updateLabel();
}

QString RoomItem::labelText() const
{
    // the toolbar switches hide names / areas on screen; the PDF export always shows them
    auto *ps = qobject_cast<PlanScene *>(scene());
    const bool screen = ps && !ps->isExporting();
    const bool names = !screen || ps->showRoomNames();
    const bool areas = !screen || ps->showRoomAreas();
    QString text = names ? m_name : QString();
    if (m_showArea && areas) {
        if (!text.isEmpty())
            text += "\n";
        text += Geo::formatArea(area());
    }
    return text;
}

void RoomItem::pointsChanged()
{
    updateLabel();
}

void RoomItem::updateLabel()
{
    auto *l = static_cast<RoomLabel *>(m_label);
    l->m_updating = true;
    l->setPos(Geo::polygonCentroid(m_pts) + m_labelOffset);
    l->m_updating = false;
    l->update();
    update();
}

double RoomItem::area() const { return Geo::polygonArea(m_pts); }
double RoomItem::perimeter() const { return Geo::polylineLength(m_pts, true); }

QRectF RoomItem::boundingRect() const
{
    return m_pts.boundingRect().adjusted(-3, -3, 3, 3);
}

QPainterPath RoomItem::shape() const
{
    QPainterPath p;
    p.addPolygon(m_pts);
    p.closeSubpath();
    return p;
}

void RoomItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    auto *ps = qobject_cast<PlanScene *>(scene());
    const bool exporting = ps && ps->isExporting();
    p->setRenderHint(QPainter::Antialiasing);
    QColor fill = m_color;
    fill.setAlpha(45);
    QColor border = m_color.darker(130);
    p->setPen(QPen(border, isSelected() && !exporting ? 3 : 1.5, isSelected() && !exporting ? Qt::DashLine : Qt::SolidLine));
    p->setBrush(fill);
    p->drawPolygon(m_pts);

}
