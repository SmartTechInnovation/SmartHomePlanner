#include "DeviceItem.h"
#include "EffectItem.h"
#include "NoteItem.h"
#include "PlanScene.h"
#include "core/Geometry.h"
#include "core/IconCache.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QUuid>

QString DeviceData::newUid()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

DeviceData DeviceData::fromEntry(const LibraryEntry &e)
{
    DeviceData d;
    d.uid = newUid();
    d.key = e.key;
    d.name = e.name;
    d.variant = e.variant;
    d.iconPath = e.iconPath;
    d.section = e.section;
    d.kind = e.kind;
    d.note = e.note;
    d.doc = e.doc;
    d.shop = e.shop;
    d.size = e.size;
    d.tile = e.wantsTile();
    if (e.beam > 0) d.beam = e.beam;
    if (e.cct > 0)  d.cct = e.cct;
    if (e.radius > 0) d.radius = e.radius;
    d.wallLoss = e.wallLoss;
    if (e.kind == DeviceKind::Wifi) {
        if (e.radius <= 0)
            d.radius = 1500;            // typical indoor range of an access point
    }
    return d;
}

DeviceItem::DeviceItem(const DeviceData &d) : m_d(d)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setZValue(ZOrder::Device);
    m_note = new NoteItem(this);
    m_note->setPos(symbolSize() / 2 + 4, -symbolSize() / 2 - 4);
    refresh();
}

DeviceItem::~DeviceItem()
{
    if (m_effect) {
        m_effect->detachDevice();
        delete m_effect;
        m_effect = nullptr;
    }
}

PlanScene *DeviceItem::planScene() const
{
    return qobject_cast<PlanScene *>(scene());
}

void DeviceItem::setData(const DeviceData &d)
{
    m_d = d;
    refresh();
}

void DeviceItem::refresh()
{
    prepareGeometryChange();
    m_note->setText(m_d.note);
    const SectionDef *s = Library::instance().section(m_d.section);
    m_note->setAccent(s ? s->color : QColor(Qt::gray));
    PlanScene *ps = planScene();
    const bool sectionVisible = !ps || ps->isSectionVisible(m_d.section);
    m_note->setVisible(m_d.showNote && !m_d.note.isEmpty() && (!ps || ps->showNotes()));
    setVisible(sectionVisible);
    syncEffect();
    update();
}

void DeviceItem::setOn(bool on)
{
    m_d.on = on;
    if (m_effect)
        m_effect->invalidate();
    update();
}

double DeviceItem::symbolSize() const
{
    const PlanScene *ps = planScene();
    return m_d.size * (ps ? ps->settings().symbolScale : 1.0);
}

double DeviceItem::mountHeight() const
{
    const PlanScene *ps = planScene();
    if (m_d.mountHeight > 0)
        return m_d.mountHeight;
    return ps ? ps->settings().ceilingHeight : 270.0;
}

double DeviceItem::effectRadius() const
{
    if (isSpeaker() || isWifi())
        return m_d.radius;
    if (isLight()) {
        const PlanScene *ps = planScene();
        return Geo::lightRadius(m_d.beam, mountHeight(), ps ? ps->settings().workPlane : 0.0);
    }
    return 0;
}

QColor DeviceItem::lightColor() const
{
    if (m_d.rgb.isValid())
        return m_d.rgb;
    return Geo::colorFromKelvin(m_d.cct);
}

void DeviceItem::syncEffect()
{
    PlanScene *ps = planScene();
    if (!ps)
        return;
    if (hasEffect() && !m_effect) {
        m_effect = new EffectItem(this);
        ps->addItem(m_effect);
    } else if (!hasEffect() && m_effect) {
        m_effect->detachDevice();
        delete m_effect;
        m_effect = nullptr;
    }
    if (m_effect) {
        m_effect->setVisible(isVisible());
        m_effect->invalidate();
    }
}

QRectF DeviceItem::boundingRect() const
{
    const double s = symbolSize();
    const double m = std::max(4.0, s * 0.12);       // tile + light ring
    return QRectF(-s / 2 - m, -s / 2 - TextSize::Id - 6, s + 2 * m + 40, s + TextSize::Id + 6 + m);
}

QPainterPath DeviceItem::shape() const
{
    const double s = symbolSize();
    QPainterPath p;
    p.addRect(QRectF(-s / 2, -s / 2, s, s));
    return p;
}

void DeviceItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    PlanScene *ps = planScene();
    const bool exporting = ps && ps->isExporting();
    const double s = symbolSize();
    const QRectF r(-s / 2, -s / 2, s, s);
    p->setRenderHint(QPainter::Antialiasing);

    if (isLight() && m_d.on) {
        QColor c = lightColor();
        c.setAlpha(170);
        p->setPen(QPen(c, 3));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(r.adjusted(-3, -3, 3, 3));
    }

    // white rounded tile behind the symbol: readable on walls and dark floor plans (per element)
    if (m_d.tile) {
        p->setPen(QPen(QColor(170, 170, 170), std::max(0.4, s * 0.025)));
        p->setBrush(QColor(255, 255, 255, 240));
        p->drawRoundedRect(r.adjusted(-s * 0.08, -s * 0.08, s * 0.08, s * 0.08), s * 0.2, s * 0.2);
    }

    p->save();
    p->rotate(m_d.rotation);
    IconCache::instance().draw(p, m_d.iconPath, r);
    p->restore();

    if (!m_d.id.isEmpty()) {
        QFont f = p->font();
        f.setPixelSize(TextSize::Id);
        f.setBold(true);
        p->setFont(f);
        const SectionDef *sec = Library::instance().section(m_d.section);
        p->setPen(sec ? sec->color.darker(120) : QColor(Qt::black));
        p->drawText(QRectF(-s / 2, -s / 2 - TextSize::Id - 4, s + 40, TextSize::Id + 3),
                    Qt::AlignLeft | Qt::AlignBottom, m_d.id);
    }

    if (isSelected() && !exporting) {
        p->setPen(QPen(QColor(0, 120, 215), 0, Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawRect(r.adjusted(-2, -2, 2, 2));
    }
}

QVariant DeviceItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    switch (change) {
    case ItemPositionChange:
        if (PlanScene *ps = planScene())
            return ps->alignDevice(this, value.toPointF());     // in line with the other elements
        break;
    case ItemPositionHasChanged:
        if (m_effect)
            m_effect->invalidate();
        if (PlanScene *ps = planScene())
            ps->scheduleLinkUpdate();
        break;
    case ItemVisibleHasChanged:
        if (m_effect)
            m_effect->setVisible(isVisible());
        if (PlanScene *ps = planScene())
            ps->scheduleLinkUpdate();
        break;
    case ItemSelectedHasChanged:
        if (m_effect)
            m_effect->update();
        break;
    case ItemSceneChange:
        if (m_effect && value.value<QGraphicsScene *>() == nullptr) {
            m_effect->detachDevice();
            delete m_effect;
            m_effect = nullptr;
        }
        break;
    case ItemSceneHasChanged:
        if (scene())
            refresh();
        break;
    default:
        break;
    }
    return QGraphicsItem::itemChange(change, value);
}

void DeviceItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e)
{
    if (hasEffect() && !isWifi() && e->button() == Qt::LeftButton) {     // Wi-Fi: shown while selected
        setOn(!m_d.on);
        if (PlanScene *ps = planScene())
            ps->notifyItemEdited(this);
        e->accept();
        return;
    }
    QGraphicsItem::mouseDoubleClickEvent(e);
}
