#include "PlanScene.h"

#include "BackgroundItem.h"
#include "DeviceItem.h"
#include "EffectItem.h"
#include "GroupItem.h"
#include "PanelItems.h"
#include "HandleItem.h"
#include "NoteItem.h"
#include "RoomItem.h"
#include "SceneSerializer.h"
#include "StripItem.h"
#include "WallItem.h"
#include "core/Geometry.h"

#include <QApplication>
#include <QGraphicsView>
#include <QInputDialog>
#include <QMenu>
#include <QClipboard>
#include <QGraphicsPathItem>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QTimer>
#include <QUndoCommand>
#include <algorithm>
#include <cmath>

namespace {

constexpr const char *kEntryMime = "application/x-smarthomeplanner-entry";

class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(PlanScene *scene, const QString &before, const QString &after, const QString &label)
        : QUndoCommand(label), m_scene(scene), m_before(before), m_after(after) {}
    void undo() override { apply(m_before); }
    void redo() override
    {
        if (m_first) {         // state is already applied when the command is pushed
            m_first = false;
            return;
        }
        apply(m_after);
    }

private:
    void apply(const QString &s)
    {
        SceneSerializer::restoreSnapshot(m_scene, s);
        m_scene->setLastSnapshot(s);
        emit m_scene->modified();
    }
    PlanScene *m_scene;
    QString m_before, m_after;
    bool m_first = true;
};

} // namespace

PlanScene::PlanScene(QObject *parent) : QGraphicsScene(parent)
{
    setSceneRect(-10000, -10000, 30000, 30000);   // 300 x 300 m working area
    setItemIndexMethod(QGraphicsScene::NoIndex);
    m_nightItem = new NightItem;
    m_nightItem->setVisible(false);
    addItem(m_nightItem);
    m_lastSnapshot = SceneSerializer::snapshot(this);

    connect(this, &QGraphicsScene::selectionChanged, this, [this] {
        if (!isRestoring())
            updateHighlight();
    });
    // tell how to edit the selected outline
    connect(this, &QGraphicsScene::selectionChanged, this, [this] {
        if (isRestoring())
            return;
        const QList<QGraphicsItem *> sel = selectedItems();
        if (sel.size() != 1)
            return;
        const int t = sel.first()->type();
        if ((t == ItemType::Room || t == ItemType::Wall) && isEditableType(t))
            emit statusMessage(tr("Drag the blue corners to change the outline. Double-click an edge = add a corner, "
                                  "double-click a corner = delete it. Right-click for more."));
        else if (t == ItemType::Strip)
            emit statusMessage(tr("Drag the blue points to change the LED strip. Right-click = add / delete points."));
    });
}

PlanScene::~PlanScene()
{
    beginRestore();
    clearContent();
}

void PlanScene::newProject()
{
    beginRestore();
    clearContent();
    removeBackground();
    m_settings = ProjectSettings();
    m_hiddenSections.clear();
    endRestore();
    resetUndo();
    emit sectionsChanged();
    emit backgroundChanged();
}

// ------------------------------------------------------------------ background

bool PlanScene::setBackgroundImage(const QByteArray &imageData)
{
    if (!m_background) {
        m_background = new BackgroundItem;
        addItem(m_background);
    }
    if (!m_background->setImageData(imageData)) {
        removeBackground();
        return false;
    }
    emit backgroundChanged();
    return true;
}

void PlanScene::removeBackground()
{
    delete m_background;
    m_background = nullptr;
    emit backgroundChanged();
}

// ------------------------------------------------------------------ display

void PlanScene::setExporting(bool on, bool effects)
{
    m_exporting = on;
    m_exportEffects = effects;
    if (m_nightItem)
        m_nightItem->setVisible(m_night && !on);
    clearMeasure();
    if (m_preview)
        m_preview->setVisible(!on);
    if (m_snapMarker && on)
        m_snapMarker->hide();
    if (!m_showRoomNames || !m_showRoomAreas)
        for (RoomItem *r : rooms())
            r->refreshLabel();
    update();
}

void PlanScene::setNightMode(bool on)
{
    m_night = on;
    m_nightItem->setVisible(on);
    update();
}

void PlanScene::setShowBeams(bool on)
{
    m_showBeams = on;
    update();
}

void PlanScene::setShowNotes(bool on)
{
    m_showNotes = on;
    refreshAll();
}

void PlanScene::setShowRoomNames(bool on)
{
    m_showRoomNames = on;
    for (RoomItem *r : rooms())
        r->refreshLabel();
}

void PlanScene::setShowRoomAreas(bool on)
{
    m_showRoomAreas = on;
    for (RoomItem *r : rooms())
        r->refreshLabel();
}

bool PlanScene::isEditableType(int t) const
{
    if (t == ItemType::Room)
        return m_tool == RoomTool;
    if (t == ItemType::Wall)
        return m_tool == WallTool;
    return m_tool == SelectTool;
}

void PlanScene::applyEditability(QGraphicsItem *it)
{
    const int t = it->type();
    if (t == ItemType::Room || t == ItemType::Wall) {
        const bool on = isEditableType(t);
        // never moved as a whole block: only the corners are dragged
        it->setFlag(QGraphicsItem::ItemIsMovable, false);
        it->setFlag(QGraphicsItem::ItemIsSelectable, on);
        for (QGraphicsItem *c : it->childItems())       // room label
            if (c->type() != ItemType::Handle) {
                c->setFlag(QGraphicsItem::ItemIsMovable, on);
                c->setAcceptedMouseButtons(on ? Qt::LeftButton : Qt::NoButton);
                if (on)
                    c->setCursor(Qt::OpenHandCursor);
                else
                    c->unsetCursor();
            }
    } else if (t == ItemType::Device || t == ItemType::Strip || t == ItemType::Equipment || t == ItemType::Rail) {
        it->setFlag(QGraphicsItem::ItemIsMovable, m_tool == SelectTool);
    }
}

QGraphicsItem *PlanScene::outlineAt(const QPointF &pos, int itemType) const
{
    const QTransform vt = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
    for (QGraphicsItem *it : items(pos, Qt::IntersectsItemShape, Qt::DescendingOrder, vt))
        if (it->type() == itemType && it->isVisible())
            return it;
    return nullptr;
}

bool PlanScene::editPress(QGraphicsSceneMouseEvent *e)
{
    const int want = m_tool == RoomTool ? int(ItemType::Room) : int(ItemType::Wall);
    const QPointF pos = e->scenePos();
    const QTransform vt = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
    const double tol = 10.0 / std::max(1e-6, m_viewScale);

    // 1. a corner handle of the selected outline, or the room label -> drag it
    for (QGraphicsItem *it : items(pos, Qt::IntersectsItemShape, Qt::DescendingOrder, vt)) {
        const bool handle = it->type() == ItemType::Handle && static_cast<HandleItem *>(it)->owner()->type() == want;
        const bool label = it->parentItem() && it->parentItem()->type() == want && it->type() != ItemType::Handle
                           && (it->flags() & QGraphicsItem::ItemIsMovable);
        if (handle || label) {
            if (label && !it->parentItem()->isSelected()) {
                clearSelection();
                it->parentItem()->setSelected(true);
            }
            m_editing = true;
            QGraphicsScene::mousePressEvent(e);
            return true;
        }
    }
    // 2. on an edge of the selected outline: keep it selected (double-click there adds a corner)
    for (QGraphicsItem *it : selectedItems()) {
        if (it->type() != want)
            continue;
        const double extra = want == ItemType::Wall ? static_cast<WallItem *>(it)->thickness() / 2 : 0;
        if (static_cast<PolyItem *>(it)->edgeAt(pos, tol + extra) >= 0) {
            e->accept();
            return true;
        }
    }
    // 3. Room tool: click inside a room (away from its border) selects it for editing
    if (m_tool == RoomTool) {
        for (RoomItem *r : rooms()) {
            if (!r->isVisible() || !r->shape().contains(r->mapFromScene(pos)) || r->edgeAt(pos, tol) >= 0)
                continue;
            clearSelection();
            r->setSelected(true);
            e->accept();
            return true;
        }
    }
    clearSelection();
    return false;
}

void PlanScene::setSectionVisible(const QString &id, bool visible)
{
    if (visible)
        m_hiddenSections.remove(id);
    else
        m_hiddenSections.insert(id);
    refreshAll();
    emit sectionsChanged();
}

void PlanScene::setHiddenSections(const QSet<QString> &hidden)
{
    m_hiddenSections = hidden;
    refreshAll();
    emit sectionsChanged();
}

QSet<QString> PlanScene::usedSections() const
{
    QSet<QString> s;
    for (DeviceItem *d : devices()) s.insert(d->data().section);
    for (StripItem *st : strips())  s.insert(st->data().section);
    return s;
}

// ------------------------------------------------------------------ geometry

QVector<QLineF> PlanScene::wallObstacles()
{
    if (m_obstaclesDirty) {
        m_obstacles.clear();
        for (WallItem *w : walls())
            m_obstacles += w->obstacleEdges();
        m_obstaclesDirty = false;
    }
    return m_obstacles;
}

void PlanScene::notifyWallsChanged()
{
    m_obstaclesDirty = true;
    m_facesDirty = true;
    if (m_wallNotifyPending)
        return;
    m_wallNotifyPending = true;
    QTimer::singleShot(0, this, [this] {
        m_wallNotifyPending = false;
        for (DeviceItem *d : devices())
            if (EffectItem *e = d->effectItem())
                e->invalidate();
    });
}

