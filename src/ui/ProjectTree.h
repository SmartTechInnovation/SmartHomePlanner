#ifndef PROJECTTREE_H
#define PROJECTTREE_H

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTreeWidget>

class Project;
class PlanScene;
class QGraphicsItem;

// Project structure (like the original Grid Planner tree):
//   Project
//    └ Floor (one plan per floor, editable name)
//       ├ Room – area m²            (elements are assigned to the room that contains them)
//       │   └ Chapter (count)
//       │       └ ID – element name
//       ├ Outside / no room
//       └ ...
//    └ + Add floor
// Click = go to the floor and select; double-click on an element = zoom to it.
class ProjectTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit ProjectTree(Project *project, QWidget *parent = nullptr);

public slots:
    void scheduleRebuild();
    void rebuild();
    // after opening / creating a project: only the project node open, floors collapsed
    void resetExpansion();

signals:
    void focusItem(QGraphicsItem *item);        // ask the view to center on an item
    void addFloorRequested();
    void addPanelRequested(int floorIndex);
    void deleteFloorRequested(int index);
    void importPlanRequested(int index);
    void editProjectInfo();

protected:
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    enum Kind { KProject = 1, KFloor, KRoom, KOutside, KChapter, KElement, KAddFloor };
    void onClicked(QTreeWidgetItem *it);
    void onDoubleClicked(QTreeWidgetItem *it);
    void onChanged(QTreeWidgetItem *it, int column);
    void syncSelectionFromScene();
    void watchFloors();
    QString pathKey(QTreeWidgetItem *it) const;

    Project *m_project;
    QHash<QGraphicsItem *, QTreeWidgetItem *> m_byGraphicsItem;
    QList<QPointer<PlanScene>> m_watched;
    bool m_rebuildPending = false;
    bool m_building = false;
    bool m_syncing = false;
    bool m_initialized = false;
    bool m_fresh = true;        // next rebuild: nothing expanded except the project
};

#endif // PROJECTTREE_H
