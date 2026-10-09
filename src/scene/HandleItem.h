#ifndef HANDLEITEM_H
#define HANDLEITEM_H

#include "ItemTypes.h"

#include <QGraphicsItem>
#include <QPolygonF>

class PolyItem;

// Small square grip to drag one vertex of a PolyItem. Constant size on screen.
class HandleItem : public QGraphicsItem
{
public:
    HandleItem(PolyItem *owner, int index);
    int type() const override { return ItemType::Handle; }
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
    int index() const { return m_index; }

    PolyItem *owner() const { return m_owner; }
    // moves the corner to 'scenePos' (snapped to walls unless 'snap' is false)
    void dragTo(const QPointF &scenePos, bool snap = true);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override;   // removes the corner

private:
    PolyItem *m_owner;
    int m_index;
    bool m_updating = false;
    bool m_dragging = false;
    bool m_moved = false;
    QPointF m_grabOffset;
    friend class PolyItem;
};

// Base for items described by a list of points (walls, rooms, LED strips)
class PolyItem : public QGraphicsItem
{
public:
    explicit PolyItem(QGraphicsItem *parent = nullptr);
    ~PolyItem() override;

    const QPolygonF &points() const { return m_pts; }     // item coordinates
    QPolygonF scenePoints() const { return mapToScene(m_pts); }
    void setPoints(const QPolygonF &pts);
    void setPoint(int i, const QPointF &p);
    bool isClosed() const { return m_closed; }
    void setClosed(bool c) { m_closed = c; pointsChanged(); update(); }

    // Moves pos() to the first point, keeping the shape where it is (cleaner save files)
    void normalizeOrigin();
    void updateHandles();

    // ---- editing the outline
    int  minPoints() const { return m_closed ? 3 : 2; }
    bool canRemovePoint() const { return m_pts.size() > minPoints(); }
    // index of the edge (i -> i+1) nearest to 'scenePos' within 'tolerance' (scene units), or -1
    int  edgeAt(const QPointF &scenePos, double tolerance, QPointF *projected = nullptr) const;
    // inserts a corner on the nearest edge; returns false if no edge is close enough
    bool insertPointAt(const QPointF &scenePos, double tolerance);
    bool removePoint(int index);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override;   // adds a corner on an edge
    virtual void pointsChanged() {}
    virtual double extraEdgeTolerance() const { return 0; }     // e.g. half the wall thickness
    virtual bool showHandles() const { return isSelected(); }

    QPolygonF m_pts;
    bool m_closed = false;

private:
    QList<HandleItem *> m_handles;
};

#endif // HANDLEITEM_H
