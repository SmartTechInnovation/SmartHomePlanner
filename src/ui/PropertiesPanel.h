#ifndef PROPERTIESPANEL_H
#define PROPERTIESPANEL_H

#include <QList>
#include <QPointer>
#include <QScrollArea>
#include <functional>

class PlanScene;
class DeviceItem;
class StripItem;
class WallItem;
class RoomItem;
class GroupItem;
class RailItem;
class EquipmentItem;
class WireItem;
class QFormLayout;
class QLabel;
struct DeviceData;

// Right-hand panel: properties of the selected element(s), or of the project when nothing is selected.
// Edits are applied to every selected element of the same kind (e.g. set the beam of 10 spots at once).
class PropertiesPanel : public QScrollArea
{
    Q_OBJECT
public:
    explicit PropertiesPanel(PlanScene *scene, QWidget *parent = nullptr);
    void setScene(PlanScene *scene);

public slots:
    void rebuild();
    void scheduleRebuild();

signals:
    void editProjectInfo();

private:
    void buildProject();
    void buildDevices();
    void buildStrips();
    void buildWalls();
    void buildRooms();
    void buildGroups();
    void buildEquipment();
    void buildWires();
    void buildRails();
    void addGroupInfo(const QString &uid);      // "member of" rows for a single element
    void buildCommon(const DeviceData &d, bool single);
    void addHeader(const QString &title, const QString &subtitle, const QString &iconPath = QString());
    void addSeparator(const QString &title);

    // apply to all selected devices + strips (common data fields)
    void editData(const QString &label, const std::function<void(DeviceData &)> &fn);
    void editDevices(const QString &label, const std::function<void(DeviceItem *)> &fn);
    void editStrips(const QString &label, const std::function<void(StripItem *)> &fn);

    PlanScene *m_scene = nullptr;
    QMetaObject::Connection m_undoConn;
    QPointer<QWidget> m_content;
    QFormLayout *m_form = nullptr;
    QList<DeviceItem *> m_devices;
    QList<StripItem *> m_strips;
    QList<WallItem *> m_walls;
    QList<RoomItem *> m_rooms;
    QList<GroupItem *> m_groups;
    QList<EquipmentItem *> m_equipment;
    QList<WireItem *> m_wires;
    QList<RailItem *> m_rails;
    bool m_applying = false;
    bool m_rebuildPending = false;
};

#endif // PROPERTIESPANEL_H
