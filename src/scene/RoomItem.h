#ifndef ROOMITEM_H
#define ROOMITEM_H

#include "HandleItem.h"

#include <QColor>
#include <QString>

// A room / surface drawn as a closed polygon. Shows its name and area in m².
class RoomItem : public PolyItem
{
public:
    RoomItem();
    int type() const override { return ItemType::Room; }

    QString name() const { return m_name; }
    void    setName(const QString &n) { m_name = n; updateLabel(); }
    QColor  color() const { return m_color; }
    void    setColor(const QColor &c) { m_color = c; update(); }
    bool    showArea() const { return m_showArea; }
    void    setShowArea(bool s) { m_showArea = s; updateLabel(); }
    QPointF labelOffset() const { return m_labelOffset; }
    void    setLabelOffset(const QPointF &o);
    QString labelText() const;
    void    refreshLabel() { updateLabel(); }

    double area() const;        // cm²
    double perimeter() const;   // cm

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

protected:
    void pointsChanged() override;

private:
    friend class RoomLabel;
    void updateLabel();
    QGraphicsItem *m_label = nullptr;
    QString m_name;
    QColor  m_color = QColor(120, 170, 220);
    bool    m_showArea = true;
    QPointF m_labelOffset;
};

#endif // ROOMITEM_H
