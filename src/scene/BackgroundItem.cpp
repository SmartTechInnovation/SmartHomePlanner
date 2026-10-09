#include "BackgroundItem.h"
#include "PlanScene.h"

#include <QPainter>
#include <QPixmap>

BackgroundItem::BackgroundItem()
{
    setZValue(ZOrder::Background);
    setTransformationMode(Qt::SmoothTransformation);
    setLocked(true);
}

bool BackgroundItem::setImageData(const QByteArray &data)
{
    QPixmap pm;
    if (!pm.loadFromData(data))
        return false;
    m_data = data;
    setPixmap(pm);
    return true;
}

void BackgroundItem::setLocked(bool l)
{
    m_locked = l;
    setFlag(ItemIsMovable, !l);
    setFlag(ItemIsSelectable, !l);
    setAcceptedMouseButtons(l ? Qt::NoButton : Qt::LeftButton);
}

void BackgroundItem::paint(QPainter *p, const QStyleOptionGraphicsItem *o, QWidget *w)
{
    QGraphicsPixmapItem::paint(p, o, w);
    auto *ps = qobject_cast<PlanScene *>(scene());
    if (!m_locked && !(ps && ps->isExporting())) {
        p->setPen(QPen(QColor(230, 120, 0), 0, Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawRect(boundingRect());
    }
}
