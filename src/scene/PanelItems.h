#ifndef PANELITEMS_H
#define PANELITEMS_H

#include "DeviceItem.h"
#include "ItemTypes.h"

#include <QGraphicsItem>
#include <QPainterPath>
#include <QPointer>

class PlanScene;
class WireHandle;

// Electrical panel pages work in millimetres: 1 scene unit = 1 mm.

// Highlight state while an element of the panel is selected
enum class LinkState { Normal, Selected, Linked, Dim };

// ---------------------------------------------------------------- DIN rail
class RailItem : public QGraphicsItem
{
public:
    static constexpr double kHeight = 35.0;     // TS35 rail

    explicit RailItem(double length = 450);
    int type() const override { return ItemType::Rail; }

    double length() const { return m_length; }
    void setLength(double l);
    QString label() const { return m_label; }
    void setLabel(const QString &l) { m_label = l; update(); }
    double centerY() const { return scenePos().y() + kHeight / 2; }

    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *e) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;

private:
    bool nearEnd(const QPointF &local) const;
    double m_length;
    QString m_label;
    bool m_resizing = false;
    QList<QGraphicsItem *> m_riders;    // equipment carried along when the rail moves
};

// ---------------------------------------------------------------- equipment (MCB, RCD, relay ...)
class EquipmentItem : public QGraphicsItem
{
public:
    EquipmentItem(const DeviceData &d, const QSizeF &sizeMm, const QVector<QPointF> &ports, const QStringList &portNames);
    int type() const override { return ItemType::Equipment; }

    const DeviceData &data() const { return m_d; }
    DeviceData &dataRef() { return m_d; }
    QSizeF size() const { return m_size; }

    int portCount() const { return m_ports.size(); }
    QPointF portPos(int i) const { return m_ports.value(i); }         // local
    QPointF portScenePos(int i) const { return mapToScene(m_ports.value(i)); }
    QString portName(int i) const;
    const QVector<QPointF> &ports() const { return m_ports; }
    const QStringList &portNames() const { return m_portNames; }
    bool portFacesUp(int i) const { return m_ports.value(i).y() < m_size.height() / 2; }
    int portAt(const QPointF &scenePos, double tolerance) const;    // -1 if none

    void setLinkState(LinkState s) { if (s != m_state) { m_state = s; update(); } }
    LinkState linkState() const { return m_state; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    PlanScene *planScene() const;
    DeviceData m_d;
    QSizeF m_size;
    QVector<QPointF> m_ports;
    QStringList m_portNames;
    LinkState m_state = LinkState::Normal;
};

// ---------------------------------------------------------------- wire between two terminals
class WireItem : public QGraphicsItem
{
public:
    WireItem();
    int type() const override { return ItemType::Wire; }

    QString fromUid() const { return m_fromUid; }
    int fromPort() const { return m_fromPort; }
    QString toUid() const { return m_toUid; }
    int toPort() const { return m_toPort; }
    void setEnds(const QString &fromUid, int fromPort, const QString &toUid, int toPort);
    bool touches(const QString &uid) const { return m_fromUid == uid || m_toUid == uid; }

    QColor color() const { return m_color; }
    void setColor(const QColor &c) { m_color = c; update(); }
    QString crossSection() const { return m_section; }      // "1.5" (mm²)
    void setCrossSection(const QString &s) { m_section = s; update(); }
    QString label() const { return m_label; }
    void setLabel(const QString &l) { m_label = l; rebuild(); }

    EquipmentItem *fromItem() const;
    EquipmentItem *toItem() const;
    QString endText(bool from) const;       // "Q1 · 2"

    void setLinkState(LinkState s) { if (s != m_state) { m_state = s; rebuild(); } }
    void rebuild();

    // ---- route: straight segments with right angles
    // Automatic until a corner / segment is dragged; then the corners are kept (manual route).
    QPolygonF routePoints() const;                  // terminal, corners ..., terminal (scene)
    bool isManual() const { return m_manual; }
    const QVector<QPointF> &bends() const { return m_bends; }
    void setManualBends(const QVector<QPointF> &bends);   // empty = automatic
    void resetRoute();                              // back to automatic
    void translateBends(const QPointF &d);
    // editing (used by the grips): index = point index in routePoints()
    void beginEdit();
    void moveCorner(int index, const QPointF &scenePos);
    void moveSegment(int index, const QPointF &scenePos);   // segment index -> index+1
    void endEdit();
    void insertJog(const QPointF &scenePos);        // adds two corners on the nearest segment

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

    static QList<QPair<QString, QColor>> standardColors();
    static QPolygonF orthogonalize(const QPolygonF &pts);    // inserts the missing right-angle corners
    static QPolygonF simplify(const QPolygonF &pts);         // drops duplicate / collinear corners
    static QString colorName(const QColor &c);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override;

private:
    PlanScene *planScene() const;
    QPolygonF autoRoute(const QPointF &a, bool aUp, const QPointF &b, bool bUp, const QRectF &ra, const QRectF &rb) const;
    QPointF labelAnchor() const;
    void updateHandles();
    QString m_fromUid, m_toUid;
    int m_fromPort = 0, m_toPort = 0;
    QColor m_color = QColor(139, 69, 19);
    QString m_section = QStringLiteral("1.5");
    QString m_label;
    LinkState m_state = LinkState::Normal;
    QPainterPath m_path;
    QPolygonF m_route;
    QRectF m_bounds;
    QVector<QPointF> m_bends;       // manual corners (scene)
    bool m_manual = false;
    bool m_editing = false;
    QList<WireHandle *> m_handles;
};

#endif // PANELITEMS_H
