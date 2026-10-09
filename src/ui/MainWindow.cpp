#include "MainWindow.h"

#include "ExportDialog.h"
#include "LibraryPanel.h"
#include "ProjectInfoDialog.h"
#include "ProjectTree.h"
#include "PropertiesPanel.h"
#include "SectionPanel.h"
#include "core/Geometry.h"
#include "core/IconCache.h"
#include "core/Library.h"
#include "core/Project.h"
#include "export/PdfExporter.h"
#include "scene/BackgroundItem.h"
#include "scene/GroupItem.h"
#include "scene/PanelItems.h"
#include "scene/PlanView.h"
#include "scene/RoomItem.h"
#include "scene/SceneSerializer.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

#ifdef SHP_HAVE_QTPDF
#include <QPdfDocument>
#endif

QIcon MainWindow::icon(const char *name) const
{
    return IconCache::instance().icon(QStringLiteral(":/resources/icons/%1.svg").arg(QLatin1String(name)));
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_project = new Project(this);
    m_scene = m_project->current();
    m_view = new PlanView(m_scene, this);
    setWindowIcon(IconCache::instance().icon(QStringLiteral(":/resources/app.svg")));

    createActions();
    setCentralWidget(createCentral());
    createDocks();
    createMenus();
    createToolBars();

    m_posLabel = new QLabel;
    m_zoomLabel = new QLabel;
    statusBar()->addPermanentWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);

    connect(m_view, &PlanView::zoomChanged, this, [this](double z) {
        m_zoomLabel->setText(QStringLiteral("%1 %").arg(int(std::round(z * 100))));
    });
    connect(m_project, &Project::modified, this, [this] { setWindowModified(true); });
    connect(m_project, &Project::floorsChanged, this, &MainWindow::rebuildFloorTabs);
    connect(m_project, &Project::currentChanged, this, &MainWindow::bindScene);
    connect(m_props, &PropertiesPanel::editProjectInfo, this, &MainWindow::editProjectInfo);
    connect(m_tree, &ProjectTree::editProjectInfo, this, &MainWindow::editProjectInfo);
    connect(m_tree, &ProjectTree::addFloorRequested, this, &MainWindow::addFloor);
    connect(m_tree, &ProjectTree::addPanelRequested, this, [this](int floorIndex) { addPanelTo(m_project->floor(floorIndex)); });
    connect(m_tree, &ProjectTree::deleteFloorRequested, this, &MainWindow::deleteFloor);
    connect(m_tree, &ProjectTree::importPlanRequested, this, &MainWindow::importBackground);
    connect(m_tree, &ProjectTree::focusItem, this, &MainWindow::focusItem);

    connect(m_library, &LibraryPanel::currentEntryChanged, this, [this](const QString &key) {
        if (const LibraryEntry *e = Library::instance().entry(key))
            if (e->kind == DeviceKind::Strip)
                for (PlanScene *s : m_project->floors())
                    s->setStripEntry(key);
    });
    connect(m_library, &LibraryPanel::reloadRequested, this, &MainWindow::reloadLibrary);
    connect(m_library, &LibraryPanel::entryActivated, this, [this](const QString &key) {
        const LibraryEntry *e = Library::instance().entry(key);
        if (!e)
            return;
        if (e->kind == DeviceKind::Strip && !m_scene->isPanel()) {
            m_scene->setStripEntry(key);
            m_scene->setTool(PlanScene::StripTool);
            return;
        }
        m_scene->setTool(PlanScene::SelectTool);
        m_scene->placeEntry(*e, m_view->mapToScene(m_view->viewport()->rect().center()));
    });

    QSettings st;
    restoreGeometry(st.value("window/geometry").toByteArray());
    // state3: wider default panels (older saved layouts are ignored once)
    restoreState(st.value("window/state3").toByteArray());
    if (!st.contains("window/geometry"))
        resize(1600, 980);

    bindScene(m_scene);
    rebuildFloorTabs();
    updateTitle();
    setWindowModified(false);
    statusBar()->showMessage(tr("Pan: two fingers on the trackpad, right / middle mouse drag or Space + drag.  "
                                "Zoom: pinch, mouse wheel or Cmd + scroll."), 15000);
}

// ---------------------------------------------------------------- floors

void MainWindow::bindScene(PlanScene *scene)
{
    if (!scene || scene == m_bound)
        return;
    m_bound = scene;
    QSet<QString> hidden;
    if (m_scene && m_scene != scene) {
        m_scene->setTool(PlanScene::SelectTool);
        m_viewStates[m_scene] = {m_view->transform(), m_view->mapToScene(m_view->viewport()->rect().center())};
        disconnect(m_scene, nullptr, this, nullptr);
        hidden = m_scene->hiddenSections();
        scene->setHiddenSections(hidden);      // keep the chapter filter when changing floor
    }
    m_scene = scene;

    connect(scene, &PlanScene::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m); });
    connect(scene, &PlanScene::cursorMoved, this, [this](const QPointF &p) {
        m_posLabel->setText(QStringLiteral("x %1  y %2").arg(Geo::formatMeters(p.x()), Geo::formatMeters(p.y())));
    });
    connect(scene, &PlanScene::calibrationPicked, this, &MainWindow::onCalibration);
    connect(scene, &PlanScene::addPanelRequested, this, [this, scene](const QPointF &at) { addPanelTo(scene, at, true); });
    connect(scene, &PlanScene::toolChanged, this, [this](PlanScene::Tool t) {
        const char *names[] = {"toolSelect", "toolWall", "toolRoom", "toolStrip", "toolCircle", "toolScale", "toolMeasure", "toolRail", "toolWire", "toolGroup"};
        if (QAction *a = m_act.value(names[t]))
            a->setChecked(true);
    });
    auto updateBg = [this] {
        const bool has = m_scene->background() != nullptr;
        m_act["bgRemove"]->setEnabled(has);
        m_act["bgReset"]->setEnabled(has);
        m_act["bgMove"]->setEnabled(has);
        m_act["bgMove"]->blockSignals(true);
        m_act["bgMove"]->setChecked(has && !m_scene->background()->isLocked());
        m_act["bgMove"]->blockSignals(false);
    };
    connect(scene, &PlanScene::backgroundChanged, this, updateBg);
    updateBg();

    // view options are the same on every floor
    scene->setShowGrid(m_act["grid"]->isChecked());
    scene->setSnapEnabled(m_act["snap"]->isChecked());
    scene->setGridSnapEnabled(m_act["gridSnap"]->isChecked());
    scene->setShowNotes(m_act["notes"]->isChecked());
    scene->setShowRoomNames(m_act["roomNames"]->isChecked());
    scene->setShowGroups(m_act["showGroups"]->isChecked());
    scene->setShowRoomAreas(m_act["roomAreas"]->isChecked());
    scene->setShowBeams(m_act["beams"]->isChecked());
    scene->setNightMode(m_act["night"]->isChecked());
    m_act["toolSelect"]->setChecked(true);
    scene->setTool(PlanScene::SelectTool);

    applyPageKind();
    m_view->setPlanScene(scene);
    m_props->setScene(scene);
    m_sections->setScene(scene);
    m_project->undoGroup()->setActiveStack(scene->undoStack());

    if (m_viewStates.contains(scene)) {
        const ViewState vs = m_viewStates.value(scene);
        m_view->setTransform(vs.transform);
        m_view->centerOn(vs.center);
        m_view->zoomBy(1.0);
    } else {
        m_view->zoomToFit();
    }

    m_updatingTabs = true;
    m_floorTabs->setCurrentIndex(m_project->indexOf(scene));
    m_updatingTabs = false;
    updateTitle();
}

