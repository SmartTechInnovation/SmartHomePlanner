#ifndef SECTIONPANEL_H
#define SECTIONPANEL_H

#include <QWidget>

class PlanScene;
class QListWidget;
class QListWidgetItem;

// Chapter filter: check which chapters (Lighting, Sensors, Music ...) are visible on the plan.
class SectionPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SectionPanel(PlanScene *scene, QWidget *parent = nullptr);
    void setScene(PlanScene *scene);
    void rebuild();
    void updateCounts();

private:
    PlanScene *m_scene = nullptr;
    QListWidget *m_list;
    bool m_updating = false;
};

#endif // SECTIONPANEL_H
