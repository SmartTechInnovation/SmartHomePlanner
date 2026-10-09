#include "ProjectTree.h"

#include "core/Geometry.h"
#include "core/IconCache.h"
#include "core/Library.h"
#include "core/Project.h"
#include "scene/DeviceItem.h"
#include "scene/GroupItem.h"
#include "scene/PanelItems.h"
#include "scene/PlanScene.h"
#include "scene/RoomItem.h"
#include "scene/StripItem.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <functional>
#include <algorithm>

namespace {

constexpr int KindRole    = Qt::UserRole + 1;
constexpr int FloorRole   = Qt::UserRole + 2;
constexpr int PtrRole     = Qt::UserRole + 3;
constexpr int KeyRole     = Qt::UserRole + 4;
constexpr int SectionRole = Qt::UserRole + 5;

QIcon swatch(const QColor &c)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(c.darker(140));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(2.5, 2.5, 11, 11), 3, 3);
    return QIcon(pm);
}

QIcon appIcon(const char *name)
{
    return IconCache::instance().tiledIcon(QStringLiteral(":/resources/icons/%1.svg").arg(QLatin1String(name)));
}

// An element on the plan: device or LED strip
struct Elem {
    QGraphicsItem *item;
    const DeviceData *data;
    QPointF pos;
};

} // namespace

ProjectTree::ProjectTree(Project *project, QWidget *parent) : QTreeWidget(parent), m_project(project)
{
    setColumnCount(2);
    setHeaderHidden(true);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    setIconSize(QSize(20, 20));
    setUniformRowHeights(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    setExpandsOnDoubleClick(false);
    setAnimated(true);

    connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) { onClicked(it); });
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) { onDoubleClicked(it); });
    connect(this, &QTreeWidget::itemChanged, this, &ProjectTree::onChanged);
    connect(project, &Project::floorsChanged, this, [this] {
        watchFloors();
        scheduleRebuild();
    });
    connect(project, &Project::currentChanged, this, &ProjectTree::scheduleRebuild);
    connect(project, &Project::modified, this, &ProjectTree::scheduleRebuild);
    watchFloors();
    rebuild();
}

void ProjectTree::watchFloors()
{
    for (const QPointer<PlanScene> &s : m_watched)
        if (s)
            disconnect(s, nullptr, this, nullptr);
    m_watched.clear();
    for (PlanScene *s : m_project->floors()) {
        connect(s, &PlanScene::modified, this, &ProjectTree::scheduleRebuild);
        connect(s, &PlanScene::itemEdited, this, &ProjectTree::scheduleRebuild);
        connect(s, &QGraphicsScene::selectionChanged, this, &ProjectTree::syncSelectionFromScene);
        m_watched << s;
    }
}

void ProjectTree::resetExpansion()
{
    m_fresh = true;
    clear();
    m_byGraphicsItem.clear();
}

void ProjectTree::scheduleRebuild()
{
    if (m_rebuildPending)
        return;
    m_rebuildPending = true;
    QTimer::singleShot(120, this, [this] {
        m_rebuildPending = false;
        rebuild();
    });
}

QString ProjectTree::pathKey(QTreeWidgetItem *it) const
{
    return it->data(0, KeyRole).toString();
}

