#include "Project.h"

#include "scene/PlanScene.h"

Project::Project(QObject *parent) : QObject(parent)
{
    reset();
}

Project::~Project()
{
    clearFloors();
}

QString Project::defaultFloorName(int index) const
{
    return index == 0 ? tr("Ground floor") : tr("Floor %1").arg(index);
}

QString Project::defaultPanelName() const
{
    int n = 0;
    for (PlanScene *s : m_floors)
        n += s->isPanel() ? 1 : 0;
    return n == 0 ? tr("Electrical panel") : tr("Electrical panel %1").arg(n + 1);
}

QList<PlanScene *> Project::panelsOf(const PlanScene *floor) const
{
    QList<PlanScene *> out;
    for (PlanScene *s : m_floors)
        if (s->isPanel() && s->parentFloor() == floor)
            out << s;
    return out;
}

void Project::setPanelFloor(PlanScene *panel, PlanScene *floor)
{
    if (!panel || !panel->isPanel() || panel->parentFloor() == floor)
        return;
    PlanScene *cur = current();
    panel->setParentFloor(floor);
    if (floor) {
        // keep the panel next to its floor
        m_floors.removeAll(panel);
        int at = m_floors.indexOf(floor) + 1;
        while (at < m_floors.size() && m_floors[at]->isPanel() && m_floors[at]->parentFloor() == floor)
            ++at;
        m_floors.insert(at, panel);
        m_current = m_floors.indexOf(cur);
    }
    emit floorsChanged();
    emit modified();
}

PlanScene *Project::addFloor(const QString &name, int kind, PlanScene *parentFloor)
{
    auto *s = new PlanScene(this);
    s->setSharedInfo(m_info);
    s->setPageKind(kind == PlanScene::PanelPage ? PlanScene::PanelPage : PlanScene::FloorPage);
    s->setFloorName(name.isEmpty() ? (s->isPanel() ? defaultPanelName() : defaultFloorName(m_floors.size())) : name);
    if (PlanScene *cur = current(); cur && !cur->isPanel() && !s->isPanel())
        s->settings() = cur->settings();          // same ceiling height, symbol scale ...
    s->resetUndo();
    if (s->isPanel() && parentFloor && m_floors.contains(parentFloor)) {
        s->setParentFloor(parentFloor);
        int at = m_floors.indexOf(parentFloor) + 1;
        while (at < m_floors.size() && m_floors[at]->isPanel() && m_floors[at]->parentFloor() == parentFloor)
            ++at;
        PlanScene *cur = current();
        m_floors.insert(at, s);
        if (cur)
            m_current = m_floors.indexOf(cur);
    } else {
        m_floors.append(s);
    }
    m_undoGroup.addStack(s->undoStack());
    connect(s, &PlanScene::modified, this, &Project::modified);
    emit floorsChanged();
    if (m_current < 0)
        setCurrent(0);
    return s;
}

void Project::removeFloor(int i)
{
    if (i < 0 || i >= m_floors.size() || m_floors.size() <= 1)
        return;
    PlanScene *s = m_floors.takeAt(i);
    for (PlanScene *p : m_floors)
        if (p->parentFloor() == s)
            p->setParentFloor(nullptr);         // its panels stay, without floor
    m_undoGroup.removeStack(s->undoStack());
    if (m_current >= m_floors.size())
        m_current = m_floors.size() - 1;
    else if (i < m_current)
        --m_current;
    const int cur = m_current;
    m_current = -1;
    setCurrent(cur);
    s->deleteLater();
    emit floorsChanged();
    emit modified();
}

void Project::moveFloor(int i, int delta)
{
    const int j = i + delta;
    if (i < 0 || j < 0 || i >= m_floors.size() || j >= m_floors.size())
        return;
    PlanScene *cur = current();
    m_floors.move(i, j);
    m_current = m_floors.indexOf(cur);
    emit floorsChanged();
    emit modified();
}

void Project::renameFloor(int i, const QString &name)
{
    if (PlanScene *s = floor(i)) {
        if (name.trimmed().isEmpty() || s->floorName() == name.trimmed())
            return;
        s->setFloorName(name.trimmed());
        emit floorsChanged();
        emit modified();
    }
}

void Project::setCurrent(int i)
{
    if (i < 0 || i >= m_floors.size() || i == m_current)
        return;
    m_current = i;
    m_undoGroup.setActiveStack(m_floors[i]->undoStack());
    emit currentChanged(m_floors[i]);
}

void Project::clearFloors()
{
    const QList<PlanScene *> old = m_floors;
    m_floors.clear();
    m_current = -1;
    for (PlanScene *s : old) {
        m_undoGroup.removeStack(s->undoStack());
        disconnect(s, nullptr, this, nullptr);
        s->deleteLater();
    }
}

void Project::reset()
{
    clearFloors();
    *m_info = ProjectInfo();
    m_info->name = tr("New project");
    addFloor(defaultFloorName(0));
    emit floorsChanged();
}