void MainWindow::applyPageKind()
{
    const bool panel = m_scene && m_scene->isPanel();
    // floor tools / panel tools
    for (const char *id : {"toolWall", "toolRoom", "toolStrip", "toolCircle", "toolScale", "toolGroup"})
        m_act[id]->setVisible(!panel);
    for (const char *id : {"toolRail", "toolWire"})
        m_act[id]->setVisible(panel);
    for (const char *id : {"night", "beams", "allOn", "allOff", "roomNames", "roomAreas", "group", "ungroup"})
        m_act[id]->setEnabled(!panel);
    m_library->setPanelMode(panel);
}

void MainWindow::rebuildFloorTabs()
{
    m_updatingTabs = true;
    while (m_floorTabs->count() > 0)
        m_floorTabs->removeTab(0);
    for (PlanScene *s : m_project->floors()) {
        const int t = m_floorTabs->addTab(icon(s->isPanel() ? "panel" : "floor"), s->floorName());
        if (s->isPanel())
            m_floorTabs->setTabToolTip(t, s->parentFloor() ? tr("Electrical panel on %1").arg(s->parentFloor()->floorName())
                                                           : tr("Electrical panel"));
    }
    const int plus = m_floorTabs->addTab(icon("flooradd"), QString());
    m_floorTabs->setTabToolTip(plus, tr("Add a floor or an electrical panel"));
    m_floorTabs->setCurrentIndex(m_project->currentIndex());
    m_updatingTabs = false;
    // drop view states of deleted floors
    for (auto it = m_viewStates.begin(); it != m_viewStates.end();) {
        if (!m_project->floors().contains(it.key()))
            it = m_viewStates.erase(it);
        else
            ++it;
    }
}

void MainWindow::addFloor()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Add floor"), tr("Floor name:"), QLineEdit::Normal,
                                               m_project->defaultFloorName(m_project->floorCount()), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    PlanScene *s = m_project->addFloor(name.trimmed());
    m_project->setCurrent(m_project->indexOf(s));
    setWindowModified(true);
    if (QMessageBox::question(this, tr("Add floor"), tr("Import the floor plan image / PDF for \"%1\" now?").arg(name.trimmed()))
        == QMessageBox::Yes)
        importBackground();
}

void MainWindow::reloadLibrary()
{
    const int n = Library::instance().reload();
    m_library->reload();
    m_library->setPanelMode(m_scene && m_scene->isPanel());
    m_sections->rebuild();
    m_props->rebuild();
    QStringList files;
    for (const QString &f : Library::instance().loadedFiles())
        files << QFileInfo(f).fileName();
    statusBar()->showMessage(tr("Library reloaded: %n element(s) from %1", nullptr, n).arg(files.join(QStringLiteral(", "))), 10000);
    if (!Library::instance().errors().isEmpty())
        QMessageBox::warning(this, tr("Library"), tr("Some library files have XML errors and were skipped:\n\n%1")
                                                       .arg(Library::instance().errors().join(QStringLiteral("\n"))));
}

void MainWindow::addPanel()
{
    addPanelTo(nullptr);
}

void MainWindow::addPanelTo(PlanScene *floor, const QPointF &symbolAt, bool placeSymbol)
{
    if (!floor && m_scene)
        floor = m_scene->isPanel() ? m_scene->parentFloor() : m_scene;

    // name + floor
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Panel"));
    auto *form = new QFormLayout(&dlg);
    auto *name = new QLineEdit(m_project->defaultPanelName());
    name->selectAll();
    auto *floors = new QComboBox;
    floors->addItem(tr("(no floor)"), -1);
    for (int i = 0; i < m_project->floorCount(); ++i) {
        PlanScene *f = m_project->floor(i);
        if (f->isPanel())
            continue;
        floors->addItem(icon("floor"), f->floorName(), i);
        if (f == floor)
            floors->setCurrentIndex(floors->count() - 1);
    }
    form->addRow(tr("Panel name:"), name);
    form->addRow(tr("On floor:"), floors);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
        return;
    PlanScene *parent = m_project->floor(floors->currentData().toInt());
    const QString panelName = name->text().trimmed();

    // the panel symbol on the floor plan, where the right click was
    if (placeSymbol && parent && !parent->isPanel()) {
        for (const LibraryEntry *e : Library::instance().entries()) {
            if (e->isPanelPart() || e->section != QLatin1String("electrical"))
                continue;
            if (e->name.contains(QLatin1String("Tablou"), Qt::CaseInsensitive)
                || e->name.contains(QLatin1String("panel"), Qt::CaseInsensitive)) {
                if (DeviceItem *d = parent->addDevice(*e, symbolAt)) {
                    d->dataRef().note = panelName;
                    d->refresh();
                    parent->commit(tr("Electrical panel symbol"));
                }
                break;
            }
        }
    }

    PlanScene *s = m_project->addFloor(panelName, PlanScene::PanelPage, parent);
    // a first DIN rail, ready for the equipment
    s->beginRestore();
    auto *r = new RailItem(450);
    r->setPos(0, 0);
    s->addItem(r);
    s->applyEditability(r);
    s->endRestore();
    s->resetUndo();
    m_project->setCurrent(m_project->indexOf(s));
    m_view->zoomToFit();
    setWindowModified(true);
    statusBar()->showMessage(tr("Drag equipment from the library onto the DIN rail. Wire tool (E): click a terminal, then the other terminal."), 15000);
}

