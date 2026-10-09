#include "SceneSerializer.h"

#include "BackgroundItem.h"
#include "DeviceItem.h"
#include "GroupItem.h"
#include "PanelItems.h"
#include "NoteItem.h"
#include "PlanScene.h"
#include "RoomItem.h"
#include "StripItem.h"
#include "WallItem.h"
#include "core/Library.h"
#include "core/Project.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace SceneSerializer {

namespace {

QString num(double v) { return QString::number(v, 'g', 10); }

void writePoints(QDomDocument &doc, QDomElement &el, const QPolygonF &pts)
{
    for (const QPointF &p : pts) {
        QDomElement pe = doc.createElement("P");
        pe.setAttribute("x", num(p.x()));
        pe.setAttribute("y", num(p.y()));
        el.appendChild(pe);
    }
}

QPolygonF readPoints(const QDomElement &el)
{
    QPolygonF pts;
    for (QDomElement p = el.firstChildElement("P"); !p.isNull(); p = p.nextSiblingElement("P"))
        pts << QPointF(p.attribute("x").toDouble(), p.attribute("y").toDouble());
    return pts;
}

void writeDeviceData(QDomElement &e, const DeviceData &d)
{
    if (!d.uid.isEmpty()) e.setAttribute("uid", d.uid);
    e.setAttribute("key", d.key);
    e.setAttribute("name", d.name);
    if (!d.variant.isEmpty()) e.setAttribute("variant", d.variant);
    e.setAttribute("icon", QFileInfo(d.iconPath).fileName());
    e.setAttribute("section", d.section);
    e.setAttribute("kind", deviceKindToString(d.kind));
    e.setAttribute("id", d.id);
    e.setAttribute("note", d.note);
    e.setAttribute("showNote", d.showNote ? 1 : 0);
    if (!d.doc.isEmpty())  e.setAttribute("doc", d.doc);
    if (!d.shop.isEmpty()) e.setAttribute("shop", d.shop);
    e.setAttribute("size", num(d.size));
    e.setAttribute("tile", d.tile ? 1 : 0);
    e.setAttribute("rot", num(d.rotation));
    e.setAttribute("on", d.on ? 1 : 0);
    if (d.kind == DeviceKind::Light) {
        e.setAttribute("beam", num(d.beam));
        e.setAttribute("mount", num(d.mountHeight));
        e.setAttribute("cct", d.cct);
    }
    if (d.rgb.isValid())
        e.setAttribute("rgb", d.rgb.name());
    if (d.kind == DeviceKind::Speaker || d.kind == DeviceKind::Strip || d.kind == DeviceKind::Wifi)
        e.setAttribute("radius", num(d.radius));
    if (d.kind == DeviceKind::Wifi)
        e.setAttribute("wallloss", num(d.wallLoss));
}

QString resolveIcon(const QString &key, const QString &iconName)
{
    const Library &lib = Library::instance();
    if (const LibraryEntry *e = lib.entry(key))
        return e->iconPath;
    if (iconName.isEmpty())
        return {};
    for (const LibraryEntry *e : lib.entries())
        if (QFileInfo(e->iconPath).fileName() == iconName)
            return e->iconPath;
    return iconName;
}

DeviceData readDeviceData(const QDomElement &e)
{
    DeviceData d;
    d.uid = e.attribute("uid");
    if (d.uid.isEmpty())
        d.uid = DeviceData::newUid();
    d.key = e.attribute("key");
    d.name = e.attribute("name");
    d.variant = e.attribute("variant");
    d.iconPath = resolveIcon(d.key, e.attribute("icon"));
    d.section = e.attribute("section", "other");
    d.kind = deviceKindFromString(e.attribute("kind"));
    d.id = e.attribute("id");
    d.note = e.attribute("note");
    d.showNote = e.attribute("showNote", "1").toInt() != 0;
    d.doc = e.attribute("doc");
    d.shop = e.attribute("shop");
    // documentation / shop link not set on the element: take them from the library
    // (so links added to the XML later also reach elements already placed)
    if (const LibraryEntry *le = Library::instance().entry(d.key)) {
        if (d.doc.isEmpty())
            d.doc = le->doc;
        if (d.shop.isEmpty())
            d.shop = le->shop;
    }
    d.size = e.attribute("size", "30").toDouble();
    if (e.hasAttribute("tile")) {
        d.tile = e.attribute("tile").toInt() != 0;
    } else if (const LibraryEntry *le = Library::instance().entry(d.key)) {
        d.tile = le->wantsTile();       // files saved before the option: library default
    } else {
        d.tile = d.iconPath.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive);
    }
    d.rotation = e.attribute("rot", "0").toDouble();
    d.on = e.attribute("on", "0").toInt() != 0;
    d.beam = e.attribute("beam", "60").toDouble();
    d.mountHeight = e.attribute("mount", "0").toDouble();
    d.cct = e.attribute("cct", "3000").toInt();
    if (e.hasAttribute("rgb"))
        d.rgb = QColor(e.attribute("rgb"));
    d.radius = e.attribute("radius", "300").toDouble();
    d.wallLoss = e.attribute("wallloss", "35").toDouble();
    return d;
}

