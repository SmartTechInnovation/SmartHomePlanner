#include "NoteItem.h"
#include "PlanScene.h"

#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QPainter>

static QFont noteFont()
{
    QFont f;
    f.setPixelSize(TextSize::Note);
    return f;
}

NoteItem::NoteItem(QGraphicsItem *parent) : QGraphicsItem(parent)
{
    setFlags(ItemIsMovable);
    setCursor(Qt::OpenHandCursor);
    setZValue(5);
}

void NoteItem::setText(const QString &t)
{
    prepareGeometryChange();
    m_text = t;
    update();
}

QRectF NoteItem::textRect() const
{
    QFontMetricsF fm(noteFont());
    const QRectF r = fm.boundingRect(QRectF(0, 0, 2000, 2000), Qt::AlignLeft | Qt::AlignTop, m_text);
    return QRectF(0, 0, r.width() + 8, r.height() + 4);
}

QRectF NoteItem::boundingRect() const
{
    if (m_text.isEmpty())
        return {};
    return textRect().adjusted(-1, -1, 1, 1);
}

void NoteItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (m_text.isEmpty())
        return;
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = textRect();
    p->setPen(QPen(m_accent, 1.2));
    p->setBrush(QColor(255, 255, 255, 230));
    p->drawRoundedRect(r, 3, 3);
    p->setFont(noteFont());
    p->setPen(QColor(25, 25, 25));
    p->drawText(r, Qt::AlignCenter, m_text);
}

void NoteItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *e)
{
    QGraphicsItem::mouseReleaseEvent(e);
    if (auto *ps = qobject_cast<PlanScene *>(scene()))
        ps->commit(QObject::tr("Move note"));
}
