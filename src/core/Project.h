#ifndef PROJECT_H
#define PROJECT_H

#include "ProjectInfo.h"

#include <QList>
#include <QObject>
#include <QUndoGroup>
#include <memory>

class PlanScene;

// A project = shared information + one plan (PlanScene) per floor.
// Every floor has its own floor plan image, scale, walls, rooms, elements and undo history.
class Project : public QObject
{
    Q_OBJECT
public:
    explicit Project(QObject *parent = nullptr);
    ~Project() override;

    ProjectInfo &info() { return *m_info; }
    const ProjectInfo &info() const { return *m_info; }

    int floorCount() const { return m_floors.size(); }
    PlanScene *floor(int i) const { return m_floors.value(i); }
    const QList<PlanScene *> &floors() const { return m_floors; }
    int indexOf(const PlanScene *s) const { return m_floors.indexOf(const_cast<PlanScene *>(s)); }

    PlanScene *current() const { return m_floors.value(m_current); }
    int currentIndex() const { return m_current; }
    void setCurrent(int i);

    // kind: PlanScene::FloorPage or PlanScene::PanelPage
    // a panel with a parent floor is placed right after that floor (and its other panels)
    PlanScene *addFloor(const QString &name, int kind = 0, PlanScene *parentFloor = nullptr);
    void setPanelFloor(PlanScene *panel, PlanScene *floor);
    QList<PlanScene *> panelsOf(const PlanScene *floor) const;
    void removeFloor(int i);
    void moveFloor(int i, int delta);
    void renameFloor(int i, const QString &name);

    // Back to a new, empty project with one floor
    void reset();
    // Remove all floors (used before loading a file); addFloor() must follow
    void clearFloors();

    QUndoGroup *undoGroup() { return &m_undoGroup; }
    QString defaultFloorName(int index) const;
    QString defaultPanelName() const;

signals:
    void floorsChanged();
    void currentChanged(PlanScene *scene);
    void modified();

private:
    std::shared_ptr<ProjectInfo> m_info = std::make_shared<ProjectInfo>();
    QList<PlanScene *> m_floors;
    int m_current = -1;
    QUndoGroup m_undoGroup;
};

#endif // PROJECT_H
