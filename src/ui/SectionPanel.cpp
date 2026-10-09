#include "SectionPanel.h"
#include "CheckDelegate.h"

#include "core/Library.h"
#include "scene/DeviceItem.h"
#include "scene/PanelItems.h"
#include "scene/PlanScene.h"
#include "scene/StripItem.h"

#include <QApplication>
#include <QCursor>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QStyledItemDelegate>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int IdRole = Qt::UserRole + 1;
constexpr int CountRole = CheckDelegate::CountRole;
constexpr int ColorRole = CheckDelegate::ColorRole;

} // namespace

SectionPanel::SectionPanel(PlanScene *scene, QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    m_list = new QListWidget;
    m_list->setItemDelegate(new CheckDelegate(m_list, false));   // the box toggles, double-click on a name = only this
    m_list->setMouseTracking(true);
    m_list->setToolTip(tr("Tick the chapters visible on the plan. Double-click a name = show only this chapter."));
    lay->addWidget(m_list, 1);
    auto *btns = new QHBoxLayout;
    auto *all = new QPushButton(tr("All"));
    auto *none = new QPushButton(tr("None"));
    btns->addWidget(all);
    btns->addWidget(none);
    lay->addLayout(btns);

    connect(all, &QPushButton::clicked, this, [this] { m_scene->setHiddenSections({}); });
    connect(none, &QPushButton::clicked, this, [this] {
        QSet<QString> all;
        for (const SectionDef &s : Library::instance().sections())
            all.insert(s.id);
        m_scene->setHiddenSections(all);
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        if (m_updating)
            return;
        m_scene->setSectionVisible(it->data(IdRole).toString(), it->checkState() == Qt::Checked);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        // a double click on the check box only toggles it
        if (m_list->viewport()->mapFromGlobal(QCursor::pos()).x() < CheckDelegate::kBoxZone)
            return;
        QSet<QString> hidden;
        for (const SectionDef &s : Library::instance().sections())
            if (s.id != it->data(IdRole).toString())
                hidden.insert(s.id);
        m_scene->setHiddenSections(hidden);
    });
    rebuild();
    setScene(scene);
}

void SectionPanel::setScene(PlanScene *scene)
{
    if (m_scene)
        disconnect(m_scene, nullptr, this, nullptr);
    m_scene = scene;
    connect(m_scene, &PlanScene::sectionsChanged, this, &SectionPanel::updateCounts);
    connect(m_scene, &PlanScene::modified, this, &SectionPanel::updateCounts);
    updateCounts();
}

void SectionPanel::rebuild()
{
    m_updating = true;
    m_list->clear();
    for (const SectionDef &s : Library::instance().sections()) {
        auto *it = new QListWidgetItem(s.name);
        it->setData(IdRole, s.id);
        it->setData(ColorRole, s.color);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        m_list->addItem(it);
    }
    m_updating = false;
    updateCounts();
}

void SectionPanel::updateCounts()
{
    if (!m_scene)
        return;
    QHash<QString, int> counts;
    for (DeviceItem *d : m_scene->devices())
        counts[d->data().section]++;
    for (StripItem *s : m_scene->strips())
        counts[s->data().section]++;
    for (EquipmentItem *e : m_scene->equipment())
        counts[e->data().section]++;
    m_updating = true;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *it = m_list->item(i);
        const QString id = it->data(IdRole).toString();
        const int n = counts.value(id);
        it->setText(Library::instance().sectionName(id));
        it->setData(CountRole, n);
        it->setCheckState(m_scene->isSectionVisible(id) ? Qt::Checked : Qt::Unchecked);
    }
    m_updating = false;
}