void ProjectTree::rebuild()
{
    // remember what was expanded
    QSet<QString> expanded;
    QSet<QString> known;
    std::function<void(QTreeWidgetItem *)> collect = [&](QTreeWidgetItem *it) {
        known.insert(pathKey(it));
        if (it->isExpanded())
            expanded.insert(pathKey(it));
        for (int i = 0; i < it->childCount(); ++i)
            collect(it->child(i));
    };
    for (int i = 0; i < topLevelItemCount(); ++i)
        collect(topLevelItem(i));
    const int scroll = verticalScrollBar()->value();

    m_building = true;
    clear();
    m_byGraphicsItem.clear();
    const Library &lib = Library::instance();

    auto *root = new QTreeWidgetItem(this);
    root->setText(0, m_project->info().name);
    root->setIcon(0, appIcon("project"));
    root->setData(0, KindRole, KProject);
    root->setData(0, KeyRole, QStringLiteral("project"));
    QFont bold = root->font(0);
    bold.setBold(true);
    root->setFont(0, bold);

    // floors first, then the panels (under the floor they are on)
    QList<int> order;
    for (int fi = 0; fi < m_project->floorCount(); ++fi)
        if (!m_project->floor(fi)->isPanel() || !m_project->floor(fi)->parentFloor())
            order << fi;
    for (int fi = 0; fi < m_project->floorCount(); ++fi)
        if (m_project->floor(fi)->isPanel() && m_project->floor(fi)->parentFloor())
            order << fi;
    QHash<PlanScene *, QTreeWidgetItem *> floorNodes;
    for (int fi : order) {
        PlanScene *scene = m_project->floor(fi);
        const QString floorKey = QStringLiteral("floor:%1").arg(fi);

        // gather the elements
        QList<Elem> elems;
        for (DeviceItem *d : scene->devices())
            elems.append({d, &d->data(), d->scenePos()});
        for (StripItem *s : scene->strips())
            elems.append({s, &s->data(), s->sceneBoundingRect().center()});
        for (EquipmentItem *e : scene->equipment())
            elems.append({e, &e->data(), e->sceneBoundingRect().center()});

        QTreeWidgetItem *parentNode = scene->isPanel() && floorNodes.contains(scene->parentFloor())
                                          ? floorNodes.value(scene->parentFloor()) : root;
        auto *floorIt = new QTreeWidgetItem(parentNode);
        floorNodes.insert(scene, floorIt);
        floorIt->setText(0, scene->floorName());
        floorIt->setText(1, QString::number(elems.size()));
        floorIt->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        floorIt->setIcon(0, appIcon(scene->isPanel() ? "panel" : "floor"));
        floorIt->setData(0, KindRole, KFloor);
        floorIt->setData(0, FloorRole, fi);
        floorIt->setData(0, KeyRole, floorKey);
        floorIt->setFlags(floorIt->flags() | Qt::ItemIsEditable);
        floorIt->setToolTip(0, tr("Click to open this floor. F2 or right-click to rename."));
        if (fi == m_project->currentIndex()) {
            floorIt->setFont(0, bold);
            floorIt->setForeground(0, QColor(30, 100, 190));
        }

        // assign every element to the smallest room containing it
        QList<RoomItem *> rooms = scene->rooms();
        std::sort(rooms.begin(), rooms.end(), [](RoomItem *a, RoomItem *b) { return a->name().localeAwareCompare(b->name()) < 0; });
        QHash<RoomItem *, QPolygonF> polys;
        for (RoomItem *r : rooms)
            polys.insert(r, r->scenePoints());
        QHash<RoomItem *, QList<Elem>> byRoom;
        QList<Elem> outside;
        for (const Elem &e : elems) {
            RoomItem *best = nullptr;
            double bestArea = 0;
            for (RoomItem *r : rooms) {
                if (polys[r].containsPoint(e.pos, Qt::OddEvenFill) && (!best || r->area() < bestArea)) {
                    best = r;
                    bestArea = r->area();
                }
            }
            if (best)
                byRoom[best].append(e);
            else
                outside.append(e);
        }

        auto addChapters = [&](QTreeWidgetItem *parent, const QList<Elem> &list, const QString &parentKey) {
            for (const SectionDef &sec : lib.sections()) {
                QList<Elem> inSec;
                for (const Elem &e : list)
                    if (e.data->section == sec.id)
                        inSec << e;
                if (inSec.isEmpty())
                    continue;
                std::sort(inSec.begin(), inSec.end(), [](const Elem &a, const Elem &b) {
                    const QString ia = a.data->id, ib = b.data->id;
                    if (ia.size() != ib.size())
                        return ia.size() < ib.size();
                    return ia < ib;
                });
                auto *chap = new QTreeWidgetItem(parent);
                chap->setText(0, sec.name);
                chap->setText(1, QString::number(inSec.size()));
                chap->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
                chap->setIcon(0, swatch(sec.color));
                chap->setData(0, KindRole, KChapter);
                chap->setData(0, FloorRole, fi);
                chap->setData(0, SectionRole, sec.id);
                chap->setData(0, KeyRole, parentKey + "/chap:" + sec.id);
                if (!scene->isSectionVisible(sec.id))
                    chap->setForeground(0, QColor(150, 150, 150));
                for (const Elem &e : inSec) {
                    auto *el = new QTreeWidgetItem(chap);
                    const QString name = e.data->displayName();
                    el->setText(0, e.data->id.isEmpty() ? name : e.data->id + QStringLiteral("  ") + name);
                    el->setIcon(0, IconCache::instance().icon(e.data->iconPath));
                    el->setData(0, KindRole, KElement);
                    el->setData(0, FloorRole, fi);
                    el->setData(0, PtrRole, QVariant::fromValue<quintptr>(quintptr(e.item)));
                    el->setData(0, KeyRole, chap->data(0, KeyRole).toString() + "/" + e.data->id);
                    QString tip = QStringLiteral("<b>%1</b> %2").arg(e.data->id.toHtmlEscaped(), name.toHtmlEscaped());
                    if (!e.data->note.isEmpty())
                        tip += QStringLiteral("<br>%1: %2").arg(tr("Note"), e.data->note.toHtmlEscaped());
                    if (e.data->kind == DeviceKind::Light || e.data->kind == DeviceKind::Strip || e.data->kind == DeviceKind::Speaker)
                        tip += QStringLiteral("<br>%1").arg(e.data->on ? tr("On") : tr("Off"));
                    el->setToolTip(0, tip);
                    m_byGraphicsItem.insert(e.item, el);
                }
            }
        };

        for (RoomItem *r : rooms) {
            auto *roomIt = new QTreeWidgetItem(floorIt);
            const QString key = floorKey + "/room:" + r->name();
            roomIt->setText(0, r->name().isEmpty() ? tr("Room") : r->name());
            roomIt->setText(1, Geo::formatArea(r->area()));
            roomIt->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            roomIt->setForeground(1, QColor(30, 100, 190));
            roomIt->setIcon(0, appIcon("roomtree"));
            roomIt->setData(0, KindRole, KRoom);
            roomIt->setData(0, FloorRole, fi);
            roomIt->setData(0, PtrRole, QVariant::fromValue<quintptr>(quintptr(r)));
            roomIt->setData(0, KeyRole, key);
            roomIt->setFlags(roomIt->flags() | Qt::ItemIsEditable);
            m_byGraphicsItem.insert(r, roomIt);
            addChapters(roomIt, byRoom.value(r), key);
            if (!known.contains(key))
                if (!m_fresh)
                    expanded.insert(key);       // rooms drawn now start expanded
        }
        if (!outside.isEmpty()) {
            auto *outIt = new QTreeWidgetItem(floorIt);
            const QString key = floorKey + "/outside";
            outIt->setText(0, rooms.isEmpty() ? tr("Elements") : tr("Outside / no room"));
            outIt->setText(1, QString::number(outside.size()));
            outIt->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            outIt->setIcon(0, appIcon("outside"));
            outIt->setData(0, KindRole, KOutside);
            outIt->setData(0, FloorRole, fi);
            outIt->setData(0, KeyRole, key);
            addChapters(outIt, outside, key);
            if (!known.contains(key) && !m_fresh)
                expanded.insert(key);
        }
        // groups (floor plans) and connections (panels)
        auto leaf = [&](QTreeWidgetItem *parent, QGraphicsItem *gi, const QString &text, const QIcon &icon,
                        const QString &key, const QString &tip) {
            auto *el = new QTreeWidgetItem(parent);
            el->setText(0, text);
            el->setIcon(0, icon);
            el->setData(0, KindRole, KElement);
            el->setData(0, FloorRole, fi);
            el->setData(0, PtrRole, QVariant::fromValue<quintptr>(quintptr(gi)));
            el->setData(0, KeyRole, key);
            el->setToolTip(0, tip);
            m_byGraphicsItem.insert(gi, el);
            return el;
        };
        const QList<GroupItem *> groups = scene->groups();
        if (!groups.isEmpty()) {
            auto *gIt = new QTreeWidgetItem(floorIt);
            const QString key = floorKey + "/groups";
            gIt->setText(0, tr("Groups"));
            gIt->setText(1, QString::number(groups.size()));
            gIt->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            gIt->setIcon(0, appIcon("group"));
            gIt->setData(0, KindRole, KOutside);
            gIt->setData(0, FloorRole, fi);
            gIt->setData(0, KeyRole, key);
            for (GroupItem *g : groups) {
                QStringList ids;
                for (QGraphicsItem *m : g->memberItems()) {
                    if (m->type() == ItemType::Device)
                        ids << static_cast<DeviceItem *>(m)->data().id;
                    else if (m->type() == ItemType::Strip)
                        ids << static_cast<StripItem *>(m)->data().id;
                }
                auto *el = leaf(gIt, g, g->name() + QStringLiteral("  (") + ids.join(QStringLiteral(", ")) + ")",
                                swatch(g->color()), key + "/" + g->name(),
                                g->note().isEmpty() ? ids.join(QStringLiteral(", ")) : g->note());
                el->setText(1, QString::number(ids.size()));
                el->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            }
        }
        const QList<WireItem *> wires = scene->wires();
        if (!wires.isEmpty()) {
            auto *wIt = new QTreeWidgetItem(floorIt);
            const QString key = floorKey + "/wires";
            wIt->setText(0, tr("Connections"));
            wIt->setText(1, QString::number(wires.size()));
            wIt->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            wIt->setIcon(0, appIcon("wire"));
            wIt->setData(0, KindRole, KOutside);
            wIt->setData(0, FloorRole, fi);
            wIt->setData(0, KeyRole, key);
            QList<WireItem *> sorted = wires;
            std::sort(sorted.begin(), sorted.end(), [](WireItem *a, WireItem *b) {
                return a->endText(true).localeAwareCompare(b->endText(true)) < 0;
            });
            for (WireItem *w : sorted) {
                const QString text = w->endText(true) + QStringLiteral("  →  ") + w->endText(false);
                leaf(wIt, w, text, swatch(w->color()), key + "/" + text,
                     WireItem::colorName(w->color()) + QStringLiteral(", ") + w->crossSection() + QStringLiteral(" mm²")
                         + (w->label().isEmpty() ? QString() : QStringLiteral("<br>") + w->label().toHtmlEscaped()));
            }
        }
        if (!known.contains(floorKey) && !m_fresh)
            expanded.insert(floorKey);
    }

    auto *add = new QTreeWidgetItem(root);
    add->setText(0, tr("Add floor"));
    add->setIcon(0, appIcon("flooradd"));
    add->setData(0, KindRole, KAddFloor);
    add->setData(0, KeyRole, QStringLiteral("add"));
    add->setForeground(0, QColor(30, 100, 190));
    add->setToolTip(0, tr("Add a new floor with its own plan"));

    expanded.insert(QStringLiteral("project"));
    std::function<void(QTreeWidgetItem *)> restore = [&](QTreeWidgetItem *it) {
        if (expanded.contains(pathKey(it)))
            it->setExpanded(true);
        for (int i = 0; i < it->childCount(); ++i)
            restore(it->child(i));
    };
    restore(root);
    verticalScrollBar()->setValue(scroll);
    m_building = false;
    m_initialized = true;
    syncSelectionFromScene();
    m_fresh = false;
}

