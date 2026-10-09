#ifndef SCENESERIALIZER_H
#define SCENESERIALIZER_H

#include <QDomDocument>
#include <QDomElement>
#include <QList>
#include <QPointF>
#include <QString>

class PlanScene;
class Project;
class QGraphicsItem;

// Reads / writes the project file (.shp, XML) and the snapshots used for undo and copy/paste.
namespace SceneSerializer {

constexpr const char *kFileSuffix = "shp";
constexpr const char *kClipboardMime = "application/x-smarthomeplanner-items";

// <Content> with the given items (all content items if 'items' is empty and 'all' is true)
QDomElement writeContent(QDomDocument &doc, const QList<QGraphicsItem *> &items);
// newUids: pasted copies get new uids (links between them are re-pointed)
QList<QGraphicsItem *> readContent(PlanScene *scene, const QDomElement &content,
                                   const QPointF &offset = QPointF(), bool newUids = false);

// Project file: <SmartHomePlan version="2"><Info/><Floor name=".."><Settings/><Background/><Content/></Floor>...
// (version 1 files without <Floor> are read as a single floor)
bool saveProject(Project *project, const QString &path, QString *error = nullptr);
bool loadProject(Project *project, const QString &path, QString *error = nullptr);

QString snapshot(PlanScene *scene);
void    restoreSnapshot(PlanScene *scene, const QString &xml);

QByteArray itemsToClipboard(const QList<QGraphicsItem *> &items);
QList<QGraphicsItem *> itemsFromClipboard(PlanScene *scene, const QByteArray &data, const QPointF &offset);

} // namespace SceneSerializer

#endif // SCENESERIALIZER_H