QPointF PlanScene::snapToGrid(const QPointF &p) const
{
    if (!m_gridSnap || m_settings.gridSize <= 0 || (QApplication::keyboardModifiers() & Qt::AltModifier))
        return p;
    const double g = m_settings.gridSize;
    return QPointF(std::round(p.x() / g) * g, std::round(p.y() / g) * g);
}

void PlanScene::updateFaces() const
{
    if (!m_facesDirty)
        return;
    m_faceCorners.clear();
    m_faceEdges.clear();
    QPainterPath all;
    for (WallItem *w : walls())
        all = all.united(w->mapToScene(w->shape()));
    all = all.simplified();
    for (QPolygonF poly : all.toSubpathPolygons()) {
        if (poly.size() > 1 && QLineF(poly.first(), poly.last()).length() < 1e-6)
            poly.removeLast();
        // drop points in the middle of a straight face (seams of the outline), they are not corners
        for (int i = poly.size() - 1; i >= 0 && poly.size() > 3; --i) {
            const QPointF a = poly[(i + poly.size() - 1) % poly.size()];
            const QPointF b = poly[i];
            const QPointF c = poly[(i + 1) % poly.size()];
            const double cross = (b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
            if (std::abs(cross) < 1e-6 * (QLineF(a, b).length() * QLineF(b, c).length() + 1e-9))
                poly.remove(i);
        }
        const int n = poly.size();
        for (int i = 0; i < n; ++i) {
            m_faceCorners << poly[i];
            if (QLineF(poly[i], poly[(i + 1) % n]).length() > 1e-6)
                m_faceEdges << QLineF(poly[i], poly[(i + 1) % n]);
        }
    }
    m_facesDirty = false;
}

static QPointF projectOnSegment(const QPointF &p, const QLineF &l)
{
    const QPointF d = l.p2() - l.p1();
    const double len2 = d.x() * d.x() + d.y() * d.y();
    if (len2 < 1e-12)
        return l.p1();
    double t = ((p.x() - l.x1()) * d.x() + (p.y() - l.y1()) * d.y()) / len2;
    t = std::clamp(t, 0.0, 1.0);
    return l.p1() + d * t;
}

QPointF PlanScene::snapPoint(const QPointF &p, const QGraphicsItem *ignore, bool grid, bool faces, SnapKind *kind) const
{
    auto done = [&](const QPointF &r, SnapKind k) {
        m_lastSnap = k;
        if (kind)
            *kind = k;
        return r;
    };
    if (!m_snap || (QApplication::keyboardModifiers() & Qt::AltModifier))
        return done(grid && m_gridSnap ? snapToGrid(p) : p, grid && m_gridSnap ? GridSnap : NoSnap);
    const double tol = 12.0 / std::max(1e-6, m_viewScale);

    // 1. corners
    double best = tol;
    QPointF bestPt;
    bool found = false;
    auto consider = [&](const QPointF &q) {
        const double d = QLineF(p, q).length();
        if (d < best) {
            best = d;
            bestPt = q;
            found = true;
        }
    };
    if (faces) {
        updateFaces();
        for (const QPointF &q : m_faceCorners)
            consider(q);
    }
    for (QGraphicsItem *it : items()) {
        if (it == ignore)
            continue;
        if ((!faces && it->type() == ItemType::Wall) || it->type() == ItemType::Room)
            for (const QPointF &q : static_cast<PolyItem *>(it)->scenePoints())
                consider(q);
    }
    for (const QPointF &q : m_drawPts)
        consider(q);
    if (found)
        return done(bestPt, CornerSnap);

    // 2. a point on a wall face
    if (faces) {
        best = tol;
        for (const QLineF &e : m_faceEdges) {
            const QPointF q = projectOnSegment(p, e);
            const double d = QLineF(p, q).length();
            if (d < best) {
                best = d;
                bestPt = q;
                found = true;
            }
        }
        if (found)
            return done(bestPt, EdgeSnap);
    }

    // 3. grid
    if (grid && m_gridSnap)
        return done(snapToGrid(p), GridSnap);
    return done(p, NoSnap);
}

void PlanScene::showSnapMarker(const QPointF &p, SnapKind kind)
{
    if (kind == NoSnap || kind == GridSnap || m_tool == SelectTool) {
        if (m_snapMarker)
            m_snapMarker->hide();
        return;
    }
    if (!m_snapMarker) {
        m_snapMarker = new QGraphicsPathItem;
        m_snapMarker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        m_snapMarker->setZValue(ZOrder::Preview + 1);
        m_snapMarker->setAcceptedMouseButtons(Qt::NoButton);
        addItem(m_snapMarker);
    }
    QPainterPath path;
    if (kind == CornerSnap) {
        path.addRect(-6, -6, 12, 12);
        m_snapMarker->setPen(QPen(QColor(0, 160, 60), 2));
    } else {
        path.moveTo(0, -7);
        path.lineTo(7, 0);
        path.lineTo(0, 7);
        path.lineTo(-7, 0);
        path.closeSubpath();
        m_snapMarker->setPen(QPen(QColor(230, 120, 0), 2));
    }
    m_snapMarker->setPath(path);
    m_snapMarker->setPos(p);
    m_snapMarker->show();
}

// ------------------------------------------------------------------ content

QString PlanScene::nextId(const QString &section) const
{
    QString prefix;
    if (section.startsWith(QLatin1Char('@'))) {
        prefix = section.mid(1);            // explicit prefix (panel equipment: Q, K ...)
    } else {
        const SectionDef *s = Library::instance().section(section);
        prefix = s ? s->prefix : QStringLiteral("X");
    }
    const QRegularExpression re("^" + QRegularExpression::escape(prefix) + "(\\d+)$");
    int maxN = 0;
    auto scan = [&](const QString &id) {
        const auto m = re.match(id);
        if (m.hasMatch())
            maxN = std::max(maxN, m.captured(1).toInt());
    };
    for (DeviceItem *d : devices()) scan(d->data().id);
    for (StripItem *st : strips())  scan(st->data().id);
    for (EquipmentItem *e : equipment()) scan(e->data().id);
    return prefix + QString::number(maxN + 1);
}

DeviceItem *PlanScene::addDevice(const LibraryEntry &e, const QPointF &scenePos)
{
    DeviceData d = DeviceData::fromEntry(e);
    d.id = nextId(d.section);
    auto *item = new DeviceItem(d);
    item->setPos(snapToGrid(scenePos));
    addItem(item);
    if (!isSectionVisible(d.section))
        setSectionVisible(d.section, true);
    clearSelection();
    item->setSelected(true);
    commit(tr("Add %1").arg(e.displayName()));
    return item;
}

static DeviceData stripData(const LibraryEntry &e)
{
    DeviceData d = DeviceData::fromEntry(e);
    d.kind = DeviceKind::Strip;
    d.rgb = e.color.isValid() ? e.color : QColor(255, 220, 150);
    d.radius = e.glow > 0 ? e.glow : 35;
    d.on = true;
    return d;
}

StripItem *PlanScene::addStripLine(const LibraryEntry &e, const QPolygonF &scenePts)
{
    if (scenePts.size() < 2)
        return nullptr;
    DeviceData d = stripData(e);
    d.id = nextId(d.section);
    auto *s = new StripItem(d, StripItem::Line);
    s->setPos(scenePts.first());
    s->setPoints(scenePts.translated(-scenePts.first()));
    addItem(s);
    if (!isSectionVisible(d.section))
        setSectionVisible(d.section, true);
    clearSelection();
    s->setSelected(true);
    commit(tr("Add LED strip"));
    return s;
}

StripItem *PlanScene::addStripCircle(const LibraryEntry &e, const QPointF &center, double radius)
{
    DeviceData d = stripData(e);
    d.id = nextId(d.section);
    auto *s = new StripItem(d, StripItem::Circle);
    s->setPos(center);
    s->setCircleRadius(radius);
    addItem(s);
    if (!isSectionVisible(d.section))
        setSectionVisible(d.section, true);
    clearSelection();
    s->setSelected(true);
    commit(tr("Add LED circle"));
    return s;
}

void PlanScene::renumberIds()
{
    struct Ref { QGraphicsItem *item; QPointF pos; QString section; };
    QHash<QString, QList<Ref>> bySection;
    for (DeviceItem *d : devices())
        bySection[d->data().section].append({d, d->scenePos(), d->data().section});
    for (StripItem *s : strips())
        bySection[s->data().section].append({s, s->sceneBoundingRect().center(), s->data().section});
    for (auto it = bySection.begin(); it != bySection.end(); ++it) {
        QList<Ref> &list = it.value();
        // reading order: rows of 1.5 m from top to bottom, left to right inside a row
        std::sort(list.begin(), list.end(), [](const Ref &a, const Ref &b) {
            const int ra = int(std::floor(a.pos.y() / 150.0));
            const int rb = int(std::floor(b.pos.y() / 150.0));
            if (ra != rb) return ra < rb;
            return a.pos.x() < b.pos.x();
        });
        const SectionDef *s = Library::instance().section(it.key());
        const QString prefix = s ? s->prefix : QStringLiteral("X");
        int n = 1;
        for (const Ref &r : list) {
            const QString id = prefix + QString::number(n++);
            if (r.item->type() == ItemType::Device) {
                auto *d = static_cast<DeviceItem *>(r.item);
                d->dataRef().id = id;
                d->update();
            } else {
                auto *st = static_cast<StripItem *>(r.item);
                st->dataRef().id = id;
                st->update();
            }
        }
    }
    // panel equipment: per prefix (Q, K ...), rail by rail from top to bottom, left to right
    QHash<QString, QList<EquipmentItem *>> byPrefix;
    for (EquipmentItem *e : equipment()) {
        const LibraryEntry *le = Library::instance().entry(e->data().key);
        QString prefix = le && !le->idPrefix.isEmpty() ? le->idPrefix : QString();
        if (prefix.isEmpty()) {
            static const QRegularExpression letters(QStringLiteral("^([A-Za-z]+)"));
            const auto m = letters.match(e->data().id);
            prefix = m.hasMatch() ? m.captured(1) : QStringLiteral("Q");
        }
        byPrefix[prefix] << e;
    }
    for (auto it = byPrefix.begin(); it != byPrefix.end(); ++it) {
        QList<EquipmentItem *> &list = it.value();
        std::sort(list.begin(), list.end(), [](EquipmentItem *a, EquipmentItem *b) {
            const QPointF pa = a->sceneBoundingRect().center(), pb = b->sceneBoundingRect().center();
            const int ra = int(std::floor(pa.y() / 120.0)), rb = int(std::floor(pb.y() / 120.0));
            if (ra != rb) return ra < rb;
            return pa.x() < pb.x();
        });
        int n = 1;
        for (EquipmentItem *e : list) {
            e->dataRef().id = it.key() + QString::number(n++);
            e->update();
        }
    }
    updateLinks();
    commit(tr("Renumber IDs"));
}

QList<QGraphicsItem *> PlanScene::contentItems() const
{
    QList<QGraphicsItem *> list;
    for (QGraphicsItem *it : items(Qt::AscendingOrder)) {
        const int t = it->type();
        if ((t == ItemType::Wall || t == ItemType::Room || t == ItemType::Device || t == ItemType::Strip
             || t == ItemType::Group || t == ItemType::Rail || t == ItemType::Equipment || t == ItemType::Wire)
            && it->parentItem() == nullptr)
            list << it;
    }
    return list;
}

template <typename T>
static QList<T *> itemsOfType(const QGraphicsScene *s, int type)
{
    QList<T *> out;
    for (QGraphicsItem *it : s->items(Qt::AscendingOrder))
        if (it->type() == type)
            out << static_cast<T *>(it);
    return out;
}

QList<DeviceItem *> PlanScene::devices() const { return itemsOfType<DeviceItem>(this, ItemType::Device); }
QList<StripItem *>  PlanScene::strips() const  { return itemsOfType<StripItem>(this, ItemType::Strip); }
QList<RoomItem *>   PlanScene::rooms() const   { return itemsOfType<RoomItem>(this, ItemType::Room); }
QList<WallItem *>   PlanScene::walls() const   { return itemsOfType<WallItem>(this, ItemType::Wall); }
QList<GroupItem *>  PlanScene::groups() const  { return itemsOfType<GroupItem>(this, ItemType::Group); }
QList<RailItem *>   PlanScene::rails() const   { return itemsOfType<RailItem>(this, ItemType::Rail); }
QList<EquipmentItem *> PlanScene::equipment() const { return itemsOfType<EquipmentItem>(this, ItemType::Equipment); }
QList<WireItem *>   PlanScene::wires() const   { return itemsOfType<WireItem>(this, ItemType::Wire); }

QRectF PlanScene::contentRect() const
{
    QRectF r;
    for (QGraphicsItem *it : contentItems()) {
        // the real drawing (not the margins kept for labels), plus visible notes
        const int t = it->type();
        if (t == ItemType::Group || t == ItemType::Wire) {
            if (it->boundingRect().isEmpty() || !it->isVisible())
                continue;
        }
        QRectF b = (t == ItemType::Equipment || t == ItemType::Rail || t == ItemType::Group || t == ItemType::Wire)
                       ? it->sceneBoundingRect()
                       : it->mapToScene(it->shape()).boundingRect();
        for (QGraphicsItem *child : it->childItems())
            if (child->isVisible() && child->type() == ItemType::Note)
                b = b.united(child->sceneBoundingRect());
        r = r.isNull() ? b : r.united(b);
    }
    if (m_background)
        r = r.isNull() ? m_background->sceneBoundingRect() : r.united(m_background->sceneBoundingRect());
    return r;
}

void PlanScene::clearContent()
{
    cancelDrawing();
    clearMeasure();
    clearSelection();
    const QList<QGraphicsItem *> list = contentItems();
    for (QGraphicsItem *it : list)
        delete it;
    m_obstaclesDirty = true;
    m_facesDirty = true;
}

void PlanScene::refreshAll()
{
    for (DeviceItem *d : devices())
        d->refresh();
    for (StripItem *s : strips())
        s->refresh();
    m_obstaclesDirty = true;
    m_facesDirty = true;
    notifyWallsChanged();
    updateLinks();
    update();
}

void PlanScene::deleteSelection()
{
    const QList<QGraphicsItem *> sel = selectedItems();
    bool any = false;
    // wires of deleted equipment go with it
    for (QGraphicsItem *it : sel)
        if (it->type() == ItemType::Equipment)
            for (WireItem *w : wiresOf(static_cast<EquipmentItem *>(it)->data().uid))
                if (!sel.contains(w)) {
                    delete w;
                    any = true;
                }
    for (QGraphicsItem *it : sel) {
        const int t = it->type();
        if (t == ItemType::Wall || t == ItemType::Room || t == ItemType::Device || t == ItemType::Strip
            || t == ItemType::Group || t == ItemType::Rail || t == ItemType::Equipment || t == ItemType::Wire) {
            const bool wall = t == ItemType::Wall;
            delete it;
            any = true;
            if (wall)
                notifyWallsChanged();
        }
    }
    if (any) {
        updateLinks();
        commit(tr("Delete"));
    }
}

void PlanScene::copySelection()
{
    QList<QGraphicsItem *> sel;
    for (QGraphicsItem *it : selectedItems())
        if (contentItems().contains(it))
            sel << it;
    if (sel.isEmpty())
        return;
    auto *mime = new QMimeData;
    mime->setData(SceneSerializer::kClipboardMime, SceneSerializer::itemsToClipboard(sel));
    QApplication::clipboard()->setMimeData(mime);
}

void PlanScene::paste(const QPointF &at)
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(SceneSerializer::kClipboardMime))
        return;
    const QByteArray data = mime->data(SceneSerializer::kClipboardMime);
    // place the copied group so that its top-left lands at 'at'
    QList<QGraphicsItem *> created = SceneSerializer::itemsFromClipboard(this, data, QPointF());
    if (created.isEmpty())
        return;
    // groups and wires follow their elements: they are not moved themselves
    auto isLink = [](const QGraphicsItem *it) { return it->type() == ItemType::Group || it->type() == ItemType::Wire; };
    QRectF r;
    for (QGraphicsItem *it : created)
        if (!isLink(it))
            r = r.isNull() ? it->sceneBoundingRect() : r.united(it->sceneBoundingRect());
    const QPointF delta = snapToGrid(at - r.topLeft());
    clearSelection();
    for (QGraphicsItem *it : created) {
        if (!isLink(it))
            it->moveBy(delta.x(), delta.y());
        else if (it->type() == ItemType::Wire)
            static_cast<WireItem *>(it)->translateBends(delta);
        if (it->type() == ItemType::Device) {
            auto *d = static_cast<DeviceItem *>(it);
            d->dataRef().id = QString();
            d->dataRef().id = nextId(d->data().section);
            d->update();
        } else if (it->type() == ItemType::Strip) {
            auto *s = static_cast<StripItem *>(it);
            s->dataRef().id = QString();
            s->dataRef().id = nextId(s->data().section);
            s->update();
        } else if (it->type() == ItemType::Equipment) {
            auto *e = static_cast<EquipmentItem *>(it);
            const LibraryEntry *le = Library::instance().entry(e->data().key);
            e->dataRef().id = QString();
            e->dataRef().id = nextId(QStringLiteral("@") + (le && !le->idPrefix.isEmpty() ? le->idPrefix : QStringLiteral("Q")));
            e->update();
        }
        if (it->flags() & QGraphicsItem::ItemIsSelectable)
            it->setSelected(true);
    }
    notifyWallsChanged();
    updateLinks();
    commit(tr("Paste"));
}

