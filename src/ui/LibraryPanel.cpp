#include "LibraryPanel.h"

#include "core/IconCache.h"
#include "core/Library.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QToolButton>
#include <QLineEdit>
#include <QMimeData>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int KeyRole = Qt::UserRole + 1;
constexpr int SearchRole = Qt::UserRole + 2;
constexpr int PanelRole = Qt::UserRole + 3;     // 1 = panel part, 0 = floor plan element
constexpr const char *kEntryMime = "application/x-smarthomeplanner-entry";

class LibraryTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    QStringList mimeTypes() const override { return {kEntryMime}; }
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        if (items.isEmpty())
            return nullptr;
        const QString key = items.first()->data(0, KeyRole).toString();
        if (key.isEmpty())
            return nullptr;
        auto *m = new QMimeData;
        m->setData(kEntryMime, key.toUtf8());
        return m;
    }
};

QString kindText(DeviceKind k)
{
    switch (k) {
    case DeviceKind::Light:   return QObject::tr("Light");
    case DeviceKind::Speaker: return QObject::tr("Speaker");
    case DeviceKind::Wifi:    return QObject::tr("Wi-Fi access point");
    case DeviceKind::Strip:   return QObject::tr("LED strip");
    default:                  return QObject::tr("Element");
    }
}

} // namespace

LibraryPanel::LibraryPanel(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search elements..."));
    m_search->setClearButtonEnabled(true);
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(m_search, 1);
    auto *reloadBtn = new QToolButton;
    reloadBtn->setIcon(IconCache::instance().icon(QStringLiteral(":/resources/icons/reload.svg")));
    reloadBtn->setToolTip(tr("Reload the library (after editing the XML files) – F5"));
    reloadBtn->setAutoRaise(true);
    connect(reloadBtn, &QToolButton::clicked, this, &LibraryPanel::reloadRequested);
    top->addWidget(reloadBtn);
    lay->addLayout(top);

    m_tree = new LibraryTree;
    m_tree->setHeaderHidden(true);
    m_tree->setIconSize(QSize(28, 28));
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setUniformRowHeights(true);
    lay->addWidget(m_tree, 1);

    connect(m_search, &QLineEdit::textChanged, this, &LibraryPanel::applyFilter);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        const QString key = it->data(0, KeyRole).toString();
        if (!key.isEmpty())
            emit entryActivated(key);
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        if (it)
            emit currentEntryChanged(it->data(0, KeyRole).toString());
    });
    reload();
}

QString LibraryPanel::currentKey() const
{
    return m_tree->currentItem() ? m_tree->currentItem()->data(0, KeyRole).toString() : QString();
}

void LibraryPanel::reload()
{
    m_tree->clear();
    for (const auto &root : Library::instance().roots())
        addNode(nullptr, root.get());
    if (m_tree->topLevelItemCount() == 1)
        m_tree->topLevelItem(0)->setExpanded(true);
    applyFilter(m_search->text());
}

void LibraryPanel::addNode(QTreeWidgetItem *parent, const LibraryNode *node)
{
    auto *it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    it->setText(0, node->name);
    if (!node->iconPath.isEmpty())
        it->setIcon(0, IconCache::instance().icon(node->iconPath));
    QString search = node->name;
    if (node->entry) {
        const LibraryEntry *e = node->entry;
        it->setData(0, KeyRole, e->key);
        it->setData(0, PanelRole, e->isPanelPart() ? 1 : 0);
        it->setFlags(it->flags() | Qt::ItemIsDragEnabled);
        QString tip = QStringLiteral("<b>%1</b><br>%2: %3<br>%4: %5")
                          .arg(e->displayName().toHtmlEscaped(),
                               tr("Chapter"), Library::instance().sectionName(e->section).toHtmlEscaped(),
                               tr("Type"), kindText(e->kind));
        if (e->isPanelPart())
            tip += QStringLiteral("<br>%1 × %2 mm").arg(e->width).arg(e->height)
                   + (e->ports.isEmpty() ? QString() : QStringLiteral(", ") + tr("%n terminal(s)", nullptr, int(e->ports.size())));
        if (!e->note.isEmpty())
            tip += QStringLiteral("<br>%1: %2").arg(tr("Default note"), e->note.toHtmlEscaped());
        it->setToolTip(0, tip);
        search += " " + e->variant + " " + e->note + " " + Library::instance().sectionName(e->section);
    } else {
        it->setFlags(it->flags() & ~Qt::ItemIsDragEnabled);
        QFont f = it->font(0);
        f.setBold(true);
        it->setFont(0, f);
    }
    it->setData(0, SearchRole, search);
    for (const auto &child : node->children)
        addNode(it, child.get());
}

void LibraryPanel::setPanelMode(bool panel)
{
    if (m_panelMode == panel)
        return;
    m_panelMode = panel;
    applyFilter(m_search->text());
}

void LibraryPanel::applyFilter(const QString &text)
{
    const QString t = text.trimmed();
    // returns true when the item stays visible
    std::function<bool(QTreeWidgetItem *, bool)> filter = [&](QTreeWidgetItem *it, bool parentMatch) -> bool {
        const bool selfMatch = t.isEmpty() || it->data(0, SearchRole).toString().contains(t, Qt::CaseInsensitive);
        const bool isEntry = !it->data(0, KeyRole).toString().isEmpty();
        const bool modeOk = !isEntry || it->data(0, PanelRole).toInt() == (m_panelMode ? 1 : 0);
        bool anyChild = false;
        for (int i = 0; i < it->childCount(); ++i)
            anyChild |= filter(it->child(i), parentMatch || selfMatch);
        bool visible;
        if (isEntry)
            visible = modeOk && (selfMatch || parentMatch || anyChild);
        else
            visible = anyChild;     // a category is shown when it has visible elements
        it->setHidden(!visible);
        if (!t.isEmpty() && anyChild)
            it->setExpanded(true);
        return visible;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_tree->topLevelItem(i);
        filter(top, false);
        if (m_panelMode && !top->isHidden())
            top->setExpanded(true);
    }
}