QDomElement writeItem(QDomDocument &doc, QGraphicsItem *it)
{
    switch (it->type()) {
    case ItemType::Wall: {
        auto *w = static_cast<WallItem *>(it);
        QDomElement e = doc.createElement("Wall");
        e.setAttribute("x", num(w->pos().x()));
        e.setAttribute("y", num(w->pos().y()));
        e.setAttribute("thickness", num(w->thickness()));
        e.setAttribute("closed", w->isClosed() ? 1 : 0);
        writePoints(doc, e, w->points());
        return e;
    }
    case ItemType::Room: {
        auto *r = static_cast<RoomItem *>(it);
        QDomElement e = doc.createElement("Room");
        e.setAttribute("x", num(r->pos().x()));
        e.setAttribute("y", num(r->pos().y()));
        e.setAttribute("name", r->name());
        e.setAttribute("color", r->color().name());
        e.setAttribute("showArea", r->showArea() ? 1 : 0);
        e.setAttribute("lx", num(r->labelOffset().x()));
        e.setAttribute("ly", num(r->labelOffset().y()));
        writePoints(doc, e, r->points());
        return e;
    }
    case ItemType::Device: {
        auto *d = static_cast<DeviceItem *>(it);
        QDomElement e = doc.createElement("Device");
        e.setAttribute("x", num(d->pos().x()));
        e.setAttribute("y", num(d->pos().y()));
        writeDeviceData(e, d->data());
        e.setAttribute("nx", num(d->noteItem()->pos().x()));
        e.setAttribute("ny", num(d->noteItem()->pos().y()));
        return e;
    }
    case ItemType::Strip: {
        auto *s = static_cast<StripItem *>(it);
        QDomElement e = doc.createElement("Strip");
        e.setAttribute("x", num(s->pos().x()));
        e.setAttribute("y", num(s->pos().y()));
        e.setAttribute("mode", s->mode() == StripItem::Circle ? "circle" : "line");
        writeDeviceData(e, s->data());
        e.setAttribute("nx", num(s->noteItem()->pos().x()));
        e.setAttribute("ny", num(s->noteItem()->pos().y()));
        writePoints(doc, e, s->points());
        return e;
    }
    case ItemType::Group: {
        auto *g = static_cast<GroupItem *>(it);
        QDomElement e = doc.createElement("Group");
        e.setAttribute("name", g->name());
        e.setAttribute("color", g->color().name());
        e.setAttribute("style", g->style() == GroupItem::Straight ? "straight" : "curved");
        if (!g->note().isEmpty())
            e.setAttribute("note", g->note());
        // links "a>b;c>d" between elements that still exist
        auto *ps = qobject_cast<PlanScene *>(g->scene());
        QStringList links;
        for (const GroupItem::Link &l : g->links())
            if (!ps || (ps->itemByUid(l.first) && ps->itemByUid(l.second)))
                links << l.first + QLatin1Char('>') + l.second;
        e.setAttribute("links", links.join(';'));
        return e;
    }
    case ItemType::Rail: {
        auto *r = static_cast<RailItem *>(it);
        QDomElement e = doc.createElement("Rail");
        e.setAttribute("x", num(r->pos().x()));
        e.setAttribute("y", num(r->pos().y()));
        e.setAttribute("length", num(r->length()));
        if (!r->label().isEmpty())
            e.setAttribute("label", r->label());
        return e;
    }
    case ItemType::Equipment: {
        auto *q = static_cast<EquipmentItem *>(it);
        QDomElement e = doc.createElement("Equipment");
        e.setAttribute("x", num(q->pos().x()));
        e.setAttribute("y", num(q->pos().y()));
        writeDeviceData(e, q->data());
        e.setAttribute("w", num(q->size().width()));
        e.setAttribute("h", num(q->size().height()));
        for (int i = 0; i < q->portCount(); ++i) {
            QDomElement pe = doc.createElement("Port");
            pe.setAttribute("x", num(q->portPos(i).x()));
            pe.setAttribute("y", num(q->portPos(i).y()));
            if (!q->portNames().value(i).isEmpty())
                pe.setAttribute("name", q->portNames().value(i));
            e.appendChild(pe);
        }
        return e;
    }
    case ItemType::Wire: {
        auto *w = static_cast<WireItem *>(it);
        QDomElement e = doc.createElement("Wire");
        e.setAttribute("from", w->fromUid());
        e.setAttribute("fromPort", w->fromPort());
        e.setAttribute("to", w->toUid());
        e.setAttribute("toPort", w->toPort());
        e.setAttribute("color", w->color().name());
        e.setAttribute("section", w->crossSection());
        if (!w->label().isEmpty())
            e.setAttribute("label", w->label());
        if (w->isManual()) {
            QPolygonF bends;
            for (const QPointF &p : w->bends())
                bends << p;
            writePoints(doc, e, bends);
        }
        return e;
    }
    default:
        return {};
    }
}

} // namespace