void PlanScene::duplicateSelection()
{
    QList<QGraphicsItem *> sel;
    for (QGraphicsItem *it : selectedItems())
        if (contentItems().contains(it))
            sel << it;
    if (sel.isEmpty())
        return;
    const QByteArray data = SceneSerializer::itemsToClipboard(sel);
    QList<QGraphicsItem *> created = SceneSerializer::itemsFromClipboard(this, data, QPointF(40, 40));
    clearSelection();
    for (QGraphicsItem *it : created) {
        if (it->type() == ItemType::Device) {
            auto *d = static_cast<DeviceItem *>(it);
            d->dataRef().id = QString();
            d->dataRef().id = nextId(d->data().section);
            d->update();
        } else if (it->type() == ItemType::Strip) {
            auto *s = static_cast<StripItem *>(it);
            s->dataRef().id = QString();
            s->dataRef().id = nextId(s->data().section);
            s->update();
        } else if (it->type() == ItemType::Equipment) {
            auto *e = static_cast<EquipmentItem *>(it);
            const LibraryEntry *le = Library::instance().entry(e->data().key);
            e->dataRef().id = QString();
            e->dataRef().id = nextId(QStringLiteral("@") + (le && !le->idPrefix.isEmpty() ? le->idPrefix : QStringLiteral("Q")));
            e->update();
        }
        if (it->flags() & QGraphicsItem::ItemIsSelectable)
            it->setSelected(true);
    }
    updateLinks();
    commit(tr("Duplicate"));
}

