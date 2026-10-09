#ifndef NOTEITEM_H
#define NOTEITEM_H

#include "ItemTypes.h"

#include <QColor>
#include <QGraphicsItem>
#include <QString>

// Text label next to an element (e.g. "3x1.5", "Tree"). Can be dragged; offset is saved.
class NoteItem : public QGraphicsItem
{
public:
    explicit NoteItem(QGraphicsItem *parent);
    int type() const override { return ItemType::Note; }

    void setText(const QString &t);
    QString text() const { return m_text; }
    void setAccent(const QColor &c) { m_accent = c; update(); }

    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;

private:
    QRectF textRect() const;
    QString m_text;
    QColor  m_accent = Qt::gray;
};

#endif // NOTEITEM_H