void MainWindow::deleteFloor(int index)
{
    PlanScene *s = m_project->floor(index);
    if (!s || m_project->floorCount() <= 1)
        return;
    const QString question = s->isPanel()
        ? tr("Delete the electrical panel \"%1\" with its equipment and wires?\nThis cannot be undone.").arg(s->floorName())
        : tr("Delete the floor \"%1\" with everything drawn on it?\nThis cannot be undone.").arg(s->floorName());
    if (QMessageBox::warning(this, s->isPanel() ? tr("Delete panel") : tr("Delete floor"), question,
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    if (s == m_scene)
        m_project->setCurrent(index == 0 ? 1 : index - 1);
    m_viewStates.remove(s);
    m_project->removeFloor(index);
    setWindowModified(true);
}

void MainWindow::focusItem(QGraphicsItem *item)
{
    if (!item || item->scene() != m_scene)
        return;
    const int t = item->type();
    const bool area = t == ItemType::Room || t == ItemType::Group || t == ItemType::Wire;
    const double margin = m_scene->isPanel() ? 60 : 250;
    const QRectF r = area ? item->sceneBoundingRect().adjusted(-margin / 2.5, -margin / 2.5, margin / 2.5, margin / 2.5)
                          : QRectF(item->sceneBoundingRect().center() - QPointF(margin, margin), QSizeF(2 * margin, 2 * margin));
    const QRectF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    if (area || m_view->zoom() < 0.5 || !visible.contains(r.center()))
        m_view->fitInView(r, Qt::KeepAspectRatio);
    else
        m_view->centerOn(item);
    m_view->zoomBy(1.0);
    m_view->setFocus();
}

// ---------------------------------------------------------------- actions & menus

void MainWindow::createActions()
{
    auto add = [this](const QString &id, const QString &text, const char *ic, const QKeySequence &key, auto slot) {
        auto *a = new QAction(text, this);
        if (ic)
            a->setIcon(icon(ic));
        if (!key.isEmpty())
            a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        m_act.insert(id, a);
        return a;
    };
    auto addCheck = [this](const QString &id, const QString &text, const char *ic, const QKeySequence &key, bool checked, auto slot) {
        auto *a = new QAction(text, this);
        if (ic)
            a->setIcon(icon(ic));
        a->setCheckable(true);
        a->setChecked(checked);
        if (!key.isEmpty())
            a->setShortcut(key);
        connect(a, &QAction::toggled, this, slot);
        m_act.insert(id, a);
        return a;
    };

    // file
    add("new", tr("&New project"), "new", QKeySequence::New, &MainWindow::newProject);
    add("open", tr("&Open..."), "open", QKeySequence::Open, &MainWindow::openProjectDialog);
    add("save", tr("&Save"), "save", QKeySequence::Save, &MainWindow::save);
    add("saveAs", tr("Save &as..."), nullptr, QKeySequence::SaveAs, &MainWindow::saveAs);
    add("export", tr("&Export PDF..."), "pdf", QKeySequence(Qt::CTRL | Qt::Key_E), &MainWindow::exportPdf);
    add("bgImport", tr("Import &floor plan (image / PDF)..."), "import", QKeySequence(Qt::CTRL | Qt::Key_I),
        [this] { importBackground(); });
    add("info", tr("Project &information..."), "info", QKeySequence(), &MainWindow::editProjectInfo);
    add("floorAdd", tr("Add &floor..."), "flooradd", QKeySequence(), &MainWindow::addFloor);
    add("panelAdd", tr("Add &Panel"), "panel", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), &MainWindow::addPanel)
        ->setToolTip(tr("Add an electrical panel page on the current floor (Ctrl+Shift+P)"));
    add("group", tr("Group selected elements"), "group", QKeySequence(Qt::CTRL | Qt::Key_G), [this] {
        m_scene->setTool(PlanScene::SelectTool);
        m_scene->groupSelection();
    })->setToolTip(tr("Link the selected elements with a dashed line (circuit / group) (Ctrl+G)"));
    add("ungroup", tr("Ungroup"), "ungroup", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [this] {
        // selected group lines are deleted; selected elements leave their groups
        QList<GroupItem *> gs;
        QStringList uids;
        for (QGraphicsItem *it : m_scene->selectedItems()) {
            if (it->type() == ItemType::Group)
                gs << static_cast<GroupItem *>(it);
            else if (!PlanScene::uidOf(it).isEmpty())
                uids << PlanScene::uidOf(it);
        }
        if (gs.isEmpty() && uids.isEmpty())
            return;
        for (GroupItem *g : gs)
            delete g;
        for (const QString &u : uids)
            for (GroupItem *g : m_scene->groupsOf(u)) {
                g->removeMember(u);
                if (g->members().size() < 2)
                    delete g;
            }
        m_scene->commit(tr("Ungroup"));
    })->setToolTip(tr("Remove the selected group, or take the selected elements out of their groups (Ctrl+Shift+G)"));
    add("floorDel", tr("&Delete floor..."), nullptr, QKeySequence(), [this] { deleteFloor(m_project->currentIndex()); });
    add("quit", tr("&Quit"), nullptr, QKeySequence::Quit, &QWidget::close);

    // edit
    QAction *undo = m_project->undoGroup()->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setIcon(icon("undo"));
    m_act.insert("undo", undo);
    QAction *redo = m_project->undoGroup()->createRedoAction(this, tr("&Redo"));
    redo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    redo->setIcon(icon("redo"));
    m_act.insert("redo", redo);
    add("copy", tr("&Copy"), "copy", QKeySequence::Copy, [this] { m_scene->copySelection(); });
    add("paste", tr("&Paste"), nullptr, QKeySequence::Paste, [this] {
        m_scene->setTool(PlanScene::SelectTool);
        const QPoint vp = m_view->viewport()->mapFromGlobal(QCursor::pos());
        const QPointF at = m_view->viewport()->rect().contains(vp) ? m_view->mapToScene(vp)
                                                                   : m_view->mapToScene(m_view->viewport()->rect().center());
        m_scene->paste(at);
    });
    add("duplicate", tr("&Duplicate"), nullptr, QKeySequence(Qt::CTRL | Qt::Key_D), [this] { m_scene->duplicateSelection(); });
    QAction *del = add("delete", tr("De&lete"), "delete", QKeySequence::Delete, [this] { m_scene->deleteSelection(); });
    del->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    add("selectAll", tr("Select &all"), nullptr, QKeySequence::SelectAll, [this] {
        m_scene->setTool(PlanScene::SelectTool);
        for (QGraphicsItem *it : m_scene->contentItems())
            if (it->isVisible())
                it->setSelected(true);
    });
    add("rotate", tr("&Rotate 90°"), "rotate", QKeySequence(Qt::Key_R), [this] { m_scene->rotateSelection(90); });
    add("renumber", tr("Re&number IDs"), "renumber", QKeySequence(), [this] {
        if (QMessageBox::question(this, tr("Renumber IDs"),
                                  tr("Renumber all elements of this floor per chapter, from top-left to bottom-right?"))
            == QMessageBox::Yes)
            m_scene->renumberIds();
    });

    // tools
    m_toolGroup = new QActionGroup(this);
    struct ToolDef { const char *id; QString text; const char *ic; QKeySequence key; PlanScene::Tool tool; QString tip; };
    const ToolDef tools[] = {
        {"toolSelect", tr("Select"), "select", QKeySequence(Qt::Key_V), PlanScene::SelectTool, tr("Select and move (V)")},
        {"toolWall", tr("Wall"), "wall", QKeySequence(Qt::Key_W), PlanScene::WallTool, tr("Draw walls (W)")},
        {"toolRoom", tr("Room"), "room", QKeySequence(Qt::Key_M), PlanScene::RoomTool, tr("Draw a room / surface – shows m² (M)")},
        {"toolStrip", tr("LED line"), "ledline", QKeySequence(Qt::Key_L), PlanScene::StripTool, tr("Draw an LED strip as a line (L)")},
        {"toolCircle", tr("LED circle"), "ledcircle", QKeySequence(Qt::Key_C), PlanScene::StripCircleTool, tr("Draw an LED strip as a circle (C)")},
        {"toolScale", tr("Scale"), "scale", QKeySequence(Qt::Key_K), PlanScene::CalibrateTool, tr("Set the plan scale from a known distance (K)")},
        {"toolMeasure", tr("Measure"), "measure", QKeySequence(Qt::Key_D), PlanScene::MeasureTool, tr("Measure a distance (D)")},
        {"toolRail", tr("DIN rail"), "rail", QKeySequence(Qt::Key_I), PlanScene::RailTool, tr("Draw a DIN rail in the electrical panel (I)")},
        {"toolWire", tr("Wire"), "wire", QKeySequence(Qt::Key_E), PlanScene::WireTool, tr("Connect two terminals with a wire (E)")},
        {"toolGroup", tr("Group"), "group", QKeySequence(Qt::Key_J), PlanScene::GroupTool,
         tr("Link elements two by two with a dashed line: click an element, then the next one (J)")},
    };
    for (const ToolDef &t : tools) {
        auto *a = new QAction(icon(t.ic), t.text, this);
        a->setCheckable(true);
        a->setShortcut(t.key);
        a->setToolTip(t.tip);
        a->setStatusTip(t.tip);
        m_toolGroup->addAction(a);
        const PlanScene::Tool tool = t.tool;
        connect(a, &QAction::triggered, this, [this, tool] {
            if (tool == PlanScene::StripTool || tool == PlanScene::StripCircleTool) {
                const LibraryEntry *e = Library::instance().entry(m_scene->stripEntry());
                if (!e || e->kind != DeviceKind::Strip) {
                    for (const LibraryEntry *le : Library::instance().entries())
                        if (le->kind == DeviceKind::Strip) {
                            m_scene->setStripEntry(le->key);
                            break;
                        }
                }
            }
            m_scene->setTool(tool);
            m_view->setFocus();
        });
        m_act.insert(t.id, a);
    }
    m_act["toolSelect"]->setChecked(true);

    // view
    add("zoomFit", tr("Zoom to &fit"), "fit", QKeySequence(Qt::Key_F), [this] { m_view->zoomToFit(); });
    add("libReload", tr("Reload &library"), "reload", QKeySequence(Qt::Key_F5), &MainWindow::reloadLibrary);
    QAction *zin = add("zoomIn", tr("Zoom &in"), "zoomin", QKeySequence::ZoomIn, [this] { m_view->zoomBy(1.25); });
    zin->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal)});
    QAction *zout = add("zoomOut", tr("Zoom &out"), "zoomout", QKeySequence::ZoomOut, [this] { m_view->zoomBy(0.8); });
    zout->setShortcuts({QKeySequence::ZoomOut, QKeySequence(Qt::Key_Minus)});
    addCheck("grid", tr("Show &grid"), nullptr, QKeySequence(Qt::Key_G), true, [this](bool on) { m_scene->setShowGrid(on); });
    addCheck("snap", tr("S&nap to walls / corners"), nullptr, QKeySequence(Qt::Key_S), true, [this](bool on) { m_scene->setSnapEnabled(on); });
    addCheck("gridSnap", tr("Snap to &grid"), nullptr, QKeySequence(), false, [this](bool on) { m_scene->setGridSnapEnabled(on); });
    addCheck("notes", tr("Show &notes"), nullptr, QKeySequence(), true, [this](bool on) { m_scene->setShowNotes(on); });
    addCheck("showGroups", tr("Show &groups"), nullptr, QKeySequence(), true, [this](bool on) { m_scene->setShowGroups(on); });
    addCheck("roomNames", tr("Room names"), "roomname", QKeySequence(), true, [this](bool on) { m_scene->setShowRoomNames(on); });
    m_act["roomNames"]->setToolTip(tr("Show the room names on the plan (the PDF always shows them)"));
    addCheck("roomAreas", tr("Areas m²"), "roomarea", QKeySequence(), true, [this](bool on) { m_scene->setShowRoomAreas(on); });
    m_act["roomAreas"]->setToolTip(tr("Show the room areas on the plan (the PDF always shows them)"));
    addCheck("beams", tr("Show light &beams / speaker range"), "beams", QKeySequence(Qt::Key_B), false,
             [this](bool on) { m_scene->setShowBeams(on); });
    addCheck("night", tr("&Night simulation"), "night", QKeySequence(Qt::Key_N), false, [this](bool on) { m_scene->setNightMode(on); });
    add("allOn", tr("All lights &on"), "on", QKeySequence(Qt::SHIFT | Qt::Key_O), [this] { m_scene->setAllLights(true); });
    add("allOff", tr("All lights o&ff"), "off", QKeySequence(Qt::SHIFT | Qt::Key_F), [this] { m_scene->setAllLights(false); });

    // short labels for the icon toolbars
    const std::pair<const char *, QString> shortText[] = {
        {"new", tr("New")}, {"open", tr("Open")}, {"save", tr("Save")}, {"export", tr("PDF")},
        {"bgImport", tr("Floor plan")}, {"floorAdd", tr("Add floor")}, {"undo", tr("Undo")}, {"redo", tr("Redo")},
        {"night", tr("Night")}, {"beams", tr("Beams")}, {"allOn", tr("All on")}, {"allOff", tr("All off")},
        {"zoomFit", tr("Fit")}, {"zoomIn", tr("Zoom +")}, {"zoomOut", tr("Zoom −")}, {"rotate", tr("Rotate")}, {"delete", tr("Delete")}, {"renumber", tr("Renumber")},
        {"info", tr("Project")},
    };
    for (const auto &[id, text] : shortText)
        if (QAction *a = m_act.value(id))
            a->setIconText(text);

    // background
    addCheck("bgMove", tr("&Move / unlock floor plan"), nullptr, QKeySequence(), false, [this](bool on) {
        if (BackgroundItem *bg = m_scene->background())
            bg->setLocked(!on);
    });
    add("bgReset", tr("Reset floor plan &scale"), nullptr, QKeySequence(), &MainWindow::resetBackgroundScale);
    add("bgRemove", tr("&Remove floor plan"), nullptr, QKeySequence(), [this] {
        if (QMessageBox::question(this, tr("Remove floor plan"), tr("Remove the imported floor plan image?"))
            == QMessageBox::Yes) {
            m_scene->removeBackground();
            m_scene->resetUndo();
            setWindowModified(true);
        }
    });
}

