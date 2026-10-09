#ifndef EFFECTITEM_H
#define EFFECTITEM_H

#include "ItemTypes.h"

#include <QGraphicsItem>
#include <QPolygonF>

class DeviceItem;

// Light cone / speaker coverage of a DeviceItem, clipped by the walls.
// Lives as a separate top-level item so it is drawn below walls and symbols.
class EffectItem : public QGraphicsItem
{
public:
    explicit EffectItem(DeviceItem *device);
    ~EffectItem() override;
    int type() const override { return ItemType::Effect; }

    void invalidate();
    void detachDevice() { m_device = nullptr; }
    DeviceItem *device() const { return m_device; }

    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

    const QPolygonF &visibleArea();     // scene coords
    // Wi-Fi: areas with excellent / good / weak signal (scene coords)
    const QVector<QPolygonF> &wifiZones();

private:
    DeviceItem *m_device;
    QPolygonF m_poly;
    QVector<QPolygonF> m_zones;
    QRectF m_bounds;
    bool m_dirty = true;
};

// Darkens the whole plan in "night" simulation mode
class NightItem : public QGraphicsItem
{
public:
    NightItem();
    int type() const override { return ItemType::Night; }
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
};

#endif // EFFECTITEM_H
