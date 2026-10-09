#ifndef PLANSCENE_H
#define PLANSCENE_H

#include "core/Library.h"
#include "core/ProjectInfo.h"

#include <QGraphicsScene>
#include <QLineF>
#include <QPointer>
#include <QPolygonF>
#include <QSet>
#include <QUndoStack>
#include <QVector>

#include <memory>

class BackgroundItem;
class DeviceItem;
class GroupItem;
class RailItem;
class EquipmentItem;
class WireItem;
class StripItem;
class WallItem;
class RoomItem;
class NightItem;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;

class PlanScene : public QGraphicsScene
{
    Q_OBJECT
public:
    enum Tool { SelectTool, WallTool, RoomTool, StripTool, StripCircleTool, CalibrateTool, MeasureTool,
                RailTool, WireTool, GroupTool };
    Q_ENUM(Tool)
    // a floor plan (cm) or an electrical panel (mm: DIN rails, equipment, wires)
    enum PageKind { FloorPage, PanelPage };

    explicit PlanScene(QObject *parent = nullptr);
    ~PlanScene() override;

    // ---- project data
    // project data is shared by all the floors of a project
    ProjectInfo &info() { return *m_info; }
    const ProjectInfo &info() const { return *m_info; }
    void setSharedInfo(const std::shared_ptr<ProjectInfo> &info) { m_info = info; }
    PageKind pageKind() const { return m_kind; }
    void setPageKind(PageKind k);
    bool isPanel() const { return m_kind == PanelPage; }
    // electrical panel: the floor it is on (may be null)
    PlanScene *parentFloor() const { return m_parentFloor.data(); }
    void setParentFloor(PlanScene *floor) { m_parentFloor = floor; }
    QString floorName() const { return m_floorName; }
    void setFloorName(const QString &n) { m_floorName = n; }
    ProjectSettings &settings() { return m_settings; }
    const ProjectSettings &settings() const { return m_settings; }
    void newProject();

    // ---- background plan
    BackgroundItem *background() const { return m_background; }
    bool setBackgroundImage(const QByteArray &imageData);
    void removeBackground();

    // ---- tools
    Tool tool() const { return m_tool; }
    void setTool(Tool t);
    void setStripEntry(const QString &key) { m_stripKey = key; }
    QString stripEntry() const { return m_stripKey; }
    void cancelDrawing();
    void finishDrawing();
    bool isDrawing() const { return !m_drawPts.isEmpty(); }

    // Rooms can be selected / edited only with the Room tool, walls only with the Wall tool,
    // elements (devices, LED strips) only with the Select tool.
    bool isEditableType(int itemType) const;
    void applyEditability(QGraphicsItem *item);

