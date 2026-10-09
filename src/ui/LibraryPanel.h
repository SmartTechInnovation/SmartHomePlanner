#ifndef LIBRARYPANEL_H
#define LIBRARYPANEL_H

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
struct LibraryNode;

// Tree of library elements. Drag an element onto the plan, or double-click to place it.
class LibraryPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LibraryPanel(QWidget *parent = nullptr);
    void reload();
    QString currentKey() const;
    // panel pages show only panel equipment (DIN rail parts); floor plans everything else
    void setPanelMode(bool panel);
    bool panelMode() const { return m_panelMode; }

signals:
    void entryActivated(const QString &key);       // double-click
    void currentEntryChanged(const QString &key);
    void reloadRequested();                         // read the XML files again

private:
    void addNode(QTreeWidgetItem *parent, const LibraryNode *node);
    void applyFilter(const QString &text);
    QLineEdit *m_search;
    QTreeWidget *m_tree;
    bool m_panelMode = false;
};

#endif // LIBRARYPANEL_H
