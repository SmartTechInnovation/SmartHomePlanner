#ifndef BACKGROUNDITEM_H
#define BACKGROUNDITEM_H

#include "ItemTypes.h"

#include <QByteArray>
#include <QGraphicsPixmapItem>

// The imported floor plan (image / rendered PDF page). Scale = cm per image pixel.
class BackgroundItem : public QGraphicsPixmapItem
{
public:
    BackgroundItem();
    int type() const override { return ItemType::Background; }

    bool setImageData(const QByteArray &data);   // PNG/JPG bytes
    const QByteArray &imageData() const { return m_data; }

    double cmPerPixel() const { return scale(); }
    void   setCmPerPixel(double s) { setScale(s); }
    bool   isLocked() const { return m_locked; }
    void   setLocked(bool l);

    void paint(QPainter *p, const QStyleOptionGraphicsItem *o, QWidget *w) override;

private:
    QByteArray m_data;
    bool m_locked = true;
};

#endif // BACKGROUNDITEM_H