QDomElement writeContent(QDomDocument &doc, const QList<QGraphicsItem *> &items)
{
    QDomElement content = doc.createElement("Content");
    // keep a stable order: rooms, walls, strips, devices
    const int order[] = {ItemType::Room, ItemType::Wall, ItemType::Rail, ItemType::Strip, ItemType::Device,
                         ItemType::Equipment, ItemType::Wire, ItemType::Group};
    for (int t : order) {
        for (QGraphicsItem *it : items) {
            if (it->type() != t)
                continue;
            QDomElement e = writeItem(doc, it);
            if (!e.isNull())
                content.appendChild(e);
        }
    }
    return content;
}

QList<QGraphicsItem *> readContent(PlanScene *scene, const QDomElement &content, const QPointF &offset, bool newUids)
{
    QList<QGraphicsItem *> created;
    // pasted copies get new uids; groups / wires among them are re-pointed to the copies
    QHash<QString, QString> uidMap;
    auto mapUid = [&](DeviceData &d) {
        if (!newUids)
            return;
        const QString n = DeviceData::newUid();
        uidMap.insert(d.uid, n);
        d.uid = n;
    };
    auto refUid = [&](const QString &old) -> QString {
        if (!newUids)
            return old;
        return uidMap.value(old);
    };
    QList<QDomElement> links;       // groups and wires: read after the elements
    for (QDomElement e = content.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        const QPointF pos(e.attribute("x").toDouble() + offset.x(), e.attribute("y").toDouble() + offset.y());
        const QString tag = e.tagName();
        if (tag == "Wall") {
            auto *w = new WallItem;
            w->setThickness(e.attribute("thickness", "20").toDouble());
            w->setPos(pos);
            w->setPoints(readPoints(e));
            w->setClosed(e.attribute("closed").toInt() != 0);
            scene->addItem(w);
            created << w;
        } else if (tag == "Room") {
            auto *r = new RoomItem;
            r->setName(e.attribute("name"));
            r->setColor(QColor(e.attribute("color", "#78AADC")));
            r->setShowArea(e.attribute("showArea", "1").toInt() != 0);
            r->setLabelOffset(QPointF(e.attribute("lx").toDouble(), e.attribute("ly").toDouble()));
            r->setPos(pos);
            r->setPoints(readPoints(e));
            scene->addItem(r);
            created << r;
        } else if (tag == "Device") {
            DeviceData dd = readDeviceData(e);
            mapUid(dd);
            auto *d = new DeviceItem(dd);
            d->setPos(pos);
            if (e.hasAttribute("nx"))
                d->noteItem()->setPos(e.attribute("nx").toDouble(), e.attribute("ny").toDouble());
            scene->addItem(d);
            created << d;
        } else if (tag == "Strip") {
            DeviceData dd = readDeviceData(e);
            dd.kind = DeviceKind::Strip;
            mapUid(dd);
            auto *s = new StripItem(dd, e.attribute("mode") == "circle" ? StripItem::Circle : StripItem::Line);
            s->setPos(pos);
            s->setPoints(readPoints(e));
            if (e.hasAttribute("nx"))
                s->noteItem()->setPos(e.attribute("nx").toDouble(), e.attribute("ny").toDouble());
            scene->addItem(s);
            created << s;
        } else if (tag == "Rail") {
            auto *r = new RailItem(e.attribute("length", "450").toDouble());
            r->setLabel(e.attribute("label"));
            scene->addItem(r);
            r->setPos(pos);
            scene->applyEditability(r);
            created << r;
        } else if (tag == "Equipment") {
            DeviceData dd = readDeviceData(e);
            mapUid(dd);
            QVector<QPointF> ports;
            QStringList names;
            for (QDomElement pe = e.firstChildElement("Port"); !pe.isNull(); pe = pe.nextSiblingElement("Port")) {
                ports << QPointF(pe.attribute("x").toDouble(), pe.attribute("y").toDouble());
                names << pe.attribute("name");
            }
            auto *q = new EquipmentItem(dd, QSizeF(e.attribute("w", "18").toDouble(), e.attribute("h", "86").toDouble()),
                                        ports, names);
            q->setFlag(QGraphicsItem::ItemSendsGeometryChanges, false);     // exact position, no rail snap
            q->setPos(pos);
            q->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
            scene->addItem(q);
            scene->applyEditability(q);
            created << q;
        } else if (tag == "Wire" || tag == "Group") {
            links << e;
        }
    }
    for (const QDomElement &e : links) {
        if (e.tagName() == "Wire") {
            const QString a = refUid(e.attribute("from"));
            const QString b = refUid(e.attribute("to"));
            if (a.isEmpty() || b.isEmpty())
                continue;
            auto *w = new WireItem;
            w->setColor(QColor(e.attribute("color", "#8B4513")));
            w->setCrossSection(e.attribute("section", "1.5"));
            w->setLabel(e.attribute("label"));
            scene->addItem(w);
            w->setEnds(a, e.attribute("fromPort").toInt(), b, e.attribute("toPort").toInt());
            const QPolygonF bends = readPoints(e);
            if (!bends.isEmpty()) {
                QVector<QPointF> v;
                for (const QPointF &p : bends)
                    v << p + offset;
                w->setManualBends(v);
            }
            scene->applyEditability(w);
            created << w;
        } else {
            // links (current files) or a member list (first version: linked as a chain)
            QVector<GroupItem::Link> links;
            for (const QString &l : e.attribute("links").split(';', Qt::SkipEmptyParts)) {
                const QStringList ab = l.split('>');
                if (ab.size() != 2)
                    continue;
                const QString a = refUid(ab[0]), b = refUid(ab[1]);
                if (!a.isEmpty() && !b.isEmpty() && a != b)
                    links.push_back({a, b});
            }
            QStringList members;
            if (!e.hasAttribute("links"))
                for (const QString &m : e.attribute("members").split(',', Qt::SkipEmptyParts)) {
                    const QString u = refUid(m);
                    if (!u.isEmpty())
                        members << u;
                }
            if (links.isEmpty() && members.size() < 2)
                continue;
            auto *g = new GroupItem;
            g->setName(e.attribute("name"));
            g->setColor(QColor(e.attribute("color", "#DC2828")));
            g->setStyle(e.attribute("style") == "straight" ? GroupItem::Straight : GroupItem::Curved);
            g->setNote(e.attribute("note"));
            scene->addItem(g);
            if (!links.isEmpty())
                g->setLinks(links);
            else
                g->setMembers(members);
            g->setVisible(scene->showGroups());
            scene->applyEditability(g);
            created << g;
        }
    }
    scene->notifyWallsChanged();
    return created;
}