    // ---- display options
    bool isExporting() const { return m_exporting; }
    bool exportEffects() const { return m_exportEffects; }
    void setExporting(bool on, bool effects = true);
    bool nightMode() const { return m_night; }
    void setNightMode(bool on);
    bool showBeams() const { return m_showBeams; }
    void setShowBeams(bool on);
    bool showNotes() const { return m_showNotes; }
    void setShowNotes(bool on);
    // room name / area labels on the plan (screen only; the PDF export always shows them)
    bool showRoomNames() const { return m_showRoomNames; }
    void setShowRoomNames(bool on);
    bool showRoomAreas() const { return m_showRoomAreas; }
    void setShowRoomAreas(bool on);
    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool on) { m_showGrid = on; update(); }
    // snap to wall corners / wall faces / room corners
    bool snapEnabled() const { return m_snap; }
    void setSnapEnabled(bool on) { m_snap = on; }
    // snap to the grid (off by default: with a floor plan image it pulls points off the drawn lines)
    bool gridSnapEnabled() const { return m_gridSnap; }
    void setGridSnapEnabled(bool on) { m_gridSnap = on; }
    enum SnapKind { NoSnap, CornerSnap, EdgeSnap, GridSnap };
    void showSnapMarker(const QPointF &p, SnapKind kind);     // NoSnap hides it

    // ---- chapters / sections filter
    bool isSectionVisible(const QString &id) const { return !m_hiddenSections.contains(id); }
    void setSectionVisible(const QString &id, bool visible);
    const QSet<QString> &hiddenSections() const { return m_hiddenSections; }
    void setHiddenSections(const QSet<QString> &hidden);
    QSet<QString> usedSections() const;

    // ---- geometry helpers
    QVector<QLineF> wallObstacles();
    void notifyWallsChanged();
    // Snap order: corners -> (faces) point on a wall face -> (grid) grid.
    //   faces = false: wall centre-line vertices (to join walls)
    //   faces = true : corners and faces of the wall bodies (rooms sit exactly on the inner face)
    QPointF snapPoint(const QPointF &p, const QGraphicsItem *ignore = nullptr, bool grid = true,
                      bool faces = false, SnapKind *kind = nullptr) const;
    QPointF snapToGrid(const QPointF &p) const;
    // alignment ("smart guides"): x / y of p taken from the nearest reference within a few pixels;
    // magenta guide lines show what it lines up with
    QPointF alignToAxes(const QPointF &p, const QVector<QPointF> &refs) const;
    QVector<QPointF> alignRefs(const QGraphicsItem *ignore, bool devices, bool outlines) const;
    QPointF alignDevice(const QGraphicsItem *moving, const QPointF &pos) const;
    void hideGuides() const;
    void setViewScale(double s) { m_viewScale = s; }
    double viewScale() const { return m_viewScale; }

    // ---- content
    // places a library element: device / LED strip on a floor, equipment / DIN rail on a panel
    void placeEntry(const LibraryEntry &e, const QPointF &scenePos);
    DeviceItem *addDevice(const LibraryEntry &e, const QPointF &scenePos);
    StripItem  *addStripLine(const LibraryEntry &e, const QPolygonF &scenePts);
    StripItem  *addStripCircle(const LibraryEntry &e, const QPointF &center, double radius);
    QString nextId(const QString &section) const;
    void renumberIds();
    QList<QGraphicsItem *> contentItems() const;      // walls, rooms, devices, strips
    QList<DeviceItem *> devices() const;
    QList<StripItem *> strips() const;
    QList<RoomItem *> rooms() const;
    QList<WallItem *> walls() const;
    QGraphicsItem *itemByUid(const QString &uid) const;    // device, LED strip or panel equipment

    // ---- groups (elements linked by a curved dashed line)
    QList<GroupItem *> groups() const;
    QList<GroupItem *> groupsOf(const QString &uid) const;
    GroupItem *groupSelection();                    // group of the selected elements (commit)
    void addSelectionToGroup(GroupItem *g);
    void removeFromGroup(GroupItem *g, const QString &uid);
    QString nextGroupName() const;
    bool showGroups() const { return m_showGroups; }
    void setShowGroups(bool on);
    static QString uidOf(const QGraphicsItem *it);  // uid of a device / strip / equipment

    // ---- electrical panel
    QList<RailItem *> rails() const;
    QList<EquipmentItem *> equipment() const;
    QList<WireItem *> wires() const;
    QList<WireItem *> wiresOf(const QString &uid) const;
    RailItem *addRail(const QPointF &topLeft, double length);
    EquipmentItem *addEquipment(const LibraryEntry &e, const QPointF &scenePos);
    WireItem *addWire(EquipmentItem *a, int portA, EquipmentItem *b, int portB);
    QPointF snapEquipment(EquipmentItem *e, const QPointF &pos) const;   // onto a DIN rail
    QColor newWireColor() const { return m_wireColor; }
    void setNewWireColor(const QColor &c) { m_wireColor = c; }
    QString newWireSection() const { return m_wireSection; }
    void setNewWireSection(const QString &s) { m_wireSection = s; }

    // groups and wires follow their elements (coalesced)
    void scheduleLinkUpdate();
    void updateLinks();
    void updateHighlight();         // panel: connections of the selected equipment
    QRectF contentRect() const;
    void clearContent();
    void refreshAll();                  // after settings change

    void deleteSelection();
    void duplicateSelection();
    void rotateSelection(double deg);
    void setAllLights(bool on);
    void copySelection();
    void paste(const QPointF &at);
    QPointF lastMousePos() const { return m_lastMouse; }

    // ---- calibration
    void applyCalibration(const QPointF &a, const QPointF &b, double realCm, bool scaleDrawing);

    // ---- undo
    QUndoStack *undoStack() { return &m_undo; }
    void commit(const QString &label);
    void resetUndo();
    void beginRestore() { ++m_restoring; }
    void endRestore() { --m_restoring; }
    bool isRestoring() const { return m_restoring > 0; }
    void setLastSnapshot(const QString &s) { m_lastSnapshot = s; }
    void notifyItemEdited(QGraphicsItem *item);

    // right-click menu (short click without dragging) for the item under the cursor
    void showContextMenu(const QPointF &scenePos, const QPoint &screenPos);

