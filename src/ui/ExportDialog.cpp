#include "ExportDialog.h"
#include "CheckDelegate.h"
#include "core/IconCache.h"

#include "core/Library.h"
#include "core/Project.h"
#include "scene/PlanScene.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

ExportDialog::ExportDialog(Project *project, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Export PDF"));
    auto *lay = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    m_page = new QComboBox;
    m_page->addItem("A4", int(QPageSize::A4));
    m_page->addItem("A3", int(QPageSize::A3));
    m_page->addItem("A2", int(QPageSize::A2));
    m_page->setCurrentIndex(1);
    m_orientation = new QComboBox;
    m_orientation->addItem(tr("Landscape"), int(QPageLayout::Landscape));
    m_orientation->addItem(tr("Portrait"), int(QPageLayout::Portrait));
    form->addRow(tr("Page size"), m_page);
    form->addRow(tr("Orientation"), m_orientation);
    lay->addLayout(form);

    auto *floorsBox = new QGroupBox(tr("Floors"));
    auto *fl = new QVBoxLayout(floorsBox);
    m_floors = new QListWidget;
    m_floors->setItemDelegate(new CheckDelegate(m_floors));
    for (int i = 0; i < project->floorCount(); ++i) {
        auto *it = new QListWidgetItem(project->floor(i)->floorName());
        it->setIcon(IconCache::instance().icon(QStringLiteral(":/resources/icons/%1.svg")
                                                   .arg(project->floor(i)->isPanel() ? "panel" : "floor")));
        it->setData(Qt::UserRole, i);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
        m_floors->addItem(it);
    }
    m_floors->setMaximumHeight(std::min(130, 26 * project->floorCount() + 8));
    fl->addWidget(m_floors);
    lay->addWidget(floorsBox);

    auto *pages = new QGroupBox(tr("Pages"));
    auto *pl = new QVBoxLayout(pages);
    m_title = new QCheckBox(tr("Title page (project data, summary, rooms)"));
    m_overview = new QCheckBox(tr("Plan with all elements"));
    m_chapters = new QCheckBox(tr("One page per chapter:"));
    for (QCheckBox *c : {m_title, m_overview, m_chapters}) {
        c->setChecked(true);
        pl->addWidget(c);
    }
    m_list = new QListWidget;
    m_list->setItemDelegate(new CheckDelegate(m_list));
    QSet<QString> used;
    for (PlanScene *s : project->floors())
        used |= s->usedSections();
    for (const SectionDef &s : Library::instance().sections()) {
        if (!used.contains(s.id))
            continue;
        auto *it = new QListWidgetItem(s.name);
        it->setData(Qt::UserRole, s.id);
        it->setData(CheckDelegate::ColorRole, s.color);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
        m_list->addItem(it);
    }
    if (m_list->count() == 0)
        m_list->addItem(new QListWidgetItem(tr("(no elements on the plan yet)")));
    m_list->setMaximumHeight(170);
    pl->addWidget(m_list);
    connect(m_chapters, &QCheckBox::toggled, m_list, &QWidget::setEnabled);
    lay->addWidget(pages);

    auto *opts = new QGroupBox(tr("Options"));
    auto *ol = new QVBoxLayout(opts);
    m_effects = new QCheckBox(tr("Show light / sound simulation (elements switched on)"));
    m_effects->setChecked(true);
    m_notes = new QCheckBox(tr("Show notes next to the elements"));
    m_notes->setChecked(true);
    m_rooms = new QCheckBox(tr("Rooms table with areas"));
    m_rooms->setChecked(true);
    m_bom = new QCheckBox(tr("Bill of materials (quantity of every element, shop links)"));
    m_bom->setChecked(true);
    ol->addWidget(m_bom);
    ol->addWidget(m_effects);
    ol->addWidget(m_notes);
    ol->addWidget(m_rooms);
    lay->addWidget(opts);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Export..."));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

PdfExporter::Options ExportDialog::options() const
{
    PdfExporter::Options o;
    o.pageSize = QPageSize::PageSizeId(m_page->currentData().toInt());
    o.orientation = QPageLayout::Orientation(m_orientation->currentData().toInt());
    o.titlePage = m_title->isChecked();
    o.overviewPage = m_overview->isChecked();
    o.chapterPages = m_chapters->isChecked();
    o.effects = m_effects->isChecked();
    o.notes = m_notes->isChecked();
    o.roomsTable = m_rooms->isChecked();
    o.bom = m_bom->isChecked();
    for (int i = 0; i < m_floors->count(); ++i)
        if (m_floors->item(i)->checkState() == Qt::Checked)
            o.floors << m_floors->item(i)->data(Qt::UserRole).toInt();
    if (o.floors.isEmpty())
        o.floors << 0;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *it = m_list->item(i);
        const QString id = it->data(Qt::UserRole).toString();
        if (!id.isEmpty() && it->checkState() == Qt::Checked)
            o.chapters << id;
    }
    return o;
}
