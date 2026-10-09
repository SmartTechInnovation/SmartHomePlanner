#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "scene/PlanScene.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QTransform>

class Project;
class PlanView;
class LibraryPanel;
class PropertiesPanel;
class SectionPanel;
class ProjectTree;
class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QTabBar;
class QToolBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openProject(const QString &path);
    // opens the most recent project if it still exists (setting "reopenLast"); false = keep the new one
    bool openLastProject();
    Project *project() const { return m_project; }

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void newProject();
    void openProjectDialog();
    bool save();
    bool saveAs();
    void exportPdf();
    void importBackground(int floorIndex = -1);
    void editProjectInfo();
    void addFloor();
    void addPanel();
    void addPanelTo(PlanScene *floor, const QPointF &symbolAt = QPointF(), bool placeSymbol = false);
    void reloadLibrary();
    void deleteFloor(int index);
    void onCalibration(const QPointF &a, const QPointF &b);
    void resetBackgroundScale();
    void updateTitle();
    void bindScene(PlanScene *scene);
    void rebuildFloorTabs();
    void focusItem(QGraphicsItem *item);

private:
    void createActions();
    void applyPageKind();
    void resetLayout();             // default sizes of the side panels
    void createMenus();
    void createToolBars();
    void createDocks();
    QWidget *createCentral();
    bool maybeSave();
    bool saveTo(const QString &path);
    void addRecent(const QString &path);
    void rebuildRecentMenu();
    QString lastDir() const;
    void setLastDir(const QString &path);
    QIcon icon(const char *name) const;

    Project *m_project;
    PlanScene *m_scene = nullptr;          // current floor
    QPointer<PlanScene> m_bound;           // scene whose signals are connected
    PlanView *m_view;
    LibraryPanel *m_library;
    PropertiesPanel *m_props;
    SectionPanel *m_sections;
    ProjectTree *m_tree;
    QTabBar *m_floorTabs;
    QToolBar *m_toolsBar;
    QLabel *m_posLabel;
    QLabel *m_zoomLabel;
    QString m_path;
    bool m_updatingTabs = false;

    struct ViewState { QTransform transform; QPointF center; };
    QHash<PlanScene *, ViewState> m_viewStates;

    QHash<QString, QAction *> m_act;
    QList<class QDockWidget *> m_docks;
    QActionGroup *m_toolGroup;
    QMenu *m_recentMenu = nullptr;
};

#endif // MAINWINDOW_H