signals:
    void addPanelRequested(const QPointF &scenePos);   // right click on an empty place of a floor
    void modified();
    void toolChanged(PlanScene::Tool tool);
    void statusMessage(const QString &msg);
    void cursorMoved(const QPointF &scenePos);
    void calibrationPicked(const QPointF &a, const QPointF &b);
    void sectionsChanged();
    void itemEdited();
    void backgroundChanged();

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void drawBackground(QPainter *p, const QRectF &rect) override;
    void dragEnterEvent(QGraphicsSceneDragDropEvent *e) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent *e) override;
    void dropEvent(QGraphicsSceneDragDropEvent *e) override;

private:
    bool isDrawingTool() const;
    EquipmentItem *portUnder(const QPointF &scenePos, int *port) const;
    void wirePress(const QPointF &scenePos);
    QGraphicsItem *linkableAt(const QPointF &scenePos) const;
    void linkPress(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void showLinkPreview(const QPointF &cursor);
    GroupItem *linkGroup() const;
    QString m_linkFrom;                 // group tool: element the next link starts from
    GroupItem *m_linkGroup = nullptr;   // group the links go into (validated before use)
    void showWirePreview(const QPointF &cursor);
    PageKind m_kind = FloorPage;
    QPointer<PlanScene> m_parentFloor;
    bool m_linkPending = false;
    bool m_showGroups = true;
    QString m_wireFromUid;
    int m_wireFromPort = -1;
    QPointF m_wirePressPos;
    QColor m_wireColor = QColor(139, 69, 19);
    QString m_wireSection = QStringLiteral("1.5");
    void updatePreview(const QPointF &cursor);
    QPointF toolPoint(const QPointF &raw, Qt::KeyboardModifiers mods) const;
    void clearMeasure();
    bool editPress(QGraphicsSceneMouseEvent *e);     // Room / Wall tool: click on an existing outline
    QGraphicsItem *outlineAt(const QPointF &scenePos, int itemType) const;
    bool m_editing = false;
    bool m_showRoomNames = true;
    bool m_showRoomAreas = true;

    std::shared_ptr<ProjectInfo> m_info = std::make_shared<ProjectInfo>();
    QString         m_floorName;
    ProjectSettings m_settings;
    BackgroundItem *m_background = nullptr;
    NightItem      *m_nightItem = nullptr;

    Tool m_tool = SelectTool;
    QString m_stripKey;
    QPolygonF m_drawPts;
    QGraphicsPathItem *m_preview = nullptr;
    QGraphicsPathItem *m_measureLine = nullptr;
    QGraphicsSimpleTextItem *m_measureText = nullptr;
    QPointF m_lastMouse;

    bool m_exporting = false;
    bool m_exportEffects = true;
    bool m_night = false;
    bool m_showBeams = false;
    bool m_showNotes = true;
    bool m_showGrid = true;
    bool m_snap = true;
    bool m_gridSnap = false;
    mutable QVector<QPointF> m_faceCorners;
    mutable QVector<QLineF> m_faceEdges;
    mutable bool m_facesDirty = true;
    void updateFaces() const;
    QGraphicsPathItem *m_snapMarker = nullptr;
    mutable QGraphicsPathItem *m_guides = nullptr;
    mutable SnapKind m_lastSnap = NoSnap;
    double m_viewScale = 1.0;
    QSet<QString> m_hiddenSections;

    QVector<QLineF> m_obstacles;
    bool m_obstaclesDirty = true;
    bool m_wallNotifyPending = false;

    QUndoStack m_undo;
    QString m_lastSnapshot;
    int m_restoring = 0;
};

#endif // PLANSCENE_H