QWidget *MainWindow::createCentral()
{
    auto *central = new QWidget;
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // floor tabs (like sheets): one tab per floor, last tab adds a floor
    m_floorTabs = new QTabBar;
    m_floorTabs->setDocumentMode(true);
    m_floorTabs->setExpanding(false);
    m_floorTabs->setDrawBase(true);
    m_floorTabs->setIconSize(QSize(16, 16));
    // without scroll buttons (macOS default) many tabs force a wide minimum on the plan area,
    // and then the side panels cannot be made wider
    m_floorTabs->setUsesScrollButtons(true);
    m_floorTabs->setElideMode(Qt::ElideRight);
    connect(m_floorTabs, &QTabBar::currentChanged, this, [this](int i) {
        if (m_updatingTabs || i < 0)
            return;
        if (i >= m_project->floorCount()) {
            m_updatingTabs = true;
            m_floorTabs->setCurrentIndex(m_project->currentIndex());
            m_updatingTabs = false;
            QMenu menu;
            menu.addAction(icon("floor"), tr("Add floor..."), this, &MainWindow::addFloor);
            menu.addAction(icon("panel"), tr("Add Panel"), this, &MainWindow::addPanel);
            menu.exec(m_floorTabs->mapToGlobal(m_floorTabs->tabRect(i).bottomLeft()));
            return;
        }
        m_project->setCurrent(i);
    });
    // right click on a tab
    m_floorTabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_floorTabs, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int i = m_floorTabs->tabAt(pos);
        PlanScene *s = m_project->floor(i);
        QMenu menu;
        if (s) {
            menu.addAction(tr("Rename..."), this, [this, i] { emit m_floorTabs->tabBarDoubleClicked(i); });
            if (s->isPanel()) {
                QMenu *on = menu.addMenu(tr("On floor"));
                for (PlanScene *f : m_project->floors()) {
                    if (f->isPanel())
                        continue;
                    QAction *a = on->addAction(f->floorName(), this, [this, s, f] { m_project->setPanelFloor(s, f); });
                    a->setCheckable(true);
                    a->setChecked(s->parentFloor() == f);
                }
            }
            menu.addSeparator();
        }
        menu.addAction(icon("floor"), tr("Add floor..."), this, &MainWindow::addFloor);
        PlanScene *target = s ? (s->isPanel() ? s->parentFloor() : s) : m_scene;
        menu.addAction(icon("panel"), tr("Add Panel"), this, [this, target] { addPanelTo(target); });
        if (s) {
            menu.addSeparator();
            QAction *del = menu.addAction(icon("delete"), s->isPanel() ? tr("Delete panel...") : tr("Delete floor..."),
                                          this, [this, i] { deleteFloor(i); });
            del->setEnabled(m_project->floorCount() > 1);
        }
        menu.exec(m_floorTabs->mapToGlobal(pos));
    });
    connect(m_floorTabs, &QTabBar::tabBarDoubleClicked, this, [this](int i) {
        if (i < 0 || i >= m_project->floorCount())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename floor"), tr("Floor name:"), QLineEdit::Normal,
                                                   m_project->floor(i)->floorName(), &ok);
        if (ok)
            m_project->renameFloor(i, name);
    });
    v->addWidget(m_floorTabs);

    auto *h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    // drawing tools: vertical bar with icons and text, next to the plan
    m_toolsBar = new QToolBar(tr("Drawing tools"));
    m_toolsBar->setOrientation(Qt::Vertical);
    m_toolsBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_toolsBar->setIconSize(QSize(26, 26));
    m_toolsBar->setMovable(false);
    m_toolsBar->setStyleSheet(QStringLiteral("QToolBar { border-right: 1px solid palette(mid); padding: 2px; }"
                                             "QToolButton { min-width: 64px; padding: 3px 2px; }"));
    for (const char *id : {"toolSelect", "toolWall", "toolRoom", "toolStrip", "toolCircle", "toolGroup", "toolScale", "toolMeasure", "toolRail", "toolWire"})
        m_toolsBar->addAction(m_act[id]);
    m_toolsBar->addSeparator();
    for (const char *id : {"rotate", "delete", "renumber"})
        m_toolsBar->addAction(m_act[id]);
    m_toolsBar->addSeparator();
    m_toolsBar->addAction(m_act["ungroup"]);
    h->addWidget(m_toolsBar);
    h->addWidget(m_view, 1);
    v->addLayout(h, 1);
    return central;
}