void ProjectTree::syncSelectionFromScene()
{
    if (m_syncing || m_building)
        return;
    PlanScene *scene = m_project->current();
    if (!scene)
        return;
    m_syncing = true;
    clearSelection();
    QTreeWidgetItem *first = nullptr;
    for (QGraphicsItem *gi : scene->selectedItems()) {
        if (QTreeWidgetItem *ti = m_byGraphicsItem.value(gi)) {
            ti->setSelected(true);
            if (!m_fresh)
                for (QTreeWidgetItem *p = ti->parent(); p; p = p->parent())
                    p->setExpanded(true);
            if (!first)
                first = ti;
        }
    }
    if (first && !m_fresh)
        scrollToItem(first);
    m_syncing = false;
}

void ProjectTree::onClicked(QTreeWidgetItem *it)
{
    const int kind = it->data(0, KindRole).toInt();
    const int fi = it->data(0, FloorRole).toInt();
    if (kind == KAddFloor) {
        emit addFloorRequested();
        return;
    }
    if (kind == KProject)
        return;
    m_project->setCurrent(fi);
    PlanScene *scene = m_project->floor(fi);
    if (!scene || kind == KFloor)
        return;

    m_syncing = true;
    // rooms are edited with the Room tool
    if (kind == KRoom && scene->tool() != PlanScene::RoomTool)
        scene->setTool(PlanScene::RoomTool);
    scene->clearSelection();
    QGraphicsItem *focus = nullptr;
    // select every element under the clicked node (or the node itself)
    for (QTreeWidgetItem *sel : selectedItems()) {
        std::function<void(QTreeWidgetItem *)> pick = [&](QTreeWidgetItem *n) {
            const int k = n->data(0, KindRole).toInt();
            if (k == KElement || (k == KRoom && n == sel)) {
                auto *gi = reinterpret_cast<QGraphicsItem *>(n->data(0, PtrRole).value<quintptr>());
                if (gi && m_byGraphicsItem.contains(gi)) {
                    if (!gi->isVisible() && gi->type() != ItemType::Room) {
                        // element of a hidden chapter: show that chapter
                        if (gi->type() == ItemType::Device)
                            scene->setSectionVisible(static_cast<DeviceItem *>(gi)->data().section, true);
                        else if (gi->type() == ItemType::Strip)
                            scene->setSectionVisible(static_cast<StripItem *>(gi)->data().section, true);
                    }
                    gi->setSelected(true);
                    if (!focus)
                        focus = gi;
                }
                if (k == KRoom)
                    return;
            }
            for (int i = 0; i < n->childCount(); ++i)
                pick(n->child(i));
        };
        if (sel->data(0, FloorRole).toInt() == fi)
            pick(sel);
    }
    m_syncing = false;
    if (focus && kind == KElement)
        emit focusItem(focus);
}