namespace {

void writeInfo(QDomDocument &doc, QDomElement &root, const ProjectInfo &info)
{
    QDomElement ie = doc.createElement("Info");
    ie.setAttribute("name", info.name);
    ie.setAttribute("client", info.client);
    ie.setAttribute("address", info.address);
    ie.setAttribute("author", info.author);
    ie.setAttribute("company", info.company);
    ie.setAttribute("phone", info.phone);
    ie.setAttribute("date", info.date.toString(Qt::ISODate));
    ie.appendChild(doc.createTextNode(info.notes));
    root.appendChild(ie);
}

void readInfo(const QDomElement &root, ProjectInfo &info)
{
    const QDomElement ie = root.firstChildElement("Info");
    if (ie.isNull())
        return;
    info.name = ie.attribute("name");
    info.client = ie.attribute("client");
    info.address = ie.attribute("address");
    info.author = ie.attribute("author");
    info.company = ie.attribute("company");
    info.phone = ie.attribute("phone");
    info.date = QDate::fromString(ie.attribute("date"), Qt::ISODate);
    info.notes = ie.text();
}

void writeHeader(QDomDocument &doc, QDomElement &root, PlanScene *scene, bool withImage)
{
    const ProjectSettings &s = scene->settings();
    QDomElement se = doc.createElement("Settings");
    se.setAttribute("ceilingHeight", num(s.ceilingHeight));
    se.setAttribute("workPlane", num(s.workPlane));
    se.setAttribute("symbolScale", num(s.symbolScale));
    se.setAttribute("wallThickness", num(s.wallThickness));
    se.setAttribute("grid", num(s.gridSize));
    se.setAttribute("hiddenSections", QStringList(scene->hiddenSections().values()).join(','));
    root.appendChild(se);

    if (BackgroundItem *bg = scene->background()) {
        QDomElement be = doc.createElement("Background");
        be.setAttribute("x", num(bg->pos().x()));
        be.setAttribute("y", num(bg->pos().y()));
        be.setAttribute("scale", num(bg->cmPerPixel()));
        be.setAttribute("opacity", num(bg->opacity()));
        be.setAttribute("locked", bg->isLocked() ? 1 : 0);
        if (withImage)
            be.appendChild(doc.createTextNode(QString::fromLatin1(bg->imageData().toBase64())));
        root.appendChild(be);
    }
}

void readHeader(const QDomElement &root, PlanScene *scene, bool withImage)
{
    const QDomElement se = root.firstChildElement("Settings");
    if (!se.isNull()) {
        ProjectSettings &s = scene->settings();
        s.ceilingHeight = se.attribute("ceilingHeight", "270").toDouble();
        s.workPlane = se.attribute("workPlane", "0").toDouble();
        s.symbolScale = se.attribute("symbolScale", "1").toDouble();
        s.wallThickness = se.attribute("wallThickness", "20").toDouble();
        s.gridSize = se.attribute("grid", "10").toDouble();
        const QString hidden = se.attribute("hiddenSections");
        QSet<QString> hs;
        for (const QString &h : hidden.split(',', Qt::SkipEmptyParts))
            hs.insert(h);
        scene->setHiddenSections(hs);
    }
    const QDomElement be = root.firstChildElement("Background");
    if (be.isNull()) {
        if (withImage)
            scene->removeBackground();
        else if (scene->background())
            scene->removeBackground();
    } else {
        if (withImage) {
            const QByteArray data = QByteArray::fromBase64(be.text().toLatin1());
            scene->setBackgroundImage(data);
        }
        if (BackgroundItem *bg = scene->background()) {
            bg->setPos(be.attribute("x").toDouble(), be.attribute("y").toDouble());
            bg->setCmPerPixel(be.attribute("scale", "1").toDouble());
            bg->setOpacity(be.attribute("opacity", "1").toDouble());
            bg->setLocked(be.attribute("locked", "1").toInt() != 0);
        }
    }
}

} // namespace

