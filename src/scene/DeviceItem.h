#ifndef DEVICEITEM_H
#define DEVICEITEM_H

#include "ItemTypes.h"
#include "core/Library.h"

#include <QColor>
#include <QGraphicsItem>
#include <QString>

class NoteItem;
class EffectItem;
class PlanScene;

// Everything that describes one placed element. Saved 1:1 in the project file.
struct DeviceData {
    QString    uid;            // unique inside the project file; groups and wires refer to it
    QString    key;            // library entry key
    QString    name;
    QString    variant;
    QString    iconPath;
    QString    section;
    DeviceKind kind = DeviceKind::Generic;
    QString    id;             // L1, S3 ...
    QString    note;
    bool       showNote = true;
    QString    doc;
    QString    shop;
    double     size = 30;      // cm
    bool       tile = true;    // white rounded background behind the symbol
    double     rotation = 0;   // deg
    // light
    bool       on = false;
    double     beam = 60;      // deg
    double     mountHeight = 0;// cm, 0 = project ceiling height
    int        cct = 3000;
    QColor     rgb;            // valid => colored light instead of white CCT
    // speaker
    double     radius = 300;   // cm (speaker, Wi-Fi range)
    double     wallLoss = 35;  // Wi-Fi: % of the range lost per wall

    QString displayName() const { return variant.isEmpty() ? name : name + " (" + variant + ")"; }
    static DeviceData fromEntry(const LibraryEntry &e);
    static QString newUid();
};

class DeviceItem : public QGraphicsItem
{
public:
    explicit DeviceItem(const DeviceData &d);
    ~DeviceItem() override;
    int type() const override { return ItemType::Device; }

    const DeviceData &data() const { return m_d; }
    DeviceData &dataRef() { return m_d; }
    void setData(const DeviceData &d);
    void refresh();                         // call after changing dataRef()

    bool isLight() const   { return m_d.kind == DeviceKind::Light; }
    bool isSpeaker() const { return m_d.kind == DeviceKind::Speaker; }
    bool isWifi() const    { return m_d.kind == DeviceKind::Wifi; }
    bool hasEffect() const { return isLight() || isSpeaker() || isWifi(); }
    void setOn(bool on);

    double symbolSize() const;
    double effectRadius() const;            // light cone / speaker coverage radius [cm]
    double mountHeight() const;             // effective, cm
    QColor lightColor() const;

    NoteItem *noteItem() const { return m_note; }
    EffectItem *effectItem() const { return m_effect; }
    void detachEffect() { m_effect = nullptr; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override;

private:
    PlanScene *planScene() const;
    void syncEffect();

    DeviceData  m_d;
    NoteItem   *m_note = nullptr;
    EffectItem *m_effect = nullptr;
};

#endif // DEVICEITEM_H