void ProjectTree::onDoubleClicked(QTreeWidgetItem *it)
{
    const int kind = it->data(0, KindRole).toInt();
    if (kind == KElement || kind == KRoom) {
        auto *gi = reinterpret_cast<QGraphicsItem *>(it->data(0, PtrRole).value<quintptr>());
        if (gi && m_byGraphicsItem.contains(gi))
            emit focusItem(gi);
    } else if (kind == KChapter || kind == KOutside || kind == KFloor) {
        it->setExpanded(!it->isExpanded());
    }
}

void ProjectTree::onChanged(QTreeWidgetItem *it, int column)
{
    if (m_building || column != 0)
        return;
    const int kind = it->data(0, KindRole).toInt();
    const int fi = it->data(0, FloorRole).toInt();
    if (kind == KFloor) {
        m_project->renameFloor(fi, it->text(0));
    } else if (kind == KRoom) {
        auto *gi = reinterpret_cast<QGraphicsItem *>(it->data(0, PtrRole).value<quintptr>());
        PlanScene *scene = m_project->floor(fi);
        if (gi && scene && m_byGraphicsItem.contains(gi)) {
            static_cast<RoomItem *>(gi)->setName(it->text(0).trimmed());
            scene->commit(tr("Rename room"));
        }
    }
}

