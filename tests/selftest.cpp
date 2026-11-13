// Interaction tests: real mouse events on the view (QTest), undo/redo, save/load, filters, calibration.
#include "core/Geometry.h"
#include "core/Library.h"
#include "core/Project.h"
#include "ui/MainWindow.h"
#include "ui/ProjectTree.h"
#include "ui/SectionPanel.h"
#include "ui/ExportDialog.h"
#include "scene/BackgroundItem.h"
#include "scene/DeviceItem.h"
#include "scene/EffectItem.h"
#include "scene/GroupItem.h"
#include "scene/HandleItem.h"
#include "scene/PanelItems.h"
#include "scene/PlanScene.h"
#include "scene/PlanView.h"
#include "scene/RoomItem.h"
#include "scene/SceneSerializer.h"
#include "scene/StripItem.h"
#include "scene/WallItem.h"

#include <QBuffer>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QListWidget>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QtTest>

class SelfTest : public QObject
{
    Q_OBJECT
    Project *project = nullptr;
    PlanScene *scene = nullptr;
    PlanView *view = nullptr;

    QPoint vp(double x, double y) { return view->mapFromScene(QPointF(x, y)); }
    void click(double x, double y) { QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, vp(x, y)); }
    void dclick(double x, double y) { QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, vp(x, y)); }
    const LibraryEntry *find(DeviceKind k)
    {
        for (const LibraryEntry *e : Library::instance().entries())
            if (e->kind == k && e->variant.isEmpty())
                return e;
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(Library::instance().loadAll() > 100);
        QVERIFY(Library::instance().sections().size() >= 10);
        project = new Project;
        scene = project->current();
        QCOMPARE(project->floorCount(), 1);
        view = new PlanView(scene);
        view->resize(1200, 900);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view));
        view->resetTransform();
        view->scale(1, 1);
        view->centerOn(400, 300);
        scene->setViewScale(1);
    }

    void drawWall()
    {
        scene->setTool(PlanScene::WallTool);
        click(0, 0);
        click(600, 0);
        click(600, 400);
        dclick(600, 400);
        QCOMPARE(scene->walls().size(), 1);
        WallItem *w = scene->walls().first();
        QCOMPARE(w->scenePoints().size(), 3);
        QVERIFY(qAbs(w->length() - 1000) < 1);
        QVERIFY(!w->obstacleEdges().isEmpty());
    }

    void drawRoomAndArea()
    {
        // inside the wall drawn by drawWall(): the corners snap to the inner faces (10 cm from the centre line)
        scene->setTool(PlanScene::RoomTool);
        click(12, 12);
        click(588, 12);
        click(588, 388);
        click(250, 300);
        click(12, 12);               // click the first point closes the room
        QCOMPARE(scene->rooms().size(), 1);
        // the wall is open at (0,0): there the first point lands on the inner face (y = 10),
        // (588,12) is the inner corner (590,10), (588,388) lands on the inner face x = 590
        const QPolygonF rp = scene->rooms().first()->scenePoints();
        QVERIFY(qAbs(rp[0].y() - 10) < 0.01);
        QVERIFY(QLineF(rp[1], QPointF(590, 10)).length() < 0.01);
        QVERIFY(qAbs(rp[2].x() - 590) < 0.01);
        scene->undoStack()->undo();
        QCOMPARE(scene->rooms().size(), 0);
        scene->setSnapEnabled(false);
        scene->setTool(PlanScene::RoomTool);
        click(0, 0);
        click(400, 0);
        click(400, 300);
        click(0, 300);
        click(0, 0);                 // click the first point closes the room
        scene->setSnapEnabled(true);
        QCOMPARE(scene->rooms().size(), 1);
        QVERIFY(qAbs(scene->rooms().first()->area() - 120000) < 1);   // 4 x 3 m = 12 m²
        QCOMPARE(Geo::formatArea(scene->rooms().first()->area()), QStringLiteral("12.00 m²"));
    }

    void placeLightAndToggle()
    {
        scene->setTool(PlanScene::SelectTool);
        const LibraryEntry *e = find(DeviceKind::Light);
        QVERIFY(e);
        DeviceItem *d = scene->addDevice(*e, QPointF(200, 150));
        QVERIFY(d);
        QVERIFY(d->data().id.startsWith("L"));
        QVERIFY(d->effectItem());
        QVERIFY(!d->data().on);
        dclick(200, 150);
        QVERIFY(d->data().on);
        // radius: 36° at 270 cm -> 270 * tan(18°) = 87.7 cm
        d->dataRef().beam = 36;
        QVERIFY(qAbs(d->effectRadius() - 87.73) < 0.1);
    }

    void wallBlocksLight()
    {
        // light right next to the wall (x=600 is the wall), big radius: nothing behind the wall
        const LibraryEntry *e = find(DeviceKind::Light);
        DeviceItem *d = scene->addDevice(*e, QPointF(550, 200));
        d->dataRef().beam = 150;
        d->refresh();
        const QPolygonF poly = d->effectItem()->visibleArea();
        QVERIFY(poly.size() > 10);
        QVERIFY(poly.containsPoint(QPointF(450, 200), Qt::OddEvenFill));        // same side: lit
        QVERIFY(!poly.containsPoint(QPointF(700, 200), Qt::OddEvenFill));       // behind the wall: dark
        QVERIFY(poly.containsPoint(QPointF(620, 700), Qt::OddEvenFill));        // around the free wall end: lit
        scene->clearSelection();
        d->setSelected(true);
        scene->deleteSelection();
    }

    void undoRedo()
    {
        const int before = scene->devices().size();
        const LibraryEntry *e = find(DeviceKind::Speaker);
        QVERIFY(e);
        scene->addDevice(*e, QPointF(100, 100));
        QCOMPARE(scene->devices().size(), before + 1);
        scene->undoStack()->undo();
        QCOMPARE(scene->devices().size(), before);
        scene->undoStack()->redo();
        QCOMPARE(scene->devices().size(), before + 1);
    }

    void moveWithMouseIsUndoable()
    {
        scene->setTool(PlanScene::SelectTool);
        scene->clearSelection();
        DeviceItem *d = nullptr;
        for (DeviceItem *x : scene->devices())
            if (x->isSpeaker()) d = x;
        QVERIFY(d);
        const QPointF start = d->pos();
        QTest::mousePress(view->viewport(), Qt::LeftButton, {}, vp(start.x(), start.y()));
        QTest::mouseMove(view->viewport(), vp(start.x() + 50, start.y() + 20));
        QTest::mouseMove(view->viewport(), vp(start.x() + 100, start.y() + 40));
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, {}, vp(start.x() + 100, start.y() + 40));
        QVERIFY(QLineF(d->pos(), start).length() > 50);
        scene->undoStack()->undo();
        bool back = false;
        for (DeviceItem *x : scene->devices())
            if (x->isSpeaker() && QLineF(x->pos(), start).length() < 1) back = true;
        QVERIFY(back);
    }

    void stripTools()
    {
        const LibraryEntry *e = find(DeviceKind::Strip);
        QVERIFY(e);
        scene->setStripEntry(e->key);
        scene->setTool(PlanScene::StripTool);
        click(50, 350);
        click(350, 350);
        dclick(350, 350);
        scene->setTool(PlanScene::StripCircleTool);
        click(200, 150);
        click(260, 150);
        QCOMPARE(scene->strips().size(), 2);
        StripItem *line = nullptr, *circle = nullptr;
        for (StripItem *s : scene->strips())
            (s->mode() == StripItem::Circle ? circle : line) = s;
        QVERIFY(line && circle);
        QVERIFY(qAbs(line->length() - 300) < 1);
        QVERIFY(qAbs(circle->circleRadius() - 60) < 1);
        scene->setTool(PlanScene::SelectTool);
    }

    void sectionFilter()
    {
        DeviceItem *light = nullptr;
        for (DeviceItem *d : scene->devices())
            if (d->isLight()) light = d;
        QVERIFY(light && light->isVisible());
        scene->setSectionVisible("lighting", false);
        QVERIFY(!light->isVisible());
        QVERIFY(!light->effectItem()->isVisible());
        for (StripItem *s : scene->strips())
            QVERIFY(!s->isVisible());
        scene->setSectionVisible("lighting", true);
        QVERIFY(light->isVisible());
    }

    void copyPaste()
    {
        const int n = scene->devices().size();
        scene->clearSelection();
        scene->devices().first()->setSelected(true);
        scene->copySelection();
        scene->paste(QPointF(450, 300));
        QCOMPARE(scene->devices().size(), n + 1);
        QSet<QString> ids;
        for (DeviceItem *d : scene->devices())
            ids.insert(d->data().id);
        QCOMPARE(ids.size(), scene->devices().size());     // IDs stay unique
    }

    void saveLoadRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("t.shp");
        // a small background image
        QImage img(200, 100, QImage::Format_RGB32);
        img.fill(Qt::gray);
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
        QVERIFY(scene->setBackgroundImage(png));
        project->info().name = QStringLiteral("Test ăîșțâ");
        const QString before = SceneSerializer::snapshot(scene);
        QVERIFY(SceneSerializer::saveProject(project, path));
        Project other;
        QVERIFY(SceneSerializer::loadProject(&other, path));
        QCOMPARE(other.floorCount(), 1);
        QCOMPARE(SceneSerializer::snapshot(other.floor(0)), before);
        QCOMPARE(other.info().name, QStringLiteral("Test ăîșțâ"));
        QVERIFY(other.floor(0)->background());
        QCOMPARE(other.floor(0)->background()->pixmap().width(), 200);
    }

    void multipleFloors()
    {
        PlanScene *up = project->addFloor(QStringLiteral("Etaj 1"));
        QCOMPARE(project->floorCount(), 2);
        QCOMPARE(up->settings().ceilingHeight, scene->settings().ceilingHeight);
        const LibraryEntry *e = find(DeviceKind::Light);
        up->addDevice(*e, QPointF(10, 10));
        up->addDevice(*e, QPointF(20, 10));
        QCOMPARE(up->devices().size(), 2);
        QVERIFY(up->devices().first()->data().id == "L1");      // IDs are per floor
        project->setCurrent(1);
        QCOMPARE(project->undoGroup()->activeStack(), up->undoStack());
        project->undoGroup()->undo();
        QCOMPARE(up->devices().size(), 1);
        project->undoGroup()->redo();

        QTemporaryDir dir;
        const QString path = dir.filePath("floors.shp");
        QVERIFY(SceneSerializer::saveProject(project, path));
        Project other;
        QVERIFY(SceneSerializer::loadProject(&other, path));
        QCOMPARE(other.floorCount(), 2);
        QCOMPARE(other.floor(1)->floorName(), QStringLiteral("Etaj 1"));
        QCOMPARE(other.floor(1)->devices().size(), 2);
        QCOMPARE(other.floor(0)->devices().size(), scene->devices().size());

        // the tree shows: project > floors > rooms / outside > chapters > elements
        ProjectTree tree(&other);
        QTreeWidgetItem *root = tree.topLevelItem(0);
        QVERIFY(root);
        QCOMPARE(root->childCount(), 3);                       // 2 floors + "add floor"
        QCOMPARE(root->child(1)->text(0), QStringLiteral("Etaj 1"));
        QCOMPARE(root->child(1)->text(1), QStringLiteral("2"));
        QTreeWidgetItem *ground = root->child(0);
        bool roomFound = false;
        for (int i = 0; i < ground->childCount(); ++i)
            if (ground->child(i)->text(1).endsWith(QStringLiteral("m²")))
                roomFound = true;
        QVERIFY(roomFound);

        other.moveFloor(1, -1);
        QCOMPARE(other.floor(0)->floorName(), QStringLiteral("Etaj 1"));
        other.removeFloor(0);
        QCOMPARE(other.floorCount(), 1);
        project->setCurrent(0);
    }

    void calibration()
    {
        // two points 100 units apart that are really 2 m -> everything doubles
        const double len = scene->walls().first()->length();
        scene->applyCalibration(QPointF(0, 0), QPointF(100, 0), 200, true);
        QVERIFY(qAbs(scene->walls().first()->length() - 2 * len) < 1);
        QVERIFY(qAbs(scene->rooms().first()->area() - 4 * 120000) < 10);
        scene->undoStack()->undo();
        QVERIFY(qAbs(scene->walls().first()->length() - len) < 1);
    }

    void renumber()
    {
        scene->renumberIds();
        QStringList ids;
        for (DeviceItem *d : scene->devices())
            if (d->data().section == "lighting") ids << d->data().id;
        for (StripItem *s : scene->strips())
            ids << s->data().id;
        ids.sort();
        QVERIFY(ids.contains("L1"));
    }

    void roomSnapsToWallFaces()
    {
        Project p;
        PlanScene *s = p.current();
        s->setViewScale(1);
        auto *w = new WallItem;
        w->setThickness(20);
        w->setPoints(QPolygonF{{0, 0}, {600, 0}, {600, 400}, {0, 400}});
        w->setClosed(true);
        s->addItem(w);
        s->notifyWallsChanged();
        PlanScene::SnapKind k;
        // near the inner corner -> exactly on the inner face corner (10, 10), not on the centre line (0, 0)
        QPointF q = s->snapPoint(QPointF(14, 7), nullptr, true, true, &k);
        QCOMPARE(k, PlanScene::CornerSnap);
        QVERIFY(QLineF(q, QPointF(10, 10)).length() < 0.01);
        // near the inner face in the middle of a wall -> projected on the face
        q = s->snapPoint(QPointF(300, 15), nullptr, true, true, &k);
        QCOMPARE(k, PlanScene::EdgeSnap);
        QVERIFY(qAbs(q.y() - 10) < 0.01 && qAbs(q.x() - 300) < 0.01);
        // outer face corner also available
        q = s->snapPoint(QPointF(605, 407), nullptr, true, true, &k);
        QVERIFY(QLineF(q, QPointF(610, 410)).length() < 0.01);
        // free point: no grid snap by default
        q = s->snapPoint(QPointF(303.3, 201.7), nullptr, true, true, &k);
        QCOMPARE(k, PlanScene::NoSnap);
        QCOMPARE(q, QPointF(303.3, 201.7));
        s->setGridSnapEnabled(true);
        q = s->snapPoint(QPointF(303.3, 201.7), nullptr, true, true, &k);
        QCOMPARE(q, QPointF(300, 200));
    }

    void calibrateDoesNotSnap()
    {
        QSignalSpy spy(scene, &PlanScene::calibrationPicked);
        scene->setTool(PlanScene::CalibrateTool);
        click(103, 207);
        click(853, 207);
        QCOMPARE(spy.count(), 1);
        const QPointF a = spy.at(0).at(0).toPointF(), b = spy.at(0).at(1).toPointF();
        QVERIFY(qAbs(a.x() - 103) < 1.5 && qAbs(a.y() - 207) < 1.5);   // not snapped to the 10 cm grid
        QVERIFY(qAbs(QLineF(a, b).length() - 750) < 2);
        scene->setTool(PlanScene::SelectTool);
    }

    void navigation()
    {
        scene->setTool(PlanScene::SelectTool);
        view->resetTransform();
        view->zoomBy(1.0);
        const QPoint c = view->viewport()->rect().center();
        const QPointF g = view->viewport()->mapToGlobal(QPointF(c));
        // classic mouse wheel: zoom in
        {
            QWheelEvent w(QPointF(c), g, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(view->viewport(), &w);
        }
        QVERIFY(view->zoom() > 1.1);
        // macOS mouse wheel: may carry a pixel delta but no scroll phase -> still zoom
        {
            const double z0 = view->zoom();
            QWheelEvent w(QPointF(c), g, QPoint(0, 10), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(view->viewport(), &w);
            QVERIFY(view->zoom() > z0 * 1.1);
        }
        // trackpad two-finger scroll: pan, no zoom
        const double z = view->zoom();
        const int h0 = view->horizontalScrollBar()->value(), v0 = view->verticalScrollBar()->value();
        {
            QWheelEvent w(QPointF(c), g, QPoint(-40, -30), QPoint(-40, -30), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
            QCoreApplication::sendEvent(view->viewport(), &w);
        }
        QCOMPARE(view->zoom(), z);
        QCOMPARE(view->horizontalScrollBar()->value(), h0 + 40);
        QCOMPARE(view->verticalScrollBar()->value(), v0 + 30);
        // pinch: zoom, the point under the fingers stays in place
        const QPointF before = view->mapToScene(c);
        {
            QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, QPointF(c),
                                      QPointF(c), g, 0.5, QPointF(), 0);
            QCoreApplication::sendEvent(view->viewport(), &pinch);
        }
        QVERIFY(qAbs(view->zoom() - z * 1.5) < 1e-6);
        QVERIFY(QLineF(view->mapToScene(c), before).length() < 2.0 / view->zoom());
        // right-button drag on an empty spot pans
        const int h1 = view->horizontalScrollBar()->value();
        QTest::mousePress(view->viewport(), Qt::RightButton, {}, QPoint(20, 20));
        QMouseEvent mv(QEvent::MouseMove, QPointF(120, 20), view->viewport()->mapToGlobal(QPointF(120, 20)),
                       Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &mv);
        QTest::mouseRelease(view->viewport(), Qt::RightButton, {}, QPoint(120, 20));
        QCOMPARE(view->horizontalScrollBar()->value(), h1 - 100);
        view->resetTransform();
        view->scale(1, 1);
        view->centerOn(400, 300);
        scene->setViewScale(1);
    }

    void editRoomOutline()
    {
        Project p;
        PlanScene *s = p.current();
        PlanView v(s);
        v.resize(800, 600);
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.resetTransform();
        v.centerOn(200, 150);
        s->setViewScale(1);
        auto *r = new RoomItem;
        r->setPoints(QPolygonF{{0, 0}, {400, 0}, {400, 300}, {0, 300}});
        s->addItem(r);
        s->commit("room");
        auto at = [&](double x, double y) { return v.mapFromScene(QPointF(x, y)); };
        auto drag = [&](QPointF a, QPointF b) {
            QTest::mousePress(v.viewport(), Qt::LeftButton, {}, at(a.x(), a.y()));
            for (int i = 1; i <= 5; ++i) {
                const QPointF m = a + (b - a) * (i / 5.0);
                QTest::mouseMove(v.viewport(), at(m.x(), m.y()));
            }
            QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, at(b.x(), b.y()));
        };

        // Select tool: the room can be neither selected nor dragged
        QCOMPARE(s->tool(), PlanScene::SelectTool);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(200, 150));
        QVERIFY(!r->isSelected());
        drag({200, 150}, {260, 190});
        QCOMPARE(r->pos(), QPointF(0, 0));
        QCOMPARE(r->scenePoints().at(1), QPointF(400, 0));

        // Room tool: click inside selects it, nothing is drawn
        s->setTool(PlanScene::RoomTool);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(200, 150));
        QVERIFY(r->isSelected());
        QVERIFY(!s->isDrawing());

        // dragging a corner moves only that corner, not the whole room
        drag({400, 0}, {450, -40});
        QCOMPARE(r->pos(), QPointF(0, 0));
        QVERIFY(QLineF(r->scenePoints().at(1), QPointF(450, -40)).length() < 1.5);
        QCOMPARE(r->scenePoints().at(0), QPointF(0, 0));
        QCOMPARE(r->scenePoints().at(2), QPointF(400, 300));
        QCOMPARE(s->undoStack()->undoText(), QStringLiteral("Edit points"));
        s->undoStack()->undo();
        r = s->rooms().first();
        QCOMPARE(r->scenePoints().at(1), QPointF(400, 0));

        // double-click on the bottom edge of the selected room -> new corner
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(200, 150));
        QVERIFY(r->isSelected());
        QTest::mouseDClick(v.viewport(), Qt::LeftButton, {}, at(200, 300));
        QCOMPARE(r->points().size(), 5);
        QVERIFY(!s->isDrawing());
        QCOMPARE(r->area(), 400.0 * 300.0);                 // corners on edges keep the area
        s->undoStack()->undo();
        QCOMPARE(s->rooms().first()->points().size(), 4);
        s->undoStack()->redo();
        r = s->rooms().first();
        QCOMPARE(r->points().size(), 5);
        QVERIFY(!r->insertPointAt(QPointF(200, 150), 5));   // inside, not on an edge

        // double-click on a corner handle deletes that corner
        r->setSelected(true);
        QTest::mouseDClick(v.viewport(), Qt::LeftButton, {}, at(400, 300));
        QTest::qWait(20);
        QCOMPARE(r->points().size(), 4);
        QCOMPARE(r->area(), 400.0 * 300.0 / 2 + 200.0 * 300.0 / 2);  // (400,300) removed

        // clicking outside every room starts a new room
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(500, 100));
        QVERIFY(s->isDrawing());
        QVERIFY(!r->isSelected());
        s->cancelDrawing();

        // room labels can be hidden on screen
        QVERIFY(r->labelText().contains(r->name()));
        s->setShowRoomAreas(false);
        QVERIFY(!r->labelText().contains(QStringLiteral("m²")));
        s->setShowRoomNames(false);
        QVERIFY(r->labelText().isEmpty());
        s->setExporting(true);
        QVERIFY(r->labelText().contains(QStringLiteral("m²")));
        s->setExporting(false);

        // minimum 3 corners
        while (r->canRemovePoint())
            QVERIFY(r->removePoint(0));
        QCOMPARE(r->points().size(), 3);
        QVERIFY(!r->removePoint(0));
    }

    void editWallOnlyWithWallTool()
    {
        Project p;
        PlanScene *s = p.current();
        PlanView v(s);
        v.resize(800, 600);
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.resetTransform();
        v.centerOn(200, 150);
        s->setViewScale(1);
        auto *w = new WallItem;
        w->setThickness(20);
        w->setPoints(QPolygonF{{0, 0}, {400, 0}});
        s->addItem(w);
        s->commit("wall");
        auto at = [&](double x, double y) { return v.mapFromScene(QPointF(x, y)); };
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(200, 0));
        QVERIFY(!w->isSelected());
        s->setTool(PlanScene::WallTool);
        QTest::mouseDClick(v.viewport(), Qt::LeftButton, {}, at(200, 3));
        QVERIFY(w->isSelected());
        QVERIFY(!s->isDrawing());
        // drag the end point
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, at(400, 0));
        for (int i = 1; i <= 4; ++i)
            QTest::mouseMove(v.viewport(), at(400, 25.0 * i));
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, at(400, 100));
        QVERIFY(QLineF(w->scenePoints().at(1), QPointF(400, 100)).length() < 1.5);
        QCOMPARE(w->scenePoints().at(0), QPointF(0, 0));
        // back to Select: the wall is deselected and not selectable
        s->setTool(PlanScene::SelectTool);
        QVERIFY(!w->isSelected());
        QVERIFY(!(w->flags() & QGraphicsItem::ItemIsSelectable));
    }

    void wallStartsAtClickAndOneClickEdits()
    {
        Project p;
        PlanScene *s = p.current();
        PlanView v(s);
        v.resize(800, 600);
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.resetTransform();
        v.centerOn(200, 150);
        s->setViewScale(1);
        auto at = [&](double x, double y) { return v.mapFromScene(QPointF(x, y)); };
        s->setTool(PlanScene::WallTool);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(0, 0));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(400, 0));
        QTest::mouseDClick(v.viewport(), Qt::LeftButton, {}, at(400, 0));
        QCOMPARE(s->walls().size(), 1);
        WallItem *w = s->walls().first();
        // the body starts exactly at the first click (flat), the end goes half a thickness further
        const QRectF b = w->mapToScene(w->shape()).boundingRect();
        QVERIFY(qAbs(b.left()) < 0.01);
        QVERIFY(qAbs(b.right() - 410) < 0.01);
        QTest::qWait(600);
        // one click on the wall selects it (no new wall), its end can be dragged
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(200, 3));
        QVERIFY(w->isSelected());
        QVERIFY(!s->isDrawing());
        QTest::qWait(600);
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, at(400, 0));
        for (int i = 1; i <= 4; ++i)
            QTest::mouseMove(v.viewport(), at(400, 25.0 * i));
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, at(400, 100));
        QVERIFY(QLineF(w->scenePoints().at(1), QPointF(400, 100)).length() < 1.5);
        QCOMPARE(s->walls().size(), 1);
        QTest::qWait(600);
        // Ctrl + click on the wall starts a new one
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::ControlModifier, at(100, 0));
        QVERIFY(s->isDrawing());
        s->cancelDrawing();
    }

    void groupsLinkElements()
    {
        Project p;
        PlanScene *s = p.current();
        const LibraryEntry *light = find(DeviceKind::Light);
        QVERIFY(light);
        DeviceItem *a = s->addDevice(*light, QPointF(100, 100));
        DeviceItem *b = s->addDevice(*light, QPointF(300, 100));
        DeviceItem *c = s->addDevice(*light, QPointF(300, 300));
        QVERIFY(a->data().uid != b->data().uid);
        s->clearSelection();
        a->setSelected(true);
        b->setSelected(true);
        c->setSelected(true);
        GroupItem *g = s->groupSelection();
        QVERIFY(g);
        QCOMPARE(g->memberItems().size(), 3);
        QVERIFY(!g->boundingRect().isEmpty());
        QCOMPARE(s->groupsOf(b->data().uid).size(), 1);
        // the line follows a moved element
        const QRectF before = g->boundingRect();
        c->setPos(500, 600);
        s->updateLinks();
        QVERIFY(g->boundingRect() != before);
        QVERIFY(g->boundingRect().bottom() > 560);

        // save / load keeps the group and its members
        QTemporaryDir dir;
        const QString path = dir.filePath("g.shp");
        QVERIFY(SceneSerializer::saveProject(&p, path));
        Project q;
        QVERIFY(SceneSerializer::loadProject(&q, path));
        PlanScene *s2 = q.current();
        QCOMPARE(s2->groups().size(), 1);
        QCOMPARE(s2->groups().first()->memberItems().size(), 3);
        QCOMPARE(s2->groups().first()->color(), g->color());

        // copy / paste of two members: a new group between the copies, originals untouched
        s->clearSelection();
        a->setSelected(true);
        b->setSelected(true);
        s->copySelection();
        s->paste(QPointF(1000, 1000));
        QCOMPARE(s->groups().size(), 2);
        GroupItem *pasted = s->groups().last() == g ? s->groups().first() : s->groups().last();
        QCOMPARE(pasted->memberItems().size(), 2);
        for (QGraphicsItem *m : pasted->memberItems())
            QVERIFY(m != a && m != b);

        // removing a member below 2 deletes the group
        const QString uid = pasted->members().first();
        s->removeFromGroup(pasted, uid);
        QCOMPARE(s->groups().size(), 1);

        // undo brings it back
        s->undoStack()->undo();
        QCOMPARE(s->groups().size(), 2);
    }

    void panelEquipmentAndWires()
    {
        Project p;
        PlanScene *s = p.addFloor(QStringLiteral("Tablou"), PlanScene::PanelPage);
        QVERIFY(s->isPanel());
        const LibraryEntry *mcb = nullptr;
        const LibraryEntry *psu = nullptr;
        for (const LibraryEntry *e : Library::instance().entries()) {
            if (e->name == QLatin1String("MCB 1p")) mcb = e;
            if (e->name == QLatin1String("Power supply 24V DC")) psu = e;
        }
        QVERIFY(mcb && psu);
        QVERIFY(mcb->isPanelPart());
        QCOMPARE(mcb->ports.size(), 2);
        QVERIFY(qAbs(mcb->height - 86) < 1);            // from the picture's aspect ratio
        QCOMPARE(psu->portNames.value(0), QStringLiteral("L"));

        RailItem *rail = s->addRail(QPointF(0, 0), 450);
        // dropped near the rail -> snaps onto it
        EquipmentItem *q1 = s->addEquipment(*mcb, QPointF(40, 30));
        EquipmentItem *t1 = s->addEquipment(*psu, QPointF(200, 10));
        QCOMPARE(q1->sceneBoundingRect().center().y(), q1->sceneBoundingRect().center().y());
        QVERIFY(qAbs(q1->mapToScene(QRectF(QPointF(), q1->size())).boundingRect().center().y() - rail->centerY()) < 0.01);
        QCOMPARE(q1->data().id, QStringLiteral("Q1"));
        QCOMPARE(t1->data().id, QStringLiteral("T1"));

        WireItem *w = s->addWire(q1, 1, t1, 0);
        QVERIFY(w);
        QCOMPARE(w->endText(true), QStringLiteral("Q1 · 2"));
        QCOMPARE(w->endText(false), QStringLiteral("T1 · L"));
        QVERIFY(!w->boundingRect().isEmpty());

        // selecting the equipment highlights its wire and the equipment at the other end
        EquipmentItem *other = s->addEquipment(*mcb, QPointF(400, 30));
        s->clearSelection();
        q1->setSelected(true);
        QCOMPARE(q1->linkState(), LinkState::Selected);
        QCOMPARE(t1->linkState(), LinkState::Linked);
        QCOMPARE(other->linkState(), LinkState::Dim);
        s->clearSelection();
        QCOMPARE(t1->linkState(), LinkState::Normal);

        // save / load
        QTemporaryDir dir;
        const QString path = dir.filePath("panel.shp");
        QVERIFY(SceneSerializer::saveProject(&p, path));
        Project q;
        QVERIFY(SceneSerializer::loadProject(&q, path));
        QCOMPARE(q.floorCount(), 2);
        PlanScene *s2 = q.floor(1);
        QVERIFY(s2->isPanel());
        QCOMPARE(s2->rails().size(), 1);
        QCOMPARE(s2->equipment().size(), 3);
        QCOMPARE(s2->wires().size(), 1);
        QCOMPARE(s2->wires().first()->endText(false), QStringLiteral("T1 · L"));

        // deleting equipment removes its wires
        s->clearSelection();
        t1->setSelected(true);
        s->deleteSelection();
        QCOMPARE(s->wires().size(), 0);
        s->undoStack()->undo();
        QCOMPARE(s->wires().size(), 1);
        QCOMPARE(s->equipment().size(), 3);

        // wire tool: click a terminal, then the other one
        s->setViewScale(1);
        s->setTool(PlanScene::WireTool);
        EquipmentItem *qa = s->equipment().at(0);
        EquipmentItem *qb = s->equipment().at(2);
        QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
        press.setButton(Qt::LeftButton);
        press.setScenePos(qa->portScenePos(0));
        QApplication::sendEvent(s, &press);
        QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
        press2.setButton(Qt::LeftButton);
        press2.setScenePos(qb->portScenePos(0));
        QApplication::sendEvent(s, &press2);
        QCOMPARE(s->wires().size(), 2);
    }

    void wiresAreOrthogonalAndEditable()
    {
        Project p;
        PlanScene *s = p.addFloor(QStringLiteral("Tablou"), PlanScene::PanelPage);
        const LibraryEntry *mcb = nullptr;
        for (const LibraryEntry *e : Library::instance().entries())
            if (e->name == QLatin1String("MCB 1p")) mcb = e;
        QVERIFY(mcb);
        s->addRail(QPointF(0, 0), 450);
        s->addRail(QPointF(0, 200), 450);
        EquipmentItem *a = s->addEquipment(*mcb, QPointF(40, 17));
        EquipmentItem *b = s->addEquipment(*mcb, QPointF(300, 217));
        WireItem *w = s->addWire(a, 1, b, 0);      // bottom terminal of a -> top terminal of b
        QVERIFY(w);
        auto orthogonal = [](const QPolygonF &r) {
            for (int i = 0; i + 1 < r.size(); ++i)
                if (std::abs(r[i].x() - r[i + 1].x()) > 0.01 && std::abs(r[i].y() - r[i + 1].y()) > 0.01)
                    return false;
            return true;
        };
        QPolygonF r = w->routePoints();
        QVERIFY(r.size() >= 3);
        QVERIFY(orthogonal(r));
        QCOMPARE(r.first(), a->portScenePos(1));
        QCOMPARE(r.last(), b->portScenePos(0));
        QVERIFY(!w->isManual());

        // move the horizontal segment down by 20 mm
        w->beginEdit();
        const int seg = 1;      // corner 1 -> corner 2
        const QPointF mid = (r[1] + r[2]) / 2;
        w->moveSegment(seg, mid + QPointF(0, 20));
        w->endEdit();
        QVERIFY(w->isManual());
        r = w->routePoints();
        QVERIFY(orthogonal(r));
        QCOMPARE(r[1].y(), std::round(mid.y() + 20));

        // move a corner: still right angles
        w->beginEdit();
        w->moveCorner(1, r[1] + QPointF(15, 7));
        w->endEdit();
        QVERIFY(orthogonal(w->routePoints()));

        // the equipment moves: the route stays orthogonal
        b->setPos(b->pos() + QPointF(36, 0));
        s->updateLinks();
        QVERIFY(orthogonal(w->routePoints()));
        QCOMPARE(w->routePoints().last(), b->portScenePos(0));

        // save / load keeps the manual corners
        const QPolygonF before = w->routePoints();
        QTemporaryDir dir;
        const QString path = dir.filePath("w.shp");
        QVERIFY(SceneSerializer::saveProject(&p, path));
        Project q;
        QVERIFY(SceneSerializer::loadProject(&q, path));
        WireItem *w2 = q.floor(1)->wires().first();
        QVERIFY(w2->isManual());
        QCOMPARE(w2->routePoints(), before);

        // back to automatic
        w->resetRoute();
        QVERIFY(!w->isManual());
        QVERIFY(orthogonal(w->routePoints()));
    }

    void chapterFilterCheckBoxes()
    {
        Project p;
        PlanScene *s = p.current();
        SectionPanel panel(s);
        panel.resize(300, 500);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *list = panel.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), Library::instance().sections().size());
        // every chapter has its own box; clicking the box of the 3rd chapter hides only that one
        QListWidgetItem *it = list->item(2);
        const QString id = it->data(Qt::UserRole + 1).toString();
        QVERIFY(s->isSectionVisible(id));
        const QRect r = list->visualItemRect(it);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, QPoint(r.left() + 13, r.center().y()));
        QVERIFY(!s->isSectionVisible(id));
        QCOMPARE(it->checkState(), Qt::Unchecked);
        QVERIFY(s->isSectionVisible(list->item(0)->data(Qt::UserRole + 1).toString()));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, QPoint(r.left() + 13, r.center().y()));
        QVERIFY(s->isSectionVisible(id));
        // the boxes are drawn for every row
        const QImage img = list->viewport()->grab().toImage();
        for (int i = 0; i < list->count(); ++i) {
            const QRect ri = list->visualItemRect(list->item(i));
            const QColor c = img.pixelColor(ri.left() + 14, ri.center().y() - 5);
            QVERIFY2(c.blue() > 200 && c.red() < 100, qPrintable(QStringLiteral("row %1").arg(i)));
        }
    }

    void groupToolLinksTwoByTwo()
    {
        Project p;
        PlanScene *s = p.current();
        s->setViewScale(1);
        const LibraryEntry *light = find(DeviceKind::Light);
        DeviceItem *sw = s->addDevice(*light, QPointF(0, 0));          // e.g. the switch
        DeviceItem *l1 = s->addDevice(*light, QPointF(300, 0));
        DeviceItem *l2 = s->addDevice(*light, QPointF(0, 300));
        DeviceItem *l3 = s->addDevice(*light, QPointF(-300, 0));
        DeviceItem *other = s->addDevice(*light, QPointF(600, 600));
        s->setTool(PlanScene::GroupTool);
        auto click = [&](const QPointF &at, Qt::KeyboardModifiers mods = {}) {
            QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
            e.setButton(Qt::LeftButton);
            e.setScenePos(at);
            e.setModifiers(mods);
            QApplication::sendEvent(s, &e);
            QGraphicsSceneMouseEvent r(QEvent::GraphicsSceneMouseRelease);
            r.setButton(Qt::LeftButton);
            r.setScenePos(at);
            QApplication::sendEvent(s, &r);
        };
        // star: switch -> l1, back to the switch, -> l2, switch -> l3
        click(sw->scenePos());
        click(l1->scenePos());
        QCOMPARE(s->groups().size(), 1);
        GroupItem *g = s->groups().first();
        QCOMPARE(g->links().size(), 1);
        click(sw->scenePos());          // already linked: continue from the switch, no new link
        QCOMPARE(g->links().size(), 1);
        click(l2->scenePos());
        click(sw->scenePos());
        click(l3->scenePos());
        QCOMPARE(g->links().size(), 3);
        for (const GroupItem::Link &l : g->links())
            QVERIFY(l.first == sw->data().uid || l.second == sw->data().uid);   // every link touches the switch
        QCOMPARE(g->memberItems().size(), 4);

        // Esc ends; Shift+click starts a new group even from a grouped element
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(s, &esc);
        QVERIFY(!s->isDrawing());
        click(l1->scenePos(), Qt::ShiftModifier);
        click(other->scenePos());
        QCOMPARE(s->groups().size(), 2);
        QCOMPARE(s->groupsOf(l1->data().uid).size(), 2);      // one element in two groups
        QVERIFY(s->groups().at(0)->color() != s->groups().at(1)->color());

        // a link can be removed; undo restores it (undo rebuilds the items: keep only the uid)
        const QString swUid = sw->data().uid;
        g->removeLink(0);
        s->commit("x");
        QCOMPARE(g->links().size(), 2);
        s->undoStack()->undo();
        GroupItem *g2 = nullptr;
        for (GroupItem *x : s->groups())
            if (x->contains(swUid))
                g2 = x;
        QVERIFY(g2);
        QCOMPARE(g2->links().size(), 3);

        // save / load keeps the star
        QTemporaryDir dir;
        QVERIFY(SceneSerializer::saveProject(&p, dir.filePath("s.shp")));
        Project q;
        QVERIFY(SceneSerializer::loadProject(&q, dir.filePath("s.shp")));
        int maxLinks = 0;
        for (GroupItem *x : q.current()->groups())
            maxLinks = std::max(maxLinks, int(x->links().size()));
        QCOMPARE(maxLinks, 3);
    }

    void treeOpensCollapsed()
    {
        Project p;
        const LibraryEntry *light = find(DeviceKind::Light);
        p.current()->addDevice(*light, QPointF(0, 0));
        PlanScene *up = p.addFloor(QStringLiteral("Etaj 1"));
        up->addDevice(*light, QPointF(0, 0));
        ProjectTree tree(&p);
        tree.resetExpansion();
        tree.rebuild();
        QTreeWidgetItem *root = tree.topLevelItem(0);
        QVERIFY(root->isExpanded());
        QVERIFY(root->childCount() >= 2);
        for (int i = 0; i < root->childCount(); ++i)
            QVERIFY2(!root->child(i)->isExpanded(), qPrintable(root->child(i)->text(0)));
        // a later rebuild keeps them as the user left them
        root->child(0)->setExpanded(true);
        tree.rebuild();
        QVERIFY(tree.topLevelItem(0)->child(0)->isExpanded());
        QVERIFY(!tree.topLevelItem(0)->child(1)->isExpanded());
    }

    void exportDialogCheckBoxes()
    {
        Project p;
        const LibraryEntry *light = find(DeviceKind::Light);
        p.current()->addDevice(*light, QPointF(0, 0));
        p.addFloor(QStringLiteral("Etaj 1"))->addDevice(*light, QPointF(0, 0));
        p.addFloor(QStringLiteral("Tablou"), PlanScene::PanelPage);
        ExportDialog dlg(&p);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QList<QListWidget *> lists = dlg.findChildren<QListWidget *>();
        QCOMPARE(lists.size(), 2);
        QListWidget *floors = lists.first()->count() == 3 ? lists.first() : lists.last();
        QCOMPARE(floors->count(), 3);
        // every row draws its box (blue when checked)
        for (QListWidget *list : lists) {
            const QImage img = list->viewport()->grab().toImage();
            for (int i = 0; i < list->count(); ++i) {
                const QRect ri = list->visualItemRect(list->item(i));
                const QColor c = img.pixelColor(ri.left() + 14, ri.center().y() - 5);
                QVERIFY2(c.blue() > 200 && c.red() < 100, qPrintable(list->item(i)->text()));
            }
        }
        // a click on the row toggles; the export options follow
        const QRect r = floors->visualItemRect(floors->item(1));
        QTest::mouseClick(floors->viewport(), Qt::LeftButton, {}, r.center());
        QCOMPARE(floors->item(1)->checkState(), Qt::Unchecked);
        QCOMPARE(dlg.options().floors, (QList<int>{0, 2}));
        QTest::mouseClick(floors->viewport(), Qt::LeftButton, {}, r.center());
        QCOMPARE(floors->item(1)->checkState(), Qt::Checked);
    }

    void libraryReloadPicksUpNewItems()
    {
        // a copy of the library with one more element, like an edited Electrical.xml
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("Electrical/Assets");
        const QString src = QStringLiteral(SHP_SOURCE_LIBRARY_DIR) + "/Electrical/Electrical.xml";
        QFile in(src);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QByteArray xml = in.readAll();
        const int at = xml.lastIndexOf("</Category>");
        xml.insert(at, "<Item name=\"Test WallBox\" icon=\"WallBox.svg\" section=\"other\" size=\"30\" note=\"5x6\" />\n");
        QFile out(dir.filePath("Electrical/Electrical.xml"));
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(xml);
        out.close();
        const int before = Library::instance().entries().size();
        qputenv("SHP_LIBRARY_PATH", dir.path().toLocal8Bit());
        const int n = Library::instance().reload();
        qunsetenv("SHP_LIBRARY_PATH");
        bool found = false;
        for (const LibraryEntry *e : Library::instance().entries())
            found |= e->name == QLatin1String("Test WallBox") && e->note == QLatin1String("5x6");
        QVERIFY(found);
        QVERIFY(n > 100);
        // back to the normal library
        Library::instance().reload();
        QCOMPARE(Library::instance().entries().size(), before);
        QVERIFY(Library::instance().errors().isEmpty());
    }

    void panelBelongsToFloor()
    {
        Project p;
        PlanScene *ground = p.current();
        PlanScene *up = p.addFloor(QStringLiteral("Etaj 1"));
        // a panel added on the ground floor comes right after it, before "Etaj 1"
        PlanScene *panel = p.addFloor(QStringLiteral("Tablou"), PlanScene::PanelPage, ground);
        QCOMPARE(panel->parentFloor(), ground);
        QCOMPARE(p.indexOf(panel), 1);
        QCOMPARE(p.indexOf(up), 2);
        QCOMPARE(p.panelsOf(ground).size(), 1);

        // the tree shows the panel inside its floor
        ProjectTree tree(&p);
        tree.resetExpansion();
        tree.rebuild();
        QTreeWidgetItem *root = tree.topLevelItem(0);
        QTreeWidgetItem *groundNode = nullptr;
        for (int i = 0; i < root->childCount(); ++i)
            if (root->child(i)->text(0) == ground->floorName())
                groundNode = root->child(i);
        QVERIFY(groundNode);
        bool inside = false;
        for (int i = 0; i < groundNode->childCount(); ++i)
            inside |= groundNode->child(i)->text(0) == QLatin1String("Tablou");
        QVERIFY(inside);
        for (int i = 0; i < root->childCount(); ++i)
            QVERIFY(root->child(i)->text(0) != QLatin1String("Tablou"));

        // move it to the upper floor; save / load keeps it
        p.setPanelFloor(panel, up);
        QCOMPARE(panel->parentFloor(), up);
        QCOMPARE(p.indexOf(panel), p.indexOf(up) + 1);
        QTemporaryDir dir;
        QVERIFY(SceneSerializer::saveProject(&p, dir.filePath("pf.shp")));
        Project q;
        QVERIFY(SceneSerializer::loadProject(&q, dir.filePath("pf.shp")));
        PlanScene *qp = nullptr;
        for (PlanScene *s : q.floors())
            if (s->isPanel())
                qp = s;
        QVERIFY(qp);
        QVERIFY(qp->parentFloor());
        QCOMPARE(qp->parentFloor()->floorName(), QStringLiteral("Etaj 1"));

        // deleting the floor keeps the panel, without floor
        p.removeFloor(p.indexOf(up));
        QVERIFY(!panel->parentFloor());
        QVERIFY(p.floors().contains(panel));
    }

    void reopensLastProject()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("last.shp");
        {
            Project p;
            p.info().name = QStringLiteral("Ultimul");
            QVERIFY(SceneSerializer::saveProject(&p, path));
        }
        QSettings st;
        const QVariant oldRecent = st.value("recent");
        const QVariant oldReopen = st.value("reopenLast");
        st.setValue("reopenLast", true);
        st.setValue("recent", QStringList{path});
        {
            MainWindow w;
            QVERIFY(w.openLastProject());
            QCOMPARE(w.project()->info().name, QStringLiteral("Ultimul"));
        }
        // gone -> stays with a new project, no error dialog
        st.setValue("recent", QStringList{dir.filePath("missing.shp")});
        {
            MainWindow w;
            QVERIFY(!w.openLastProject());
            QCOMPARE(w.project()->floorCount(), 1);
        }
        // switched off
        st.setValue("recent", QStringList{path});
        st.setValue("reopenLast", false);
        {
            MainWindow w;
            QVERIFY(!w.openLastProject());
        }
        st.setValue("recent", oldRecent);
        if (oldReopen.isValid())
            st.setValue("reopenLast", oldReopen);
        else
            st.remove("reopenLast");
    }

    void symbolBackgroundDefaultAndToggle()
    {
        const LibraryEntry *svg = nullptr, *png = nullptr;
        for (const LibraryEntry *e : Library::instance().entries()) {
            if (e->isPanelPart() || e->tile >= 0)
                continue;
            if (!svg && e->iconPath.endsWith(".svg")) svg = e;
            if (!png && e->iconPath.endsWith(".png")) png = e;
        }
        QVERIFY(svg && png);
        Project p;
        PlanScene *s = p.current();
        DeviceItem *a = s->addDevice(*svg, QPointF(0, 0));
        DeviceItem *b = s->addDevice(*png, QPointF(100, 0));
        QVERIFY(a->data().tile);            // SVG: white background by default
        QVERIFY(!b->data().tile);           // picture: none
        a->dataRef().tile = false;
        s->commit("x");
        QTemporaryDir dir;
        QVERIFY(SceneSerializer::saveProject(&p, dir.filePath("t.shp")));
        Project q;
        QVERIFY(SceneSerializer::loadProject(&q, dir.filePath("t.shp")));
        int on = 0;
        for (DeviceItem *d : q.current()->devices())
            on += d->data().tile ? 1 : 0;
        QCOMPARE(on, 0);
        // XML attribute wins over the default
        QTemporaryFile f(dir.filePath("XXXXXX.xml"));
        QVERIFY(f.open());
        f.write("<Library name=\"T\"><Category name=\"C\"><Item name=\"NoTile\" icon=\"x.svg\" tile=\"0\"/>"
                "<Item name=\"Tile\" icon=\"x.png\" tile=\"1\"/></Category></Library>");
        f.close();
        QVERIFY(Library::instance().loadFile(f.fileName()));
        const LibraryEntry *nt = nullptr, *t = nullptr;
        for (const LibraryEntry *e : Library::instance().entries()) {
            if (e->name == QLatin1String("NoTile")) nt = e;
            if (e->name == QLatin1String("Tile")) t = e;
        }
        QVERIFY(nt && t);
        QVERIFY(!nt->wantsTile());
        QVERIFY(t->wantsTile());
        Library::instance().reload();
    }

    void alignmentGuides()
    {
        Project p;
        PlanScene *s = p.current();
        s->setViewScale(1);
        const LibraryEntry *light = find(DeviceKind::Light);
        DeviceItem *a = s->addDevice(*light, QPointF(100, 100));
        DeviceItem *b = s->addDevice(*light, QPointF(400, 300));
        s->clearSelection();
        // dragging a light close to the column of a / the row of b: lands exactly in line
        DeviceItem *c = s->addDevice(*light, QPointF(700, 700));
        s->clearSelection();
        c->setSelected(true);
        c->setPos(104, 296);
        QCOMPARE(c->pos(), QPointF(100, 300));
        // far from everything: free (grid)
        c->setPos(250, 520);
        QCOMPARE(c->pos(), s->snapToGrid(QPointF(250, 520)));
        // several selected: no alignment of each one
        a->setSelected(true);
        c->setPos(404, 523);
        QCOMPARE(c->pos(), s->snapToGrid(QPointF(404, 523)));
        Q_UNUSED(b);

        // drawing: the next wall point lines up with the previous one
        s->clearSelection();
        s->setTool(PlanScene::WallTool);
        auto press = [&](const QPointF &at) {
            QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
            e.setButton(Qt::LeftButton);
            e.setScenePos(at);
            QApplication::sendEvent(s, &e);
        };
        press(QPointF(1000, 1000));
        press(QPointF(1600, 1005));     // almost horizontal -> horizontal
        s->finishDrawing();
        WallItem *w = s->walls().last();
        const QPolygonF wp = w->scenePoints();
        QCOMPARE(wp.size(), 2);
        QCOMPARE(wp[1].y(), wp[0].y());

        // editing: a strip end dragged near the vertical of the other end
        s->setTool(PlanScene::SelectTool);
        const LibraryEntry *strip = find(DeviceKind::Strip);
        QVERIFY(strip);
        StripItem *st = s->addStripLine(*strip, QPolygonF{{2000, 2000}, {2300, 2400}});
        st->setSelected(true);
        auto *h = [&]() -> HandleItem * {
            for (QGraphicsItem *ch : st->childItems())
                if (ch->type() == ItemType::Handle && static_cast<HandleItem *>(ch)->index() == 1)
                    return static_cast<HandleItem *>(ch);
            return nullptr;
        }();
        QVERIFY(h);
        h->dragTo(QPointF(2006, 2400));
        QCOMPARE(st->scenePoints().at(1).x(), 2000.0);
    }

    void wifiCoverageThroughWalls()
    {
        const LibraryEntry *ap = find(DeviceKind::Wifi);
        QVERIFY2(ap, "an access point with type=\"wifi\" in the library");
        Project p;
        PlanScene *s = p.current();
        auto *w = new WallItem;                      // a wall 3 m to the right of the access point
        w->setThickness(20);
        w->setPoints(QPolygonF{{300, -1000}, {300, 1000}});
        s->addItem(w);
        s->notifyWallsChanged();
        DeviceItem *d = s->addDevice(*ap, QPointF(0, 0));
        QVERIFY(d->isWifi());
        QVERIFY(!d->data().on);                      // not in the PDF unless asked
        // not switched by "all on", nor by a double click
        s->setAllLights(true);
        QVERIFY(!d->data().on);
        d->dataRef().radius = 1000;
        d->dataRef().wallLoss = 40;
        d->refresh();
        QCoreApplication::processEvents();
        EffectItem *e = d->effectItem();
        QVERIFY(e);
        const QVector<QPolygonF> zones = e->wifiZones();
        QCOMPARE(zones.size(), 3);
        double left = 0, right = 0;
        for (const QPointF &q : zones.first()) {
            left = std::min(left, q.x());
            right = std::max(right, q.x());
        }
        QVERIFY(std::abs(left + 1000) < 5);          // free side: full range
        QVERIFY(right > 300);                        // goes through the wall...
        QVERIFY(right < 1000 - 300);                 // ...but weaker (40 % of the range lost)
        // the excellent zone is smaller than the weak one
        QVERIFY(zones[2].boundingRect().width() < zones[0].boundingRect().width());
        // saved with its range and wall loss
        QTemporaryDir dir;
        QVERIFY(SceneSerializer::saveProject(&p, dir.filePath("w.shp")));
        Project q;
        QVERIFY(SceneSerializer::loadProject(&q, dir.filePath("w.shp")));
        DeviceItem *d2 = q.current()->devices().first();
        QVERIFY(d2->isWifi());
        QCOMPARE(d2->data().radius, 1000.0);
        QCOMPARE(d2->data().wallLoss, 40.0);
    }

    void cleanupTestCase()
    {
        delete view;
        delete project;
    }
};

QTEST_MAIN(SelfTest)
#include "selftest.moc"
