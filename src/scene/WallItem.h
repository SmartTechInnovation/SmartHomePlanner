#ifndef WALLITEM_H
#define WALLITEM_H

#include "HandleItem.h"

#include <QLineF>
#include <QPainterPath>
#include <QVector>

// A wall drawn as a polyline with a thickness. Blocks light and sound.
class WallItem : public PolyItem
{
public:
    WallItem();
    int type() const override { return ItemType::Wall; }

    double thickness() const { return m_thickness; }
    void   setThickness(double t);

    // Edges of the wall body in scene coordinates (used as light/sound obstacles)
    QVector<QLineF> obstacleEdges() const;
    double length() const;

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

protected:
    void pointsChanged() override;
    double extraEdgeTolerance() const override { return m_thickness / 2; }

private:
    QPainterPath bodyPath() const;
    double m_thickness = 20;
    QPainterPath m_body;
};

#endif // WALLITEM_H