void PlanScene::rotateSelection(double deg)
{
    bool any = false;
    for (QGraphicsItem *it : selectedItems()) {
        if (it->type() == ItemType::Device) {
            auto *d = static_cast<DeviceItem *>(it);
            double r = std::fmod(d->data().rotation + deg, 360.0);
            if (r < 0) r += 360;
            d->dataRef().rotation = r;
            d->update();
            any = true;
        }
    }
    if (any)
        commit(tr("Rotate"));
}

void PlanScene::setAllLights(bool on)
{
    for (DeviceItem *d : devices())
        if (d->hasEffect() && !d->isWifi() && d->isVisible())    // Wi-Fi is not a light
            d->setOn(on);
    for (StripItem *s : strips())
        if (s->isVisible())
            s->setOn(on);
    emit itemEdited();
    emit modified();
}

void PlanScene::applyCalibration(const QPointF &a, const QPointF &b, double realCm, bool scaleDrawing)
{
    const double d = QLineF(a, b).length();
    if (d < 1e-6 || realCm <= 0)
        return;
    const double f = realCm / d;
    auto scalePos = [&](const QPointF &p) { return a + (p - a) * f; };
    if (m_background) {
        m_background->setCmPerPixel(m_background->cmPerPixel() * f);
        m_background->setPos(scalePos(m_background->pos()));
    }
    if (scaleDrawing) {
        for (QGraphicsItem *it : contentItems()) {
            it->setPos(scalePos(it->pos()));
            if (it->type() == ItemType::Wall || it->type() == ItemType::Room || it->type() == ItemType::Strip) {
                auto *pi = static_cast<PolyItem *>(it);
                QPolygonF pts = pi->points();
                for (QPointF &p : pts)
                    p *= f;
                pi->setPoints(pts);
            }
        }
    }
    notifyWallsChanged();
    commit(tr("Set scale"));
    emit backgroundChanged();
}

// ------------------------------------------------------------------ undo

void PlanScene::commit(const QString &label)
{
    if (isRestoring())
        return;
    const QString now = SceneSerializer::snapshot(this);
    if (now == m_lastSnapshot)
        return;
    m_undo.push(new SnapshotCommand(this, m_lastSnapshot, now, label));
    m_lastSnapshot = now;
    emit modified();
}

void PlanScene::resetUndo()
{
    m_undo.clear();
    m_lastSnapshot = SceneSerializer::snapshot(this);
    m_undo.setClean();
}

void PlanScene::showContextMenu(const QPointF &scenePos, const QPoint &screenPos)
{
    if (m_tool != SelectTool && m_tool != RoomTool && m_tool != WallTool)
        return;
    if (!m_drawPts.isEmpty())
        return;
    // topmost item under the cursor that can be edited with the current tool
    QGraphicsItem *hit = nullptr;
    for (QGraphicsItem *it : items(scenePos, Qt::IntersectsItemShape, Qt::DescendingOrder, views().isEmpty() ? QTransform() : views().first()->viewportTransform())) {
        const int t = it->type();
        const int owner = t == ItemType::Handle ? static_cast<HandleItem *>(it)->owner()->type() : t;
        if ((t == ItemType::Handle || t == ItemType::Device || t == ItemType::Strip || t == ItemType::Wall || t == ItemType::Room
             || t == ItemType::Group || t == ItemType::Equipment || t == ItemType::Wire || t == ItemType::Rail)
            && isEditableType(owner)) {
            hit = it;
            break;
        }
        if (t == ItemType::Note && it->parentItem() && isEditableType(it->parentItem()->type())) {
            hit = it->parentItem();
            break;
        }
    }
    if (!hit) {
        return;
    }
    const double tol = 10.0 / std::max(1e-6, m_viewScale);
    QMenu menu;

    if (hit->type() == ItemType::Handle) {
        auto *h = static_cast<HandleItem *>(hit);
        PolyItem *owner = h->owner();
        const int index = h->index();
        QAction *del = menu.addAction(tr("Delete corner"), [this, owner, index] {
            if (owner->removePoint(index))
                commit(tr("Delete corner"));
        });
        del->setEnabled(owner->canRemovePoint());
        menu.exec(screenPos);
        return;
    }

    if (!hit->isSelected()) {
        clearSelection();
        hit->setSelected(true);
    }
    const int t = hit->type();
    if (t == ItemType::Room || t == ItemType::Wall || t == ItemType::Strip) {
        auto *poly = static_cast<PolyItem *>(hit);
        const bool lineStrip = t != ItemType::Strip || static_cast<StripItem *>(hit)->mode() == StripItem::Line;
        if (lineStrip) {
            const double extra = t == ItemType::Wall ? static_cast<WallItem *>(hit)->thickness() / 2 : 0;
            QAction *add = menu.addAction(tr("Add corner here"), [this, poly, scenePos, tol, extra] {
                if (poly->insertPointAt(scenePos, tol + extra))
                    commit(tr("Add corner"));
            });
            add->setEnabled(poly->edgeAt(scenePos, tol + extra) >= 0);
        }
        if (t == ItemType::Room) {
            auto *room = static_cast<RoomItem *>(hit);
            menu.addAction(tr("Rename..."), [this, room] {
                bool ok = false;
                const QString name = QInputDialog::getText(nullptr, tr("Rename room"), tr("Name:"), QLineEdit::Normal,
                                                           room->name(), &ok);
                if (ok) {
                    room->setName(name.trimmed());
                    commit(tr("Rename room"));
                    emit itemEdited();
                }
            });
        }
        if (t == ItemType::Strip) {
            auto *strip = static_cast<StripItem *>(hit);
            menu.addAction(strip->data().on ? tr("Switch off") : tr("Switch on"), [this, strip] {
                strip->setOn(!strip->data().on);
                notifyItemEdited(strip);
            });
        }
    } else if (t == ItemType::Device) {
        auto *d = static_cast<DeviceItem *>(hit);
        if (d->hasEffect() && !d->isWifi())
            menu.addAction(d->data().on ? tr("Switch off") : tr("Switch on"), [this, d] {
                d->setOn(!d->data().on);
                notifyItemEdited(d);
            });
        menu.addAction(tr("Rotate 90°"), [this] { rotateSelection(90); });
    } else if (t == ItemType::Wire) {
        auto *w = static_cast<WireItem *>(hit);
        menu.addAction(tr("Add a step here"), [this, w, scenePos] {
            w->insertJog(scenePos);
            commit(tr("Edit wire"));
        });
        QAction *a = menu.addAction(tr("Automatic route"), [this, w] {
            w->resetRoute();
            commit(tr("Automatic route"));
            emit itemEdited();
        });
        a->setEnabled(w->isManual());
    } else if (t == ItemType::Group) {
        auto *g = static_cast<GroupItem *>(hit);
        const int link = g->linkAt(scenePos);
        QAction *dl = menu.addAction(tr("Delete this link"), [this, g, link] {
            g->removeLink(link);
            if (g->links().isEmpty())
                delete g;
            commit(tr("Delete link"));
            emit itemEdited();
        });
        dl->setEnabled(link >= 0);
        menu.addAction(tr("Rename..."), [this, g] {
            bool ok = false;
            const QString name = QInputDialog::getText(nullptr, tr("Rename group"), tr("Name:"), QLineEdit::Normal, g->name(), &ok);
            if (ok) {
                g->setName(name.trimmed());
                commit(tr("Rename group"));
                emit itemEdited();
            }
        });
    }
    // groups of the elements
    if (t == ItemType::Device || t == ItemType::Strip) {
        menu.addSeparator();
        int elems = 0;
        for (QGraphicsItem *it : selectedItems())
            elems += (it->type() == ItemType::Device || it->type() == ItemType::Strip) ? 1 : 0;
        QAction *grp = menu.addAction(tr("Group the selected elements"), [this] { groupSelection(); });
        grp->setEnabled(elems >= 2);
        const QString uid = uidOf(hit);
        const QList<GroupItem *> mine = groupsOf(uid);
        QList<GroupItem *> others;
        for (GroupItem *g : groups())
            if (!mine.contains(g))
                others << g;
        if (!others.isEmpty()) {
            QMenu *addTo = menu.addMenu(tr("Add to group"));
            for (GroupItem *g : others) {
                QPixmap pm(12, 12);
                pm.fill(g->color());
                addTo->addAction(QIcon(pm), g->name(), [this, g] { addSelectionToGroup(g); });
            }
        }
        for (GroupItem *g : mine)
            menu.addAction(tr("Remove from group %1").arg(g->name()), [this, g, uid] { removeFromGroup(g, uid); });
    }
    menu.addSeparator();
    menu.addAction(tr("Duplicate"), [this] { duplicateSelection(); });
    menu.addAction(tr("Delete"), [this] { deleteSelection(); });
    menu.exec(screenPos);
}

void PlanScene::notifyItemEdited(QGraphicsItem *)
{
    emit itemEdited();
    emit modified();
}

// ------------------------------------------------------------------ tools

