#ifndef STRIPITEM_H
#define STRIPITEM_H

#include "DeviceItem.h"
#include "HandleItem.h"

#include <QPainterPath>

class NoteItem;

// LED strip drawn as a polyline or as a circle. Glows when switched on.
//   data().rgb    -> strip color
//   data().radius -> glow width [cm]
class StripItem : public PolyItem
{
public:
    enum Mode { Line, Circle };

    explicit StripItem(const DeviceData &d, Mode mode = Line);
    int type() const override { return ItemType::Strip; }

    const DeviceData &data() const { return m_d; }
    DeviceData &dataRef() { return m_d; }
    void refresh();

    Mode mode() const { return m_mode; }
    double circleRadius() const;
    void setCircleRadius(double r);
    double length() const;          // cm
    void setOn(bool on) { m_d.on = on; update(); }
    QColor color() const { return m_d.rgb.isValid() ? m_d.rgb : QColor(255, 220, 150); }

    NoteItem *noteItem() const { return m_note; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

protected:
    void pointsChanged() override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QPainterPath linePath() const;
    DeviceData m_d;
    Mode m_mode;
    NoteItem *m_note = nullptr;
};

#endif // STRIPITEM_H