void MainWindow::createToolBars()
{
    QToolBar *tb = addToolBar(tr("Main toolbar"));
    tb->setObjectName("tbMain");
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(26, 26));
    tb->setMovable(false);
    for (const char *id : {"new", "open", "save"})
        tb->addAction(m_act[id]);
    tb->addSeparator();
    for (const char *id : {"bgImport", "floorAdd", "info"})
        tb->addAction(m_act[id]);
    tb->addSeparator();
    tb->addAction(m_act["undo"]);
    tb->addAction(m_act["redo"]);
    tb->addSeparator();
    for (const char *id : {"night", "beams", "allOn", "allOff"})
        tb->addAction(m_act[id]);
    tb->addSeparator();
    for (const char *id : {"roomNames", "roomAreas"})
        tb->addAction(m_act[id]);
    tb->addSeparator();
    for (const char *id : {"zoomIn", "zoomOut", "zoomFit"})
        tb->addAction(m_act[id]);
    tb->addSeparator();
    tb->addAction(m_act["export"]);
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_act["new"]);
    file->addAction(m_act["open"]);
    m_recentMenu = file->addMenu(tr("Open &recent"));
    rebuildRecentMenu();
    QAction *reopen = file->addAction(tr("Open the last project at start"));
    reopen->setCheckable(true);
    reopen->setChecked(QSettings().value("reopenLast", true).toBool());
    connect(reopen, &QAction::toggled, this, [](bool on) { QSettings().setValue("reopenLast", on); });
    file->addSeparator();
    file->addAction(m_act["save"]);
    file->addAction(m_act["saveAs"]);
    file->addSeparator();
    file->addAction(m_act["bgImport"]);
    file->addAction(m_act["export"]);
    file->addSeparator();
    file->addAction(m_act["info"]);
    file->addSeparator();
    file->addAction(m_act["quit"]);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    for (const char *id : {"undo", "redo"})
        edit->addAction(m_act[id]);
    edit->addSeparator();
    for (const char *id : {"copy", "paste", "duplicate", "delete", "selectAll"})
        edit->addAction(m_act[id]);
    edit->addSeparator();
    edit->addAction(m_act["rotate"]);
    edit->addAction(m_act["renumber"]);
    edit->addSeparator();
    edit->addAction(m_act["group"]);
    edit->addAction(m_act["ungroup"]);

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    for (const char *id : {"toolSelect", "toolWall", "toolRoom", "toolStrip", "toolCircle", "toolGroup", "toolScale", "toolMeasure", "toolRail", "toolWire"})
        tools->addAction(m_act[id]);

    QMenu *plan = menuBar()->addMenu(tr("&Plan"));
    plan->addAction(m_act["floorAdd"]);
    plan->addAction(m_act["panelAdd"]);
    plan->addAction(m_act["floorDel"]);
    plan->addSeparator();
    plan->addAction(m_act["bgImport"]);
    plan->addAction(m_act["bgMove"]);
    QMenu *op = plan->addMenu(tr("Floor plan &opacity"));
    for (int pct : {100, 70, 50, 30}) {
        op->addAction(QStringLiteral("%1 %").arg(pct), this, [this, pct] {
            if (BackgroundItem *bg = m_scene->background()) {
                bg->setOpacity(pct / 100.0);
                m_scene->commit(tr("Floor plan opacity"));
            }
        });
    }
    plan->addAction(m_act["bgReset"]);
    plan->addAction(m_act["bgRemove"]);
    plan->addSeparator();
    plan->addAction(m_act["toolScale"]);
    plan->addAction(m_act["renumber"]);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    for (const char *id : {"zoomFit", "zoomIn", "zoomOut"})
        view->addAction(m_act[id]);
    view->addSeparator();
    for (const char *id : {"grid", "snap", "gridSnap", "notes", "roomNames", "roomAreas", "showGroups"})
        view->addAction(m_act[id]);
    view->addSeparator();
    view->addAction(m_act["libReload"]);
    view->addAction(tr("Reset panel &layout"), this, &MainWindow::resetLayout);
    view->addSeparator();
    QMenu *panels = view->addMenu(tr("&Panels"));
    for (QDockWidget *d : findChildren<QDockWidget *>())
        panels->addAction(d->toggleViewAction());
    view->addSeparator();
    QMenu *lang = view->addMenu(tr("&Language"));
    auto *langGroup = new QActionGroup(this);
    const QString cur = QSettings().value("language", QLocale::system().name().left(2)).toString();
    for (const auto &[code, name] : {std::pair<QString, QString>{"en", "English"}, {"ro", QStringLiteral("Română")}}) {
        QAction *a = lang->addAction(name);
        a->setCheckable(true);
        a->setChecked(cur == code || (code == "en" && cur != "ro"));
        langGroup->addAction(a);
        const QString c = code;
        connect(a, &QAction::triggered, this, [this, c] {
            QSettings().setValue("language", c);
            QMessageBox::information(this, tr("Language"), tr("The language will change after restarting the application."));
        });
    }

    QMenu *sim = menuBar()->addMenu(tr("&Simulation"));
    for (const char *id : {"night", "beams"})
        sim->addAction(m_act[id]);
    sim->addSeparator();
    sim->addAction(m_act["allOn"]);
    sim->addAction(m_act["allOff"]);
    sim->addSeparator();
    QAction *hint = sim->addAction(tr("Tip: double-click a light, LED strip or speaker to switch it"));
    hint->setEnabled(false);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Open the &library folder"), this, [] {
        const QStringList files = Library::instance().loadedFiles();
        if (!files.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(files.first()).absolutePath()));
    });
    help->addAction(tr("&About"), this, [this] {
        QMessageBox::about(this, tr("About Smart Home Planner"),
                           tr("<h3>Smart Home Planner %1</h3>"
                              "<p>Design smart home installations: lights, sensors, music, sockets ...</p>"
                              "<p>Library files:<br>%2</p>")
                               .arg(QStringLiteral(SHP_VERSION), Library::instance().loadedFiles().join("<br>")));
    });
}