void PlanScene::setTool(Tool t)
{
    if (m_snapMarker)
        m_snapMarker->hide();
    if (isDrawingTool() || m_tool == CalibrateTool || m_tool == MeasureTool)
        cancelDrawing();
    clearMeasure();
    m_tool = t;
    m_editing = false;
    switch (t) {
    case WallTool:
        emit statusMessage(tr("Wall: click points, double-click / Enter to finish, click the first point to close, Shift = 45°, Alt = no snap, Backspace = undo point, Esc = cancel. Double-click a wall to edit it."));
        break;
    case RoomTool:
        emit statusMessage(tr("Room: click the corners – they snap to the wall faces (green square = corner, orange = on the wall). Click the first point or press Enter to close. Alt = no snap. Click inside a room to edit it."));
        break;
    case StripTool:
        emit statusMessage(tr("LED strip: click points, double-click / Enter to finish"));
        break;
    case StripCircleTool:
        emit statusMessage(tr("LED circle: click the center, then click the radius"));
        break;
    case CalibrateTool:
        emit statusMessage(tr("Scale: click two points with a known distance on the plan"));
        break;
    case MeasureTool:
        emit statusMessage(tr("Measure: click two points"));
        break;
    case RailTool:
        emit statusMessage(tr("DIN rail: click the left end, then the right end"));
        break;
    case WireTool:
        emit statusMessage(tr("Wire: click a terminal, then the terminal it goes to (or drag between them). Esc = cancel"));
        break;
    case GroupTool:
        emit statusMessage(tr("Group: click an element, then the element to link it to. Keep clicking to continue the chain; "
                              "click an element already linked to go on from it (star). Shift+click = start a new group. Esc = stop"));
        break;
    default:
        emit statusMessage(QString());
        break;
    }
    clearSelection();
    for (QGraphicsItem *it : contentItems())
        applyEditability(it);
    emit toolChanged(t);
}

bool PlanScene::isDrawingTool() const
{
    return m_tool == WallTool || m_tool == RoomTool || m_tool == StripTool || m_tool == StripCircleTool
           || m_tool == CalibrateTool || m_tool == MeasureTool || m_tool == RailTool || m_tool == WireTool
           || m_tool == GroupTool;
}

void PlanScene::cancelDrawing()
{
    hideGuides();
    m_drawPts.clear();
    m_wireFromUid.clear();
    m_wireFromPort = -1;
    m_linkFrom.clear();
    m_linkGroup = nullptr;
    delete m_preview;
    m_preview = nullptr;
}

void PlanScene::clearMeasure()
{
    delete m_measureLine;
    m_measureLine = nullptr;
    delete m_measureText;
    m_measureText = nullptr;
}

void PlanScene::finishDrawing()
{
    QPolygonF pts = m_drawPts;
    // remove consecutive duplicates
    for (int i = pts.size() - 1; i > 0; --i)
        if (QLineF(pts[i], pts[i - 1]).length() < 0.5)
            pts.remove(i);
    const Tool t = m_tool;
    cancelDrawing();

    if (t == WallTool && pts.size() >= 2) {
        bool closed = false;
        if (pts.size() > 3 && QLineF(pts.first(), pts.last()).length() < 0.5) {
            pts.removeLast();
            closed = true;
        }
        auto *w = new WallItem;
        w->setThickness(m_settings.wallThickness);
        w->setPos(pts.first());
        w->setPoints(pts.translated(-pts.first()));
        w->setClosed(closed);
        addItem(w);
        notifyWallsChanged();
        commit(tr("Add wall"));
    } else if (t == RoomTool && pts.size() >= 3) {
        if (QLineF(pts.first(), pts.last()).length() < 0.5)
            pts.removeLast();
        if (pts.size() >= 3) {
            auto *r = new RoomItem;
            r->setName(tr("Room %1").arg(rooms().size() + 1));
            r->setPos(pts.first());
            r->setPoints(pts.translated(-pts.first()));
            addItem(r);
            clearSelection();
            r->setSelected(true);
            commit(tr("Add room"));
        }
    } else if (t == StripTool && pts.size() >= 2) {
        if (const LibraryEntry *e = Library::instance().entry(m_stripKey))
            addStripLine(*e, pts);
    }
}

QVector<QPointF> PlanScene::alignRefs(const QGraphicsItem *ignore, bool devices, bool outlines) const
{
    QVector<QPointF> refs;
    if (devices)
        for (DeviceItem *d : this->devices())
            if (d != ignore && d->isVisible() && !d->isSelected())
                refs << d->scenePos();
    if (outlines) {
        for (QGraphicsItem *it : items()) {
            const int t = it->type();
            if (it == ignore || !it->isVisible() || (t != ItemType::Wall && t != ItemType::Room && t != ItemType::Strip))
                continue;
            if (t == ItemType::Strip && static_cast<StripItem *>(it)->mode() != StripItem::Line)
                continue;
            for (const QPointF &q : static_cast<PolyItem *>(it)->scenePoints())
                refs << q;
        }
    }
    return refs;
}

QPointF PlanScene::alignToAxes(const QPointF &p, const QVector<QPointF> &refs) const
{
    if (!m_snap || (QApplication::keyboardModifiers() & Qt::AltModifier)) {
        hideGuides();
        return p;
    }
    const double tol = 8.0 / std::max(1e-6, m_viewScale);
    double bestX = tol, bestY = tol;
    int ix = -1, iy = -1;
    for (int i = 0; i < refs.size(); ++i) {
        const QPointF &r = refs[i];
        if (QLineF(r, p).length() < 0.5)
            continue;
        const double dx = std::abs(r.x() - p.x());
        const double dy = std::abs(r.y() - p.y());
        // prefer the nearest reference on that axis when several are lined up
        if (dx < bestX || (ix >= 0 && std::abs(dx - bestX) < 1e-6 && std::abs(r.y() - p.y()) < std::abs(refs[ix].y() - p.y()))) {
            bestX = dx;
            ix = i;
        }
        if (dy < bestY || (iy >= 0 && std::abs(dy - bestY) < 1e-6 && std::abs(r.x() - p.x()) < std::abs(refs[iy].x() - p.x()))) {
            bestY = dy;
            iy = i;
        }
    }
    QPointF q = p;
    if (ix >= 0)
        q.setX(refs[ix].x());
    if (iy >= 0)
        q.setY(refs[iy].y());
    if (ix < 0 && iy < 0) {
        hideGuides();
        return p;
    }
    auto *self = const_cast<PlanScene *>(this);
    if (!m_guides) {
        m_guides = new QGraphicsPathItem;
        m_guides->setZValue(ZOrder::Preview + 2);
        m_guides->setAcceptedMouseButtons(Qt::NoButton);
        QPen pen(QColor(230, 0, 140), 0, Qt::DashLine);
        pen.setCosmetic(true);
        m_guides->setPen(pen);
        self->addItem(m_guides);
    }
    QPainterPath path;
    const double ext = 30.0 / std::max(1e-6, m_viewScale);
    if (ix >= 0) {
        const QPointF r = refs[ix];
        path.moveTo(q.x(), std::min(r.y(), q.y()) - ext);
        path.lineTo(q.x(), std::max(r.y(), q.y()) + ext);
    }
    if (iy >= 0) {
        const QPointF r = refs[iy];
        path.moveTo(std::min(r.x(), q.x()) - ext, q.y());
        path.lineTo(std::max(r.x(), q.x()) + ext, q.y());
    }
    m_guides->setPath(path);
    m_guides->show();
    return q;
}

void PlanScene::hideGuides() const
{
    if (m_guides)
        m_guides->hide();
}

QPointF PlanScene::alignDevice(const QGraphicsItem *moving, const QPointF &pos) const
{
    // only when one element is dragged (a group keeps its own layout)
    int sel = 0;
    for (QGraphicsItem *it : selectedItems())
        sel += (it->type() == ItemType::Device || it->type() == ItemType::Strip) ? 1 : 0;
    if (sel > 1 || isRestoring())
        return snapToGrid(pos);
    const QPointF aligned = alignToAxes(pos, alignRefs(moving, true, false));
    if (aligned == pos)
        return snapToGrid(pos);
    // keep the aligned axis, grid on the other one
    const QPointF g = snapToGrid(pos);
    return QPointF(aligned.x() != pos.x() ? aligned.x() : g.x(), aligned.y() != pos.y() ? aligned.y() : g.y());
}

QPointF PlanScene::toolPoint(const QPointF &raw, Qt::KeyboardModifiers mods) const
{
    QPointF p;
    if (m_tool == CalibrateTool || m_tool == WireTool) {
        p = raw;                               // exact clicks on the image, no snapping at all
        m_lastSnap = NoSnap;
    } else if (m_tool == RailTool) {
        p = QPointF(std::round(raw.x() / 5.0) * 5.0, std::round(raw.y() / 5.0) * 5.0);
        if (!m_drawPts.isEmpty())
            p.setY(m_drawPts.first().y());     // rails are horizontal
        m_lastSnap = NoSnap;
    } else if (m_tool == MeasureTool) {
        p = snapPoint(raw, nullptr, false, true);
    } else if (m_tool == RoomTool || m_tool == StripTool || m_tool == StripCircleTool) {
        p = snapPoint(raw, nullptr, true, true);   // wall faces: the room sits exactly on the inner face
    } else {
        p = snapPoint(raw);                        // walls: centre-line vertices, to join walls
    }
    // nothing exact under the cursor: line up horizontally / vertically with the previous points,
    // the corners of walls / rooms and (LED strips) the lights
    const bool drawing = m_tool == WallTool || m_tool == RoomTool || m_tool == StripTool || m_tool == StripCircleTool;
    if (drawing && (m_lastSnap == NoSnap || m_lastSnap == GridSnap)) {
        QVector<QPointF> refs(m_drawPts.begin(), m_drawPts.end());
        refs += alignRefs(nullptr, m_tool == StripTool || m_tool == StripCircleTool, true);
        p = alignToAxes(m_lastSnap == GridSnap ? raw : p, refs);
    } else {
        hideGuides();
    }
    if ((mods & Qt::ShiftModifier) && !m_drawPts.isEmpty()) {
        p = Geo::constrainAngle(m_drawPts.last(), raw);
        hideGuides();
    }
    return p;
}