void ProjectTree::contextMenuEvent(QContextMenuEvent *e)
{
    QTreeWidgetItem *it = itemAt(e->pos());
    if (!it)
        return;
    const int kind = it->data(0, KindRole).toInt();
    const int fi = it->data(0, FloorRole).toInt();
    QMenu menu(this);
    if (kind == KProject) {
        menu.addAction(appIcon("info"), tr("Project information..."), this, &ProjectTree::editProjectInfo);
        menu.addAction(appIcon("flooradd"), tr("Add floor"), this, &ProjectTree::addFloorRequested);
    } else if (kind == KFloor && m_project->floor(fi) && m_project->floor(fi)->isPanel()) {
        PlanScene *panel = m_project->floor(fi);
        menu.addAction(tr("Rename"), this, [this, it] { editItem(it, 0); });
        QMenu *on = menu.addMenu(tr("On floor"));
        for (PlanScene *f : m_project->floors()) {
            if (f->isPanel())
                continue;
            QAction *a = on->addAction(f->floorName(), this, [this, panel, f] { m_project->setPanelFloor(panel, f); });
            a->setCheckable(true);
            a->setChecked(panel->parentFloor() == f);
        }
        menu.addSeparator();
        QAction *del = menu.addAction(appIcon("delete"), tr("Delete panel..."), this, [this, fi] { emit deleteFloorRequested(fi); });
        del->setEnabled(m_project->floorCount() > 1);
    } else if (kind == KFloor) {
        menu.addAction(tr("Rename"), this, [this, it] { editItem(it, 0); });
        menu.addAction(appIcon("import"), tr("Import floor plan..."), this, [this, fi] { emit importPlanRequested(fi); });
        menu.addSeparator();
        QAction *up = menu.addAction(tr("Move up"), this, [this, fi] { m_project->moveFloor(fi, -1); });
        up->setEnabled(fi > 0);
        QAction *down = menu.addAction(tr("Move down"), this, [this, fi] { m_project->moveFloor(fi, 1); });
        down->setEnabled(fi < m_project->floorCount() - 1);
        menu.addSeparator();
        menu.addAction(appIcon("flooradd"), tr("Add floor"), this, &ProjectTree::addFloorRequested);
        menu.addAction(appIcon("panel"), tr("Add Panel"), this, [this, fi] { emit addPanelRequested(fi); });
        QAction *del = menu.addAction(appIcon("delete"), tr("Delete floor..."), this, [this, fi] { emit deleteFloorRequested(fi); });
        del->setEnabled(m_project->floorCount() > 1);
    } else if (kind == KRoom) {
        menu.addAction(tr("Rename"), this, [this, it] { editItem(it, 0); });
        menu.addAction(tr("Zoom to room"), this, [this, it] { onDoubleClicked(it); });
    } else if (kind == KElement) {
        menu.addAction(tr("Zoom to element"), this, [this, it] { onDoubleClicked(it); });
    } else if (kind == KChapter) {
        const QString sid = it->data(0, SectionRole).toString();
        menu.addAction(tr("Show only this chapter"), this, [this, fi, sid] {
            QSet<QString> hidden;
            for (const SectionDef &s : Library::instance().sections())
                if (s.id != sid)
                    hidden.insert(s.id);
            if (PlanScene *scene = m_project->floor(fi))
                scene->setHiddenSections(hidden);
        });
        menu.addAction(tr("Show all chapters"), this, [this, fi] {
            if (PlanScene *scene = m_project->floor(fi))
                scene->setHiddenSections({});
        });
    } else {
        return;
    }
    menu.exec(e->globalPos());
}