bool saveProject(Project *project, const QString &path, QString *error)
{
    QDomDocument doc;
    doc.appendChild(doc.createProcessingInstruction("xml", "version=\"1.0\" encoding=\"UTF-8\""));
    QDomElement root = doc.createElement("SmartHomePlan");
    root.setAttribute("version", 2);
    doc.appendChild(root);
    writeInfo(doc, root, project->info());
    for (PlanScene *scene : project->floors()) {
        QDomElement fe = doc.createElement("Floor");
        fe.setAttribute("name", scene->floorName());
        if (scene->isPanel())
            fe.setAttribute("kind", "panel");
        if (scene->isPanel() && scene->parentFloor())
            fe.setAttribute("floor", project->indexOf(scene->parentFloor()));
        writeHeader(doc, fe, scene, true);
        fe.appendChild(writeContent(doc, scene->contentItems()));
        root.appendChild(fe);
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(doc.toByteArray(2));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool loadProject(Project *project, const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QDomDocument doc;
    const auto res = doc.setContent(&f);
    if (!res) {
        if (error) *error = QObject::tr("XML error at line %1: %2").arg(res.errorLine).arg(res.errorMessage);
        return false;
    }
    const QDomElement root = doc.documentElement();
    if (root.tagName() != "SmartHomePlan") {
        if (error) *error = QObject::tr("Not a Smart Home Planner project file.");
        return false;
    }
    project->clearFloors();
    readInfo(root, project->info());

    QList<QDomElement> floorEls;
    for (QDomElement fe = root.firstChildElement("Floor"); !fe.isNull(); fe = fe.nextSiblingElement("Floor"))
        floorEls << fe;
    if (floorEls.isEmpty())
        floorEls << root;           // version 1: a single plan
    int index = 0;
    QList<QPair<PlanScene *, int>> parents;     // panel -> index of its floor, resolved at the end
    for (const QDomElement &fe : floorEls) {
        PlanScene *scene = project->addFloor(fe.attribute("name", project->defaultFloorName(index++)),
                                             fe.attribute("kind") == "panel" ? PlanScene::PanelPage : PlanScene::FloorPage);
        scene->beginRestore();
        readHeader(fe, scene, true);
        readContent(scene, fe.firstChildElement("Content"));
        scene->endRestore();
        scene->refreshAll();
        scene->resetUndo();
        if (fe.hasAttribute("floor"))
            parents.append({scene, fe.attribute("floor").toInt()});
    }
    for (const auto &pp : parents)
        if (PlanScene *f = project->floor(pp.second); f && !f->isPanel())
            pp.first->setParentFloor(f);
    project->setCurrent(0);
    emit project->floorsChanged();
    return true;
}

QString snapshot(PlanScene *scene)
{
    QDomDocument doc;
    QDomElement root = doc.createElement("Snapshot");
    doc.appendChild(root);
    writeHeader(doc, root, scene, false);
    root.appendChild(writeContent(doc, scene->contentItems()));
    return doc.toString(-1);
}

void restoreSnapshot(PlanScene *scene, const QString &xml)
{
    QDomDocument doc;
    if (!doc.setContent(xml))
        return;
    const QDomElement root = doc.documentElement();
    scene->beginRestore();
    scene->clearContent();
    readHeader(root, scene, false);
    readContent(scene, root.firstChildElement("Content"));
    scene->endRestore();
    scene->refreshAll();
}

QByteArray itemsToClipboard(const QList<QGraphicsItem *> &selection)
{
    // wires and groups between copied elements are copied too
    QList<QGraphicsItem *> items = selection;
    QSet<QString> uids;
    for (QGraphicsItem *it : selection)
        if (!PlanScene::uidOf(it).isEmpty())
            uids.insert(PlanScene::uidOf(it));
    if (!selection.isEmpty())
        if (auto *ps = qobject_cast<PlanScene *>(selection.first()->scene())) {
            for (WireItem *w : ps->wires())
                if (!items.contains(w) && uids.contains(w->fromUid()) && uids.contains(w->toUid()))
                    items << w;
            for (GroupItem *g : ps->groups()) {
                bool any = false;
                for (const GroupItem::Link &l : g->links())
                    any |= uids.contains(l.first) && uids.contains(l.second);
                if (!items.contains(g) && any)
                    items << g;
            }
        }
    QDomDocument doc;
    QDomElement root = doc.createElement("Clipboard");
    doc.appendChild(root);
    root.appendChild(writeContent(doc, items));
    return doc.toByteArray(-1);
}

QList<QGraphicsItem *> itemsFromClipboard(PlanScene *scene, const QByteArray &data, const QPointF &offset)
{
    QDomDocument doc;
    if (!doc.setContent(data))
        return {};
    return readContent(scene, doc.documentElement().firstChildElement("Content"), offset, true);
}

} // namespace SceneSerializer