void MainWindow::createDocks()
{
    m_tree = new ProjectTree(m_project);
    auto *treeDock = new QDockWidget(tr("Project"), this);
    treeDock->setObjectName("dockProject");
    treeDock->setWidget(m_tree);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);

    m_sections = new SectionPanel(m_scene);
    auto *secDock = new QDockWidget(tr("Chapters (filter)"), this);
    secDock->setObjectName("dockChapters");
    secDock->setWidget(m_sections);
    addDockWidget(Qt::LeftDockWidgetArea, secDock);
    splitDockWidget(treeDock, secDock, Qt::Vertical);

    m_library = new LibraryPanel;
    auto *libDock = new QDockWidget(tr("Library"), this);
    libDock->setObjectName("dockLibrary");
    libDock->setWidget(m_library);
    addDockWidget(Qt::RightDockWidgetArea, libDock);

    m_props = new PropertiesPanel(m_scene);
    auto *propDock = new QDockWidget(tr("Properties"), this);
    propDock->setObjectName("dockProperties");
    propDock->setWidget(m_props);
    addDockWidget(Qt::RightDockWidgetArea, propDock);
    splitDockWidget(libDock, propDock, Qt::Vertical);

    // the splitters between the plan and the panels: wide enough to grab with a trackpad
    // (the macOS style makes them 1 px), highlighted under the pointer
    setStyleSheet(QStringLiteral("QMainWindow::separator { background: palette(window); width: 7px; height: 7px; }"
                                 "QMainWindow::separator:hover { background: palette(highlight); }"));
    m_docks = {treeDock, secDock, libDock, propDock};
    resetLayout();
}

