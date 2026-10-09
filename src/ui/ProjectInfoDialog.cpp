#include "ProjectInfoDialog.h"

#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

ProjectInfoDialog::ProjectInfoDialog(const ProjectInfo &info, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Project information"));
    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(info.name);
    m_client = new QLineEdit(info.client);
    m_address = new QLineEdit(info.address);
    m_phone = new QLineEdit(info.phone);
    m_author = new QLineEdit(info.author);
    m_company = new QLineEdit(info.company);
    m_date = new QDateEdit(info.date.isValid() ? info.date : QDate::currentDate());
    m_date->setCalendarPopup(true);
    m_notes = new QPlainTextEdit(info.notes);
    m_notes->setPlaceholderText(tr("General notes for the electricians (printed on the title page)"));
    form->addRow(tr("Project name"), m_name);
    form->addRow(tr("Client"), m_client);
    form->addRow(tr("Address"), m_address);
    form->addRow(tr("Phone"), m_phone);
    form->addRow(tr("Designer"), m_author);
    form->addRow(tr("Company"), m_company);
    form->addRow(tr("Date"), m_date);
    form->addRow(tr("Notes"), m_notes);
    lay->addLayout(form);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    resize(480, 420);
}

ProjectInfo ProjectInfoDialog::info() const
{
    ProjectInfo i;
    i.name = m_name->text().trimmed();
    i.client = m_client->text().trimmed();
    i.address = m_address->text().trimmed();
    i.phone = m_phone->text().trimmed();
    i.author = m_author->text().trimmed();
    i.company = m_company->text().trimmed();
    i.date = m_date->date();
    i.notes = m_notes->toPlainText();
    return i;
}
