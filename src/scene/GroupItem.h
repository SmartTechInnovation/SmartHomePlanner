#ifndef GROUPITEM_H
#define GROUPITEM_H

#include "ItemTypes.h"

#include <QColor>
#include <QGraphicsItem>
#include <QPainterPath>
#include <QStringList>

class PlanScene;

// A group / circuit: elements linked two by two with curved dashed lines in the group color
// (e.g. a switch and the lights it controls). A link joins two elements (by uid); an element can
// have any number of links (chain, star ...).
class GroupItem : public QGraphicsItem
{
public:
    enum Style { Curved, Straight };

    GroupItem();
    int type() const override { return ItemType::Group; }

    QString name() const { return m_name; }
    void setName(const QString &n) { m_name = n; rebuild(); }
    QColor color() const { return m_color; }
    void setColor(const QColor &c) { m_color = c; update(); }
    Style style() const { return m_style; }
    void setStyle(Style s) { m_style = s; rebuild(); }
    QString note() const { return m_note; }
    void setNote(const QString &n) { m_note = n; }

    using Link = QPair<QString, QString>;
    const QVector<Link> &links() const { return m_links; }
    void setLinks(const QVector<Link> &links) { m_links = links; m_chain.clear(); rebuild(); }
    bool addLink(const QString &a, const QString &b);      // false if it exists already / a == b
    bool hasLink(const QString &a, const QString &b) const;
    void removeLink(int index);
    int linkAt(const QPointF &scenePos) const;             // nearest drawn link, -1 if none

    QStringList members() const;                            // every linked element, in order
    // links the elements one after the other, nearest first (Ctrl+G, files of older versions)
    void setMembers(const QStringList &uids) { m_links.clear(); m_chain = uids; rebuild(); }
    void addMember(const QString &uid);                     // linked to the nearest member
    void removeMember(const QString &uid);                  // drops all its links
    bool contains(const QString &uid) const;

    // the member elements that still exist in the scene
    QList<QGraphicsItem *> memberItems() const;

    void rebuild();                     // after members moved / were hidden
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

    static QColor paletteColor(int index);
    static QPointF anchorOf(const QGraphicsItem *it);     // where the line touches an element
    static double radiusOf(const QGraphicsItem *it);      // gap left around the element

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    PlanScene *planScene() const;
    QString m_name;
    QString m_note;
    QColor m_color = QColor(220, 40, 40);
    Style m_style = Curved;
    QVector<Link> m_links;
    QStringList m_chain;            // to be linked as a chain once the elements are in the scene
    QVector<QPainterPath> m_linkPaths;     // drawn links, parallel to m_linkIndex
    QVector<int> m_linkIndex;
    QPainterPath m_path;
    QPointF m_labelPos;
    QRectF m_bounds;
};

#endif // GROUPITEM_H