void MainWindow::resetLayout()
{
    if (m_docks.size() != 4)
        return;
    for (QDockWidget *d : m_docks) {
        d->setFloating(false);
        d->show();
    }
    resizeDocks({m_docks[0], m_docks[1]}, {640, 300}, Qt::Vertical);
    resizeDocks({m_docks[2], m_docks[3]}, {400, 560}, Qt::Vertical);
    resizeDocks({m_docks[0], m_docks[2]}, {380, 400}, Qt::Horizontal);
}

// ---------------------------------------------------------------- file handling

QString MainWindow::lastDir() const
{
    return QSettings().value("lastDir", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
}

void MainWindow::setLastDir(const QString &path)
{
    QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
}

void MainWindow::updateTitle()
{
    const QString file = m_path.isEmpty() ? tr("untitled") : QFileInfo(m_path).fileName();
    QString floor;
    if (m_project->floorCount() > 1 && m_scene)
        floor = QStringLiteral(" (%1)").arg(m_scene->floorName());
    setWindowTitle(QStringLiteral("%1%2 – %3[*] – Smart Home Planner").arg(m_project->info().name, floor, file));
}

bool MainWindow::maybeSave()
{
    if (!isWindowModified())
        return true;
    const auto r = QMessageBox::warning(this, tr("Unsaved changes"),
                                        tr("The project has unsaved changes. Save them?"),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save)
        return save();
    return r == QMessageBox::Discard;
}

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    m_viewStates.clear();
    m_scene = nullptr;
    m_project->reset();
    bindScene(m_project->current());
    rebuildFloorTabs();
    m_path.clear();
    updateTitle();
    editProjectInfo();
    setWindowModified(false);
}

void MainWindow::openProjectDialog()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open project"), lastDir(),
                                                      tr("Smart Home Planner project (*.shp)"));
    if (!path.isEmpty())
        openProject(path);
}

bool MainWindow::openProject(const QString &path)
{
    QString err;
    m_viewStates.clear();
    m_scene = nullptr;
    if (!SceneSerializer::loadProject(m_project, path, &err)) {
        QMessageBox::critical(this, tr("Open project"), tr("Cannot open %1:\n%2").arg(path, err));
        m_project->reset();
        bindScene(m_project->current());
        rebuildFloorTabs();
        return false;
    }
    bindScene(m_project->current());
    rebuildFloorTabs();
    // the panels were bound while the floors were still empty
    m_sections->updateCounts();
    m_props->rebuild();
    m_tree->resetExpansion();      // project open, floors collapsed
    m_tree->rebuild();
    m_view->zoomToFit();
    m_path = path;
    setLastDir(path);
    addRecent(path);
    setWindowModified(false);
    updateTitle();
    return true;
}

bool MainWindow::save()
{
    if (m_path.isEmpty())
        return saveAs();
    return saveTo(m_path);
}

bool MainWindow::saveAs()
{
    QString suggested = m_path;
    if (suggested.isEmpty())
        suggested = lastDir() + "/" + m_project->info().name + ".shp";
    QString path = QFileDialog::getSaveFileName(this, tr("Save project"), suggested,
                                                tr("Smart Home Planner project (*.shp)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".shp", Qt::CaseInsensitive))
        path += ".shp";
    return saveTo(path);
}

bool MainWindow::saveTo(const QString &path)
{
    QString err;
    if (!SceneSerializer::saveProject(m_project, path, &err)) {
        QMessageBox::critical(this, tr("Save project"), tr("Cannot save %1:\n%2").arg(path, err));
        return false;
    }
    m_path = path;
    setLastDir(path);
    addRecent(path);
    for (PlanScene *s : m_project->floors())
        s->undoStack()->setClean();
    setWindowModified(false);
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(path), 4000);
    return true;
}

bool MainWindow::openLastProject()
{
    QSettings st;
    if (!st.value("reopenLast", true).toBool())
        return false;
    const QString last = st.value("recent").toStringList().value(0);
    if (last.isEmpty())
        return false;
    if (!QFileInfo::exists(last)) {
        // moved or deleted: start with a new project
        statusBar()->showMessage(tr("The last project was not found (%1) – new project.").arg(last), 10000);
        return false;
    }
    if (!openProject(last))
        return false;
    statusBar()->showMessage(tr("Last project opened: %1").arg(QFileInfo(last).fileName()), 8000);
    return true;
}

void MainWindow::addRecent(const QString &path)
{
    QSettings st;
    QStringList list = st.value("recent").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 8)
        list.removeLast();
    st.setValue("recent", list);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    const QStringList list = QSettings().value("recent").toStringList();
    for (const QString &p : list) {
        if (!QFileInfo::exists(p))
            continue;
        m_recentMenu->addAction(QFileInfo(p).fileName(), this, [this, p] {
            if (maybeSave())
                openProject(p);
        })->setToolTip(p);
    }
    m_recentMenu->setEnabled(!m_recentMenu->isEmpty());
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings st;
    st.setValue("window/geometry", saveGeometry());
    st.setValue("window/state3", saveState());
    e->accept();
}

// ---------------------------------------------------------------- dialogs

