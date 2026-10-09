#include "EffectItem.h"
#include "DeviceItem.h"
#include "PlanScene.h"
#include "core/Geometry.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>

EffectItem::EffectItem(DeviceItem *device) : m_device(device)
{
    setZValue(ZOrder::Effect);
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    setFlag(ItemIsSelectable, false);
    setFlag(ItemIsMovable, false);
}

EffectItem::~EffectItem()
{
    if (m_device)
        m_device->detachEffect();
}

void EffectItem::invalidate()
{
    prepareGeometryChange();
    m_dirty = true;
    if (m_device) {
        const double r = m_device->effectRadius();
        const QPointF c = m_device->scenePos();
        m_bounds = QRectF(c.x() - r - 2, c.y() - r - 2, 2 * r + 4, 2 * r + 4);
    } else {
        m_bounds = QRectF();
    }
    update();
}

QRectF EffectItem::boundingRect() const
{
    return m_bounds;
}

const QPolygonF &EffectItem::visibleArea()
{
    if (m_dirty && m_device) {
        auto *ps = qobject_cast<PlanScene *>(scene());
        const QVector<QLineF> obstacles = ps ? ps->wallObstacles() : QVector<QLineF>();
        m_poly = Geo::visibilityPolygon(m_device->scenePos(), m_device->effectRadius(), obstacles);
        m_dirty = false;
    }
    return m_poly;
}

const QVector<QPolygonF> &EffectItem::wifiZones()
{
    if (m_dirty && m_device) {
        auto *ps = qobject_cast<PlanScene *>(scene());
        const QVector<QLineF> obstacles = ps ? ps->wallObstacles() : QVector<QLineF>();
        const double r = m_device->effectRadius();
        // a wall body has two faces: half of the loss on each
        const double loss = r * std::clamp(m_device->data().wallLoss, 0.0, 100.0) / 100.0 / 2.0;
        m_zones.clear();
        for (double level : {1.0, 0.7, 0.4})          // weak, good, excellent
            m_zones << Geo::attenuatedPolygon(m_device->scenePos(), r * level, loss * level, obstacles);
        m_dirty = false;
    }
    return m_zones;
}

void EffectItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (!m_device || !m_device->isVisible())
        return;
    auto *ps = qobject_cast<PlanScene *>(scene());
    if (!ps)
        return;
    const bool exporting = ps->isExporting();
    const bool on = m_device->data().on;
    const bool selected = m_device->isSelected() && !exporting;
    const bool showOutline = selected || ps->showBeams();
    if (m_device->isWifi()) {
        // Wi-Fi coverage: only while the access point is selected / dragged (to place it);
        // in the PDF only when "show in PDF" is ticked (data().on)
        const bool show = exporting ? (on && ps->exportEffects()) : selected;
        if (!show)
            return;
    } else if (!on && !showOutline) {
        return;
    }
    if (exporting && !ps->exportEffects())
        return;

    const double r = m_device->effectRadius();
    if (r <= 0.5)
        return;
    const QPointF c = m_device->scenePos();
    if (m_device->isWifi()) {
        // coverage zones: walls weaken the signal but do not stop it
        const QVector<QPolygonF> &zones = wifiZones();
        const QColor cols[] = {QColor(255, 140, 0), QColor(240, 210, 0), QColor(30, 190, 80)};
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < zones.size(); ++i) {
            {
                QColor f = cols[i];
                f.setAlpha(exporting ? 40 : 55);
                p->setPen(Qt::NoPen);
                p->setBrush(f);
                p->drawPolygon(zones[i]);
            }
            {
                QColor o = cols[i].darker(130);
                o.setAlpha(170);
                p->setPen(QPen(o, 0, i == 0 ? Qt::DashLine : Qt::DotLine));
                p->setBrush(Qt::NoBrush);
                p->drawPolygon(zones[i]);
            }
        }
        p->restore();
        if (selected) {
            QFont f = p->font();
            f.setPixelSize(TextSize::Dim);
            p->setFont(f);
            p->setPen(QColor(60, 60, 60));
            p->drawText(QRectF(c.x() - 120, c.y() + 22, 240, 18), Qt::AlignCenter,
                        QStringLiteral("Wi-Fi R ") + Geo::formatMeters(r, 1)
                            + QStringLiteral(" · -%1% / ").arg(int(m_device->data().wallLoss)) + QObject::tr("wall"));
        }
        return;
    }
    QPainterPath clip;
    clip.addPolygon(visibleArea());
    clip.closeSubpath();

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setClipPath(clip);

    if (m_device->isLight()) {
        QColor lc = m_device->lightColor();
        if (on && exporting) {
            // PDF: gradients with transparency are not reliable -> flat translucent fill
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(lc.red(), lc.green(), lc.blue(), 70));
            p->drawEllipse(c, r, r);
            p->setBrush(QColor(lc.red(), lc.green(), lc.blue(), 60));
            p->drawEllipse(c, r * 0.55, r * 0.55);
        } else if (on) {
            const bool night = ps->nightMode() && !exporting;
            if (night)
                p->setCompositionMode(QPainter::CompositionMode_Plus);
            QRadialGradient g(c, r);
            QColor c0 = lc, c1 = lc, c2 = lc;
            c0.setAlpha(night ? 210 : 140);
            c1.setAlpha(night ? 120 : 70);
            c2.setAlpha(0);
            g.setColorAt(0.0, c0);
            g.setColorAt(0.55, c1);
            g.setColorAt(1.0, c2);
            p->setPen(Qt::NoPen);
            p->setBrush(g);
            p->drawEllipse(c, r, r);
            p->setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        if (showOutline) {
            QColor oc = lc.darker(160);
            oc.setAlpha(200);
            p->setPen(QPen(oc, 0, Qt::DashLine));
            p->setBrush(on ? Qt::NoBrush : QBrush(QColor(lc.red(), lc.green(), lc.blue(), 35)));
            p->drawEllipse(c, r, r);
        }
    } else if (m_device->isSpeaker()) {
        const QColor sc(80, 120, 255);
        if (on) {
            QRadialGradient g(c, r);
            if (exporting) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(sc.red(), sc.green(), sc.blue(), 30));
                p->drawEllipse(c, r, r);
            } else {
                g.setColorAt(0.0, QColor(sc.red(), sc.green(), sc.blue(), 110));
                g.setColorAt(1.0, QColor(sc.red(), sc.green(), sc.blue(), 10));
                p->setPen(Qt::NoPen);
                p->setBrush(g);
                p->drawEllipse(c, r, r);
            }
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(QColor(sc.red(), sc.green(), sc.blue(), 120), 0));
            for (int i = 1; i <= 4; ++i)
                p->drawEllipse(c, r * i / 4.0, r * i / 4.0);
        }
        if (showOutline) {
            p->setPen(QPen(sc.darker(130), 0, Qt::DashLine));
            p->setBrush(on ? Qt::NoBrush : QBrush(QColor(sc.red(), sc.green(), sc.blue(), 25)));
            p->drawEllipse(c, r, r);
        }
    }
    p->restore();

    if (selected) {
        // radius label
        QFont f = p->font();
        f.setPixelSize(TextSize::Dim);
        p->setFont(f);
        p->setPen(QColor(60, 60, 60));
        const QString txt = (m_device->isLight() ? QStringLiteral("Ø ") + Geo::formatMeters(2 * r, 2)
                                                 : QStringLiteral("R ") + Geo::formatMeters(r, 2));
        p->drawText(QRectF(c.x() - 80, c.y() + r - 22, 160, 18), Qt::AlignCenter, txt);
    }
}

// ---------------------------------------------------------------- NightItem

NightItem::NightItem()
{
    setZValue(ZOrder::Night);
    setAcceptedMouseButtons(Qt::NoButton);
}

QRectF NightItem::boundingRect() const
{
    return scene() ? scene()->sceneRect() : QRectF();
}

void NightItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    p->fillRect(boundingRect(), QColor(8, 12, 28, 200));
}