void PlanScene::updatePreview(const QPointF &cursor)
{
    if (m_drawPts.isEmpty()) {
        if (m_preview)
            m_preview->setPath(QPainterPath());
        return;
    }
    if (!m_preview) {
        m_preview = new QGraphicsPathItem;
        m_preview->setZValue(ZOrder::Preview);
        addItem(m_preview);
    }
    QPainterPath path;
    QPen pen(QColor(0, 120, 215), 0, Qt::DashLine);
    if (m_tool == RailTool) {
        const QPointF a = m_drawPts.first();
        path.addRect(QRectF(QPointF(std::min(a.x(), cursor.x()), a.y() - RailItem::kHeight / 2),
                            QSizeF(std::abs(cursor.x() - a.x()), RailItem::kHeight)));
        emit statusMessage(tr("Rail length: %1 mm").arg(int(std::abs(cursor.x() - a.x()))));
    } else if (m_tool == StripCircleTool || m_tool == CalibrateTool || m_tool == MeasureTool) {
        const QPointF c = m_drawPts.first();
        if (m_tool == StripCircleTool) {
            const double r = QLineF(c, cursor).length();
            path.addEllipse(c, r, r);
        }
        path.moveTo(c);
        path.lineTo(cursor);
        pen.setColor(m_tool == CalibrateTool ? QColor(220, 90, 0) : QColor(0, 120, 215));
        emit statusMessage(tr("Distance: %1").arg(Geo::formatMeters(QLineF(c, cursor).length())));
    } else {
        path.moveTo(m_drawPts.first());
        for (int i = 1; i < m_drawPts.size(); ++i)
            path.lineTo(m_drawPts[i]);
        path.lineTo(cursor);
        if (m_tool == RoomTool && m_drawPts.size() >= 2)
            path.lineTo(m_drawPts.first());
        const double seg = QLineF(m_drawPts.last(), cursor).length();
        QString msg = tr("Segment: %1").arg(Geo::formatMeters(seg));
        if (m_tool == RoomTool && m_drawPts.size() >= 2) {
            QPolygonF poly = m_drawPts;
            poly << cursor;
            msg += "   " + tr("Area: %1").arg(Geo::formatArea(Geo::polygonArea(poly)));
        }
        emit statusMessage(msg);
        if (m_tool == WallTool) {
            pen = QPen(QColor(70, 70, 70, 150), m_settings.wallThickness, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        } else if (m_tool == StripTool) {
            pen = QPen(QColor(255, 180, 0), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        }
    }
    m_preview->setPen(pen);
    m_preview->setPath(path);
}

void PlanScene::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
    m_lastMouse = e->scenePos();
    if (m_tool == SelectTool || e->button() == Qt::MiddleButton) {
        QGraphicsScene::mousePressEvent(e);
        return;
    }
    if (e->button() == Qt::RightButton) {
        if (m_tool == WallTool || m_tool == RoomTool || m_tool == StripTool)
            finishDrawing();
        else
            cancelDrawing();
        e->accept();
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if ((m_tool == RoomTool || m_tool == WallTool) && m_drawPts.isEmpty() && editPress(e))
        return;

    const QPointF p = toolPoint(e->scenePos(), e->modifiers());
    switch (m_tool) {
    case WallTool:
    case RoomTool:
    case StripTool:
        if (m_drawPts.size() >= 2 && (m_tool != StripTool)
            && QLineF(p, m_drawPts.first()).length() < 12.0 / m_viewScale) {
            m_drawPts << m_drawPts.first();        // close the shape
            finishDrawing();
            break;
        }
        m_drawPts << p;
        updatePreview(p);
        break;
    case StripCircleTool:
        if (m_drawPts.isEmpty()) {
            m_drawPts << p;
            updatePreview(p);
        } else {
            const QPointF c = m_drawPts.first();
            const double r = QLineF(c, p).length();
            cancelDrawing();
            if (r > 1)
                if (const LibraryEntry *le = Library::instance().entry(m_stripKey))
                    addStripCircle(*le, c, r);
        }
        break;
    case CalibrateTool:
        if (m_drawPts.isEmpty()) {
            m_drawPts << p;
            updatePreview(p);
        } else {
            const QPointF a = m_drawPts.first();
            cancelDrawing();
            emit calibrationPicked(a, p);
        }
        break;
    case RailTool:
        if (m_drawPts.isEmpty()) {
            m_drawPts << p;
            updatePreview(p);
        } else {
            const QPointF a = m_drawPts.first();
            cancelDrawing();
            const double len = std::abs(p.x() - a.x());
            if (len >= 20)
                addRail(QPointF(std::min(a.x(), p.x()), a.y() - RailItem::kHeight / 2), len);
        }
        break;
    case WireTool:
        wirePress(e->scenePos());
        break;
    case GroupTool:
        linkPress(e->scenePos(), e->modifiers());
        break;
    case MeasureTool:
        if (m_drawPts.isEmpty()) {
            clearMeasure();
            m_drawPts << p;
            updatePreview(p);
        } else {
            const QPointF a = m_drawPts.first();
            cancelDrawing();
            m_measureLine = new QGraphicsPathItem;
            QPainterPath path(a);
            path.lineTo(p);
            m_measureLine->setPath(path);
            m_measureLine->setPen(QPen(QColor(200, 0, 120), 0, Qt::DashLine));
            m_measureLine->setZValue(ZOrder::Preview);
            addItem(m_measureLine);
            m_measureText = new QGraphicsSimpleTextItem(Geo::formatMeters(QLineF(a, p).length()));
            m_measureText->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            m_measureText->setBrush(QColor(200, 0, 120));
            QFont f = m_measureText->font();
            f.setBold(true);
            f.setPointSize(11);
            m_measureText->setFont(f);
            m_measureText->setPos((a + p) / 2);
            m_measureText->setZValue(ZOrder::Preview);
            addItem(m_measureText);
            emit statusMessage(tr("Measured: %1").arg(Geo::formatMeters(QLineF(a, p).length())));
        }
        break;
    default:
        break;
    }
    e->accept();
}

void PlanScene::mouseMoveEvent(QGraphicsSceneMouseEvent *e)
{
    m_lastMouse = e->scenePos();
    emit cursorMoved(e->scenePos());
    if (m_editing || mouseGrabberItem()) {
        QGraphicsScene::mouseMoveEvent(e);
        return;
    }
    if (m_tool == WireTool) {
        showWirePreview(e->scenePos());
        e->accept();
        return;
    }
    if (m_tool == GroupTool) {
        showLinkPreview(e->scenePos());
        e->accept();
        return;
    }
    if (m_tool != SelectTool) {
        const QPointF p = toolPoint(e->scenePos(), e->modifiers());
        showSnapMarker(p, m_lastSnap);           // shows where the click will land
        if (!m_drawPts.isEmpty())
            updatePreview(p);
        e->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(e);
}

void PlanScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *e)
{
    hideGuides();
    if (m_editing) {
        m_editing = false;
        QGraphicsScene::mouseReleaseEvent(e);
        return;
    }
    if (m_tool == WireTool && e->button() == Qt::LeftButton && !m_wireFromUid.isEmpty()
        && QLineF(e->scenePos(), m_wirePressPos).length() * m_viewScale > 6) {
        wirePress(e->scenePos());      // dragged from one terminal to another
        e->accept();
        return;
    }
    if (m_tool != SelectTool && e->button() != Qt::MiddleButton) {
        e->accept();
        return;
    }
    QGraphicsItem *grabber = mouseGrabberItem();
    const bool wireGrip = grabber && grabber->type() == ItemType::WireHandle;   // commits by itself
    QGraphicsScene::mouseReleaseEvent(e);
    if (m_tool == SelectTool && e->button() == Qt::LeftButton && !wireGrip)
        commit(tr("Move"));
}

void PlanScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e)
{
    if (m_tool == GroupTool || m_tool == WireTool) {
        // the second click of a double click is handled as a normal click (no light toggling)
        if (m_tool == GroupTool)
            linkPress(e->scenePos(), e->modifiers());
        e->accept();
        return;
    }
    if ((m_tool == WallTool || m_tool == RoomTool) && m_drawPts.size() <= 1 && e->button() == Qt::LeftButton) {
        // editing an existing outline: corner handle -> delete it, edge -> add a corner,
        // otherwise select the wall / room under the cursor
        const int want = m_tool == RoomTool ? int(ItemType::Room) : int(ItemType::Wall);
        cancelDrawing();
        const QTransform vt = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
        for (QGraphicsItem *it : items(e->scenePos(), Qt::IntersectsItemShape, Qt::DescendingOrder, vt))
            if (it->type() == ItemType::Handle && static_cast<HandleItem *>(it)->owner()->type() == want) {
                QGraphicsScene::mouseDoubleClickEvent(e);
                return;
            }
        const double tol = 10.0 / std::max(1e-6, m_viewScale);
        for (QGraphicsItem *it : selectedItems()) {
            if (it->type() != want)
                continue;
            const double extra = want == ItemType::Wall ? static_cast<WallItem *>(it)->thickness() / 2 : 0;
            if (static_cast<PolyItem *>(it)->insertPointAt(e->scenePos(), tol + extra)) {
                commit(tr("Add corner"));
                e->accept();
                return;
            }
        }
        if (QGraphicsItem *it = outlineAt(e->scenePos(), want)) {
            clearSelection();
            it->setSelected(true);
        }
        e->accept();
        return;
    }
    if (m_tool == WallTool || m_tool == RoomTool || m_tool == StripTool) {
        // the press of the double click already added a point at the same place
        finishDrawing();
        e->accept();
        return;
    }
    QGraphicsScene::mouseDoubleClickEvent(e);
}

void PlanScene::keyPressEvent(QKeyEvent *e)
{
    if (m_tool != SelectTool) {
        switch (e->key()) {
        case Qt::Key_Escape:
            if (m_drawPts.isEmpty() && !selectedItems().isEmpty())
                clearSelection();
            else if (m_drawPts.isEmpty())
                setTool(SelectTool);
            else
                cancelDrawing();
            e->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_tool == RoomTool && m_drawPts.size() >= 3)
                m_drawPts << m_drawPts.first();
            finishDrawing();
            e->accept();
            return;
        case Qt::Key_Backspace:
            if (!m_drawPts.isEmpty()) {
                m_drawPts.removeLast();
                updatePreview(m_lastMouse);
            }
            e->accept();
            return;
        default:
            break;
        }
    } else if (e->key() == Qt::Key_Escape) {
        clearSelection();
        clearMeasure();
    }
    QGraphicsScene::keyPressEvent(e);
}

