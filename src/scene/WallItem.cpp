#include "WallItem.h"
#include "PlanScene.h"
#include "core/Geometry.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

WallItem::WallItem()
{
    setZValue(ZOrder::Wall);
}

void WallItem::setThickness(double t)
{
    prepareGeometryChange();
    m_thickness = qMax(1.0, t);
    pointsChanged();
    update();
}

QPainterPath WallItem::bodyPath() const
{
    QPainterPath line;
    if (m_pts.isEmpty())
        return line;
    line.moveTo(m_pts.first());
    for (int i = 1; i < m_pts.size(); ++i)
        line.lineTo(m_pts[i]);
    if (m_closed && m_pts.size() > 2)
        line.closeSubpath();
    QPainterPathStroker st;
    st.setWidth(m_thickness);
    st.setJoinStyle(Qt::MiterJoin);
    st.setMiterLimit(4);
    st.setCapStyle(m_closed ? Qt::FlatCap : Qt::SquareCap);
    return st.createStroke(line).simplified();
}

void WallItem::pointsChanged()
{
    m_body = bodyPath();
    if (auto *ps = qobject_cast<PlanScene *>(scene()))
        ps->notifyWallsChanged();
}

QVector<QLineF> WallItem::obstacleEdges() const
{
    QVector<QLineF> edges;
    const QPolygonF sp = scenePoints();
    const int n = sp.size();
    const int segCount = (m_closed && n > 2) ? n : n - 1;
    const double h = m_thickness / 2.0;
    for (int i = 0; i < segCount; ++i) {
        const QPointF a = sp[i];
        const QPointF b = sp[(i + 1) % n];
        QLineF l(a, b);
        if (l.length() < 1e-6)
            continue;
        const QPointF dir = (b - a) / l.length();
        const QPointF nrm(-dir.y() * h, dir.x() * h);
        // extend a little along the wall so corners have no gaps
        const QPointF a2 = a - dir * h;
        const QPointF b2 = b + dir * h;
        edges << QLineF(a2 + nrm, b2 + nrm) << QLineF(a2 - nrm, b2 - nrm)
              << QLineF(a2 + nrm, a2 - nrm) << QLineF(b2 + nrm, b2 - nrm);
    }
    return edges;
}

double WallItem::length() const
{
    return Geo::polylineLength(m_pts, m_closed);
}

QRectF WallItem::boundingRect() const
{
    const double m = m_thickness + 60;     // room for dimension labels
    return m_pts.boundingRect().adjusted(-m, -m, m, m);
}

QPainterPath WallItem::shape() const
{
    return m_body;
}

void WallItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    auto *ps = qobject_cast<PlanScene *>(scene());
    const bool exporting = ps && ps->isExporting();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(QColor(30, 30, 30), 0));
    p->setBrush(QColor(70, 70, 70));
    p->drawPath(m_body);

    if (isSelected() && !exporting) {
        p->setPen(QPen(QColor(0, 120, 215), 0, Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawPath(m_body);

        // segment lengths
        QFont f = p->font();
        f.setPixelSize(TextSize::Dim);
        p->setFont(f);
        p->setPen(QColor(0, 90, 180));
        const int n = m_pts.size();
        const int segCount = (m_closed && n > 2) ? n : n - 1;
        for (int i = 0; i < segCount; ++i) {
            QLineF l(m_pts[i], m_pts[(i + 1) % n]);
            if (l.length() < 1)
                continue;
            const QPointF mid = (l.p1() + l.p2()) / 2;
            QLineF nrm = l.normalVector();
            nrm.setLength(m_thickness / 2 + 14);
            const QPointF at = mid + (nrm.p2() - nrm.p1());
            p->save();
            p->translate(at);
            double ang = -l.angle();
            if (ang < -90) ang += 180;
            if (ang > 90) ang -= 180;
            p->rotate(ang);
            const QString txt = Geo::formatMeters(l.length());
            const QRectF r(-60, -10, 120, 20);
            p->drawText(r, Qt::AlignCenter, txt);
            p->restore();
        }
    }
}
