#include "StripItem.h"
#include "NoteItem.h"
#include "PlanScene.h"
#include "core/Geometry.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QtMath>

StripItem::StripItem(const DeviceData &d, Mode mode) : m_d(d), m_mode(mode)
{
    m_d.kind = DeviceKind::Strip;
    setZValue(ZOrder::Strip);
    m_note = new NoteItem(this);
    m_note->setPos(8, 8);
    refresh();
}

void StripItem::refresh()
{
    prepareGeometryChange();
    m_note->setText(m_d.note);
    const SectionDef *s = Library::instance().section(m_d.section);
    m_note->setAccent(s ? s->color : QColor(Qt::gray));
    auto *ps = qobject_cast<PlanScene *>(scene());
    m_note->setVisible(m_d.showNote && !m_d.note.isEmpty() && (!ps || ps->showNotes()));
    setVisible(!ps || ps->isSectionVisible(m_d.section));
    update();
}

double StripItem::circleRadius() const
{
    if (m_mode != Circle || m_pts.isEmpty())
        return 0;
    return QLineF(QPointF(0, 0), m_pts.first()).length();
}

void StripItem::setCircleRadius(double r)
{
    if (m_mode != Circle)
        return;
    QPolygonF p;
    p << QPointF(qMax(1.0, r), 0);
    setPoints(p);
}

double StripItem::length() const
{
    if (m_mode == Circle)
        return 2 * M_PI * circleRadius();
    return Geo::polylineLength(m_pts, false);
}

QPainterPath StripItem::linePath() const
{
    QPainterPath path;
    if (m_mode == Circle) {
        const double r = circleRadius();
        path.addEllipse(QPointF(0, 0), r, r);
        return path;
    }
    if (m_pts.isEmpty())
        return path;
    path.moveTo(m_pts.first());
    for (int i = 1; i < m_pts.size(); ++i)
        path.lineTo(m_pts[i]);
    return path;
}

void StripItem::pointsChanged()
{
    prepareGeometryChange();
}

QRectF StripItem::boundingRect() const
{
    const double m = m_d.radius + 20;
    QRectF r = linePath().boundingRect().adjusted(-m, -m, m, m);
    r |= QRectF(-10, -TextSize::Id - 10, 80, TextSize::Id + 12);
    return r;
}

QPainterPath StripItem::shape() const
{
    QPainterPathStroker st;
    st.setWidth(14);
    return st.createStroke(linePath());
}

void StripItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    auto *ps = qobject_cast<PlanScene *>(scene());
    const bool exporting = ps && ps->isExporting();
    const QPainterPath path = linePath();
    const QColor col = color();
    p->setRenderHint(QPainter::Antialiasing);

    if (m_d.on && (!exporting || (ps && ps->exportEffects()))) {
        const bool night = ps && ps->nightMode() && !exporting;
        if (night)
            p->setCompositionMode(QPainter::CompositionMode_Plus);
        const int steps = 7;
        for (int i = steps; i >= 1; --i) {
            QColor g = col;
            g.setAlpha(int((night ? 70 : 45) * (1.0 - double(i) / (steps + 1))) + 6);
            QPen pen(g, 2.0 * m_d.radius * i / steps, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p->strokePath(path, pen);
        }
        p->setCompositionMode(QPainter::CompositionMode_SourceOver);
        p->strokePath(path, QPen(col.lighter(130), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->strokePath(path, QPen(QColor(255, 255, 255, 220), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    } else {
        p->strokePath(path, QPen(col.darker(170), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPen dots(col, 3, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin);
        p->strokePath(path, dots);
    }

    // ID label
    if (!m_d.id.isEmpty()) {
        QFont f = p->font();
        f.setPixelSize(TextSize::Id);
        f.setBold(true);
        p->setFont(f);
        const SectionDef *sec = Library::instance().section(m_d.section);
        p->setPen(sec ? sec->color.darker(120) : QColor(Qt::black));
        const QPointF at = m_mode == Circle ? QPointF(-circleRadius(), -circleRadius())
                                            : (m_pts.isEmpty() ? QPointF() : m_pts.first());
        p->drawText(QRectF(at.x() - 4, at.y() - TextSize::Id - 8, 80, TextSize::Id + 4),
                    Qt::AlignLeft | Qt::AlignBottom, m_d.id);
    }

    if (isSelected() && !exporting) {
        p->setPen(QPen(QColor(0, 120, 215), 0, Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawPath(shape());
        QFont f = p->font();
        f.setPixelSize(TextSize::Dim);
        f.setBold(false);
        p->setFont(f);
        p->setPen(QColor(0, 90, 180));
        const QRectF b = path.boundingRect();
        p->drawText(QRectF(b.center().x() - 80, b.bottom() + 6, 160, 18), Qt::AlignCenter,
                    Geo::formatMeters(length()));
    }
}

void StripItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        setOn(!m_d.on);
        if (auto *ps = qobject_cast<PlanScene *>(scene()))
            ps->notifyItemEdited(this);
        e->accept();
        return;
    }
    PolyItem::mouseDoubleClickEvent(e);
}

QVariant StripItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSceneHasChanged && scene())
        refresh();
    if (change == ItemPositionHasChanged || change == ItemVisibleHasChanged)
        if (auto *ps = qobject_cast<PlanScene *>(scene()))
            ps->scheduleLinkUpdate();
    return PolyItem::itemChange(change, value);
}