void PlanScene::drawBackground(QPainter *p, const QRectF &rect)
{
    p->fillRect(rect, Qt::white);
    if (!m_showGrid || m_exporting)
        return;
    auto drawGrid = [&](double step, const QColor &c) {
        if (step * m_viewScale < 6)
            return;
        p->setPen(QPen(c, 0));
        const double left = std::floor(rect.left() / step) * step;
        const double top = std::floor(rect.top() / step) * step;
        QVector<QLineF> lines;
        for (double x = left; x < rect.right(); x += step)
            lines << QLineF(x, rect.top(), x, rect.bottom());
        for (double y = top; y < rect.bottom(); y += step)
            lines << QLineF(rect.left(), y, rect.right(), y);
        p->drawLines(lines);
    };
    drawGrid(50, QColor(242, 242, 242));
    drawGrid(100, QColor(225, 225, 225));
    drawGrid(500, QColor(205, 205, 205));
}

void PlanScene::dragEnterEvent(QGraphicsSceneDragDropEvent *e)
{
    if (e->mimeData()->hasFormat(kEntryMime))
        e->acceptProposedAction();
    else
        QGraphicsScene::dragEnterEvent(e);
}

void PlanScene::dragMoveEvent(QGraphicsSceneDragDropEvent *e)
{
    if (e->mimeData()->hasFormat(kEntryMime))
        e->acceptProposedAction();
    else
        QGraphicsScene::dragMoveEvent(e);
}

void PlanScene::dropEvent(QGraphicsSceneDragDropEvent *e)
{
    if (!e->mimeData()->hasFormat(kEntryMime)) {
        QGraphicsScene::dropEvent(e);
        return;
    }
    const QString key = QString::fromUtf8(e->mimeData()->data(kEntryMime));
    const LibraryEntry *entry = Library::instance().entry(key);
    if (!entry)
        return;
    placeEntry(*entry, e->scenePos());
    e->acceptProposedAction();
}

// ------------------------------------------------------------------ page kind / placing

void PlanScene::setPageKind(PageKind k)
{
    m_kind = k;
    update();
}

void PlanScene::placeEntry(const LibraryEntry &e, const QPointF &scenePos)
{
    if (isPanel()) {
        if (e.rail)
            addRail(QPointF(std::round(scenePos.x() / 5.0) * 5.0, std::round(scenePos.y() / 5.0) * 5.0),
                    e.width > 0 ? e.width : 450);
        else
            addEquipment(e, scenePos);
        return;
    }
    if (e.kind == DeviceKind::Strip) {
        QPolygonF pts;
        const QPointF p = snapToGrid(scenePos);
        pts << p << p + QPointF(100, 0);
        addStripLine(e, pts);
    } else {
        addDevice(e, scenePos);
    }
}

QString PlanScene::uidOf(const QGraphicsItem *it)
{
    if (!it)
        return {};
    switch (it->type()) {
    case ItemType::Device: return static_cast<const DeviceItem *>(it)->data().uid;
    case ItemType::Strip: return static_cast<const StripItem *>(it)->data().uid;
    case ItemType::Equipment: return static_cast<const EquipmentItem *>(it)->data().uid;
    default: return {};
    }
}

QGraphicsItem *PlanScene::itemByUid(const QString &uid) const
{
    if (uid.isEmpty())
        return nullptr;
    for (QGraphicsItem *it : items()) {
        const int t = it->type();
        if ((t == ItemType::Device || t == ItemType::Strip || t == ItemType::Equipment) && uidOf(it) == uid)
            return it;
    }
    return nullptr;
}

// ------------------------------------------------------------------ groups

QList<GroupItem *> PlanScene::groupsOf(const QString &uid) const
{
    QList<GroupItem *> out;
    for (GroupItem *g : groups())
        if (g->contains(uid))
            out << g;
    return out;
}

QString PlanScene::nextGroupName() const
{
    int maxN = 0;
    static const QRegularExpression re(QStringLiteral("^G(\\d+)$"));
    for (GroupItem *g : groups()) {
        const auto m = re.match(g->name());
        if (m.hasMatch())
            maxN = std::max(maxN, m.captured(1).toInt());
    }
    return QStringLiteral("G%1").arg(maxN + 1);
}

GroupItem *PlanScene::groupSelection()
{
    QStringList uids;
    for (QGraphicsItem *it : selectedItems()) {
        const QString uid = uidOf(it);
        if (!uid.isEmpty() && it->type() != ItemType::Equipment)
            uids << uid;
    }
    if (uids.size() < 2) {
        emit statusMessage(tr("Select at least two elements to group them"));
        return nullptr;
    }
    // keep the order in which they appear on the plan: top-left first
    std::sort(uids.begin(), uids.end(), [this](const QString &a, const QString &b) {
        const QPointF pa = GroupItem::anchorOf(itemByUid(a)), pb = GroupItem::anchorOf(itemByUid(b));
        return pa.y() + pa.x() * 0.5 < pb.y() + pb.x() * 0.5;
    });
    auto *g = new GroupItem;
    g->setName(nextGroupName());
    g->setColor(GroupItem::paletteColor(groups().size()));
    addItem(g);
    g->setMembers(uids);
    applyEditability(g);
    commit(tr("Group"));
    emit itemEdited();
    return g;
}

void PlanScene::addSelectionToGroup(GroupItem *g)
{
    if (!g)
        return;
    for (QGraphicsItem *it : selectedItems()) {
        const QString uid = uidOf(it);
        if (!uid.isEmpty() && it->type() != ItemType::Equipment)
            g->addMember(uid);
    }
    commit(tr("Add to group"));
    emit itemEdited();
}

void PlanScene::removeFromGroup(GroupItem *g, const QString &uid)
{
    if (!g)
        return;
    g->removeMember(uid);
    if (g->members().size() < 2)
        delete g;
    commit(tr("Remove from group"));
    emit itemEdited();
}

void PlanScene::setShowGroups(bool on)
{
    m_showGroups = on;
    for (GroupItem *g : groups())
        g->setVisible(on);
}

// ------------------------------------------------------------------ links (groups, wires)

void PlanScene::scheduleLinkUpdate()
{
    if (m_linkPending || isRestoring())
        return;
    m_linkPending = true;
    QTimer::singleShot(0, this, [this] {
        m_linkPending = false;
        updateLinks();
    });
}

void PlanScene::updateLinks()
{
    for (GroupItem *g : groups()) {
        g->setVisible(m_showGroups);
        g->rebuild();
    }
    for (WireItem *w : wires())
        w->rebuild();
    for (EquipmentItem *e : equipment())
        e->update();         // terminal colors
}

void PlanScene::updateHighlight()
{
    // floor plans: groups of the selected elements are drawn thicker
    for (GroupItem *g : groups())
        g->update();
    if (!isPanel())
        return;
    QSet<QString> sel;
    QList<WireItem *> selWires;
    for (QGraphicsItem *it : selectedItems()) {
        if (it->type() == ItemType::Equipment)
            sel.insert(static_cast<EquipmentItem *>(it)->data().uid);
        else if (it->type() == ItemType::Wire)
            selWires << static_cast<WireItem *>(it);
    }
    const QList<WireItem *> all = wires();
    const QList<EquipmentItem *> eqs = equipment();
    if (sel.isEmpty() && selWires.isEmpty()) {
        for (WireItem *w : all)
            w->setLinkState(LinkState::Normal);
        for (EquipmentItem *e : eqs)
            e->setLinkState(LinkState::Normal);
        return;
    }
    QSet<QString> linked;
    for (WireItem *w : all) {
        const bool hot = selWires.contains(w) || sel.contains(w->fromUid()) || sel.contains(w->toUid());
        w->setLinkState(hot ? LinkState::Selected : LinkState::Dim);
        if (hot) {
            linked.insert(w->fromUid());
            linked.insert(w->toUid());
        }
    }
    for (EquipmentItem *e : eqs) {
        const QString uid = e->data().uid;
        if (sel.contains(uid))
            e->setLinkState(LinkState::Selected);
        else if (linked.contains(uid))
            e->setLinkState(LinkState::Linked);
        else
            e->setLinkState(LinkState::Dim);
    }
}

// ------------------------------------------------------------------ electrical panel

QList<WireItem *> PlanScene::wiresOf(const QString &uid) const
{
    QList<WireItem *> out;
    for (WireItem *w : wires())
        if (w->touches(uid))
            out << w;
    return out;
}

RailItem *PlanScene::addRail(const QPointF &topLeft, double length)
{
    auto *r = new RailItem(length);
    r->setPos(topLeft);
    addItem(r);
    applyEditability(r);
    clearSelection();
    r->setSelected(true);
    commit(tr("Add DIN rail"));
    return r;
}