void MainWindow::editProjectInfo()
{
    ProjectInfoDialog dlg(m_project->info(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_project->info() = dlg.info();
    if (m_project->info().name.isEmpty())
        m_project->info().name = tr("New project");
    emit m_project->modified();
    setWindowModified(true);
    updateTitle();
    m_props->scheduleRebuild();
}

void MainWindow::exportPdf()
{
    ExportDialog dlg(m_project, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString suggested = m_path.isEmpty() ? lastDir() + "/" + m_project->info().name + ".pdf"
                                         : QFileInfo(m_path).absolutePath() + "/" + QFileInfo(m_path).completeBaseName() + ".pdf";
    QString path = QFileDialog::getSaveFileName(this, tr("Export PDF"), suggested, tr("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive))
        path += ".pdf";
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = PdfExporter::exportPdf(m_project, path, dlg.options(), &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, tr("Export PDF"), err);
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(path), 5000);
    if (QMessageBox::question(this, tr("Export PDF"), tr("PDF created. Open it now?")) == QMessageBox::Yes)
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::importBackground(int floorIndex)
{
    if (floorIndex >= 0 && floorIndex != m_project->currentIndex())
        m_project->setCurrent(floorIndex);
    QString filter = tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)");
#ifdef SHP_HAVE_QTPDF
    filter = tr("Floor plans (*.png *.jpg *.jpeg *.bmp *.webp *.pdf);;Images (*.png *.jpg *.jpeg *.bmp *.webp);;PDF (*.pdf)");
#endif
    const QString path = QFileDialog::getOpenFileName(this, tr("Import floor plan") + " – " + m_scene->floorName(),
                                                      lastDir(), filter);
    if (path.isEmpty())
        return;
    setLastDir(path);

    QByteArray data;
    if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
#ifdef SHP_HAVE_QTPDF
        QPdfDocument doc;
        if (doc.load(path) != QPdfDocument::Error::None || doc.pageCount() < 1) {
            QMessageBox::critical(this, tr("Import floor plan"), tr("Cannot read the PDF file."));
            return;
        }
        int page = 0;
        if (doc.pageCount() > 1) {
            bool ok = false;
            page = QInputDialog::getInt(this, tr("Import floor plan"), tr("Page to import:"), 1, 1, doc.pageCount(), 1, &ok) - 1;
            if (!ok)
                return;
        }
        const QSizeF pts = doc.pagePointSize(page);
        const double dpi = 200;
        const QSize px = (pts * dpi / 72.0).toSize();
        QImage img = doc.render(page, px);
        if (img.isNull()) {
            QMessageBox::critical(this, tr("Import floor plan"), tr("Cannot render the PDF page."));
            return;
        }
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        {
            QPainter pp(&flat);
            pp.drawImage(0, 0, img);
        }
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        flat.save(&buf, "PNG");
#endif
    } else {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, tr("Import floor plan"), f.errorString());
            return;
        }
        data = f.readAll();
    }
    if (!m_scene->setBackgroundImage(data)) {
        QMessageBox::critical(this, tr("Import floor plan"), tr("Unsupported image format."));
        return;
    }
    BackgroundItem *bg = m_scene->background();
    // initial guess: the image is ~15 m wide; the user calibrates next
    const double w = bg->pixmap().width();
    bg->setCmPerPixel(w > 0 ? 1500.0 / w : 1.0);
    bg->setPos(0, 0);
    bg->setOpacity(0.7);
    m_scene->resetUndo();
    setWindowModified(true);
    m_view->zoomToFit();
    QMessageBox::information(this, tr("Set the scale"),
                             tr("Now set the scale: click two points on the plan whose real distance you know "
                                "(e.g. the length of a wall), then type the distance in meters."));
    m_scene->setTool(PlanScene::CalibrateTool);
}

void MainWindow::onCalibration(const QPointF &a, const QPointF &b)
{
    const double current = QLineF(a, b).length();
    // two clicks almost on the same spot give a huge scale factor
    if (current * m_view->zoom() < 30) {
        QMessageBox::warning(this, tr("Set the scale"),
                             tr("The two points are too close to each other.\n"
                                "Click two points far apart, e.g. the two ends of the longest wall."));
        return;     // the tool stays active, click again
    }
    bool ok = false;
    const double meters = QInputDialog::getDouble(this, tr("Set the scale"),
                                                  tr("Real distance between the two points, in METERS (e.g. 4.35):"),
                                                  current / 100.0, 0.01, 10000, 3, &ok);
    if (!ok) {
        m_scene->setTool(PlanScene::SelectTool);
        return;
    }
    // sanity check: size of the floor plan after scaling
    if (BackgroundItem *bg = m_scene->background()) {
        const double f = meters * 100.0 / current;
        const QSizeF sz = bg->sceneBoundingRect().size() * f / 100.0;      // m
        const double longest = std::max(sz.width(), sz.height());
        if (longest < 3 || longest > 300) {
            const auto r = QMessageBox::warning(
                this, tr("Set the scale"),
                tr("With this distance the floor plan would be %1 × %2 m.\n"
                   "That does not look right – did you type centimeters instead of meters?\n\nApply anyway?")
                    .arg(sz.width(), 0, 'f', 1).arg(sz.height(), 0, 'f', 1),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (r != QMessageBox::Yes)
                return;     // stay in the scale tool
        }
    }
    bool scaleDrawing = false;
    if (!m_scene->contentItems().isEmpty()) {
        scaleDrawing = QMessageBox::question(this, tr("Set the scale"),
                                             tr("Scale also the walls, rooms and elements already drawn?"))
                       == QMessageBox::Yes;
    }
    m_scene->applyCalibration(a, b, meters * 100.0, scaleDrawing);
    m_scene->setTool(PlanScene::SelectTool);
    statusBar()->showMessage(tr("Scale set: %1 = %2 m").arg(Geo::formatMeters(current)).arg(meters), 6000);
    m_view->zoomToFit();
}

void MainWindow::resetBackgroundScale()
{
    BackgroundItem *bg = m_scene->background();
    if (!bg)
        return;
    const double w = bg->pixmap().width();
    bg->setCmPerPixel(w > 0 ? 1500.0 / w : 1.0);
    bg->setPos(0, 0);
    m_scene->commit(tr("Reset floor plan scale"));
    m_view->zoomToFit();
    statusBar()->showMessage(tr("Floor plan reset to ~15 m width. Set the scale again (K)."), 8000);
}
