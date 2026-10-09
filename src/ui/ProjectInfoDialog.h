#ifndef PROJECTINFODIALOG_H
#define PROJECTINFODIALOG_H

#include "core/ProjectInfo.h"

#include <QDialog>

class QLineEdit;
class QDateEdit;
class QPlainTextEdit;

// Data printed on the PDF title page
class ProjectInfoDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProjectInfoDialog(const ProjectInfo &info, QWidget *parent = nullptr);
    ProjectInfo info() const;

private:
    QLineEdit *m_name, *m_client, *m_address, *m_phone, *m_author, *m_company;
    QDateEdit *m_date;
    QPlainTextEdit *m_notes;
};

#endif // PROJECTINFODIALOG_H