EquipmentItem *PlanScene::addEquipment(const LibraryEntry &e, const QPointF &scenePos)
{
    DeviceData d = DeviceData::fromEntry(e);
    d.id = nextId(QStringLiteral("@") + (e.idPrefix.isEmpty() ? QStringLiteral("Q") : e.idPrefix));
    const QSizeF size(e.width > 0 ? e.width : 18, e.height > 0 ? e.height : 86);
    auto *item = new EquipmentItem(d, size, e.ports, e.portNames);
    addItem(item);
    item->setPos(scenePos - QPointF(size.width() / 2, size.height() / 2));     // snaps onto a rail
    applyEditability(item);
    clearSelection();
    item->setSelected(true);
    commit(tr("Add %1").arg(e.displayName()));
    return item;
}

WireItem *PlanScene::addWire(EquipmentItem *a, int portA, EquipmentItem *b, int portB)
{
    if (!a || !b || (a == b && portA == portB))
        return nullptr;
    auto *w = new WireItem;
    w->setColor(m_wireColor);
    w->setCrossSection(m_wireSection);
    addItem(w);
    w->setEnds(a->data().uid, portA, b->data().uid, portB);
    applyEditability(w);
    for (EquipmentItem *e : {a, b})
        e->update();
    commit(tr("Add wire"));
    emit statusMessage(tr("Wire %1 → %2").arg(w->endText(true), w->endText(false)));
    return w;
}

QPointF PlanScene::snapEquipment(EquipmentItem *e, const QPointF &pos) const
{
    const QSizeF sz = e->size();
    const double cy = pos.y() + sz.height() / 2;
    const double cx = pos.x() + sz.width() / 2;
    for (RailItem *r : rails()) {
        const QPointF rp = r->scenePos();
        if (std::abs(cy - r->centerY()) < 30 && cx >= rp.x() - 10 && cx <= rp.x() + r->length() + 10) {
            // on the rail: centred vertically, steps of half a module (9 mm) from the rail start
            const double x = rp.x() + std::round((pos.x() - rp.x()) / 9.0) * 9.0;
            return QPointF(x, r->centerY() - sz.height() / 2);
        }
    }
    return QPointF(std::round(pos.x()), std::round(pos.y()));
}

EquipmentItem *PlanScene::portUnder(const QPointF &scenePos, int *port) const
{
    const double tol = std::max(3.5, 9.0 / std::max(1e-6, m_viewScale));
    EquipmentItem *best = nullptr;
    double bestD = tol;
    for (EquipmentItem *e : equipment()) {
        if (!e->isVisible())
            continue;
        const int i = e->portAt(scenePos, tol);
        if (i < 0)
            continue;
        const double d = QLineF(scenePos, e->portScenePos(i)).length();
        if (d <= bestD) {
            bestD = d;
            best = e;
            if (port)
                *port = i;
        }
    }
    return best;
}

void PlanScene::wirePress(const QPointF &scenePos)
{
    int port = -1;
    EquipmentItem *e = portUnder(scenePos, &port);
    if (m_wireFromUid.isEmpty()) {
        if (!e) {
            emit statusMessage(tr("Click on a terminal (the small circles of the equipment)"));
            return;
        }
        m_wireFromUid = e->data().uid;
        m_wireFromPort = port;
        m_wirePressPos = scenePos;
        m_drawPts = {e->portScenePos(port)};
        showWirePreview(scenePos);
        return;
    }
    if (!e) {
        cancelDrawing();
        updatePreview(scenePos);
        return;
    }
    auto *from = static_cast<EquipmentItem *>(itemByUid(m_wireFromUid));
    const int fromPort = m_wireFromPort;
    if (e == from && port == fromPort)
        return;                 // same terminal (the release of the first click)
    cancelDrawing();
    updatePreview(scenePos);
    if (m_snapMarker)
        m_snapMarker->hide();
    addWire(from, fromPort, e, port);
}

void PlanScene::showWirePreview(const QPointF &cursor)
{
    int port = -1;
    EquipmentItem *e = portUnder(cursor, &port);
    if (e) {
        // ring on the terminal under the cursor (ignores the snap state of the select tool)
        showSnapMarker(e->portScenePos(port), CornerSnap);
        emit statusMessage(QStringLiteral("%1 · %2  %3").arg(e->data().id, e->portName(port), e->data().displayName()));
    } else if (m_snapMarker) {
        m_snapMarker->hide();
    }
    if (m_wireFromUid.isEmpty())
        return;
    if (!m_preview) {
        m_preview = new QGraphicsPathItem;
        m_preview->setZValue(ZOrder::Preview);
        addItem(m_preview);
    }
    const QPointF a = m_drawPts.value(0);
    const QPointF b = e ? e->portScenePos(port) : cursor;
    QPainterPath path(a);
    path.lineTo(b);
    m_preview->setPen(QPen(m_wireColor, 1.6, Qt::DashLine, Qt::RoundCap));
    m_preview->setPath(path);
}

// ------------------------------------------------------------------ group tool (links two by two)

QGraphicsItem *PlanScene::linkableAt(const QPointF &scenePos) const
{
    const QTransform vt = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
    for (QGraphicsItem *it : items(scenePos, Qt::IntersectsItemShape, Qt::DescendingOrder, vt)) {
        QGraphicsItem *t = it;
        if (t->type() == ItemType::Note && t->parentItem())
            t = t->parentItem();
        if ((t->type() == ItemType::Device || t->type() == ItemType::Strip) && t->isVisible())
            return t;
    }
    // close to a symbol is enough
    const double tol = 10.0 / std::max(1e-6, m_viewScale);
    QGraphicsItem *best = nullptr;
    double bestD = 1e18;
    for (DeviceItem *d : devices()) {
        if (!d->isVisible())
            continue;
        const double dist = QLineF(d->scenePos(), scenePos).length() - d->symbolSize() / 2;
        if (dist < tol && dist < bestD) {
            bestD = dist;
            best = d;
        }
    }
    return best;
}

GroupItem *PlanScene::linkGroup() const
{
    return m_linkGroup && groups().contains(m_linkGroup) ? m_linkGroup : nullptr;
}

void PlanScene::linkPress(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    QGraphicsItem *it = linkableAt(scenePos);
    if (!it) {
        // click on an empty place: the chain ends
        cancelDrawing();
        updatePreview(scenePos);
        emit statusMessage(tr("Group: click an element to start"));
        return;
    }
    const QString uid = uidOf(it);
    GroupItem *g = linkGroup();
    if (m_linkFrom.isEmpty() || (mods & Qt::ShiftModifier)) {
        // first element: continue its group (if it has exactly one), Shift = a new group
        m_linkFrom = uid;
        m_drawPts = {GroupItem::anchorOf(it)};
        const QList<GroupItem *> gs = groupsOf(uid);
        m_linkGroup = (mods & Qt::ShiftModifier) || gs.isEmpty() ? nullptr : (g && gs.contains(g) ? g : gs.first());
        showLinkPreview(scenePos);
        emit statusMessage(m_linkGroup ? tr("Group %1: click the element to link to").arg(m_linkGroup->name())
                                       : tr("New group: click the element to link to"));
        return;
    }
    if (uid == m_linkFrom)
        return;
    if (!g) {
        // the target's group, or a new one
        const QList<GroupItem *> gs = groupsOf(uid);
        if (!gs.isEmpty()) {
            g = gs.first();
        } else {
            g = new GroupItem;
            g->setName(nextGroupName());
            g->setColor(GroupItem::paletteColor(groups().size()));
            addItem(g);
            g->setVisible(m_showGroups);
            applyEditability(g);
        }
        m_linkGroup = g;
    }
    if (g->hasLink(m_linkFrom, uid)) {
        // already linked: go on from that element (star)
        m_linkFrom = uid;
        m_drawPts = {GroupItem::anchorOf(it)};
        showLinkPreview(scenePos);
        return;
    }
    g->addLink(m_linkFrom, uid);
    commit(tr("Link"));
    emit itemEdited();
    m_linkFrom = uid;                   // the chain continues from here
    m_linkGroup = g;
    m_drawPts = {GroupItem::anchorOf(it)};
    showLinkPreview(scenePos);
    emit statusMessage(tr("Group %1: %n link(s). Click the next element, Esc = stop", nullptr, int(g->links().size())).arg(g->name()));
}

void PlanScene::showLinkPreview(const QPointF &cursor)
{
    QGraphicsItem *hover = linkableAt(cursor);
    if (hover)
        showSnapMarker(GroupItem::anchorOf(hover), CornerSnap);
    else if (m_snapMarker)
        m_snapMarker->hide();
    QGraphicsItem *from = m_linkFrom.isEmpty() ? nullptr : itemByUid(m_linkFrom);
    if (!from) {
        if (m_preview)
            m_preview->setPath(QPainterPath());
        return;
    }
    if (!m_preview) {
        m_preview = new QGraphicsPathItem;
        m_preview->setZValue(ZOrder::Preview);
        addItem(m_preview);
    }
    const QPointF a = GroupItem::anchorOf(from);
    const QPointF b = hover ? GroupItem::anchorOf(hover) : cursor;
    QPainterPath path(a);
    const double len = QLineF(a, b).length();
    if (len > 1) {
        const QPointF dir = (b - a) / len;
        path.quadTo((a + b) / 2 + QPointF(dir.y(), -dir.x()) * (len * 0.28), b);
    }
    GroupItem *g = linkGroup();
    const QColor c = g ? g->color() : GroupItem::paletteColor(groups().size());
    QPen pen(c, 3, Qt::CustomDashLine, Qt::RoundCap);
    pen.setDashPattern({4, 2.5});
    m_preview->setPen(pen);
    m_preview->setPath(path);
}
