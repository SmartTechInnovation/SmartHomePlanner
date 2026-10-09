#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include "export/PdfExporter.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QListWidget;
class Project;

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    ExportDialog(Project *project, QWidget *parent = nullptr);
    PdfExporter::Options options() const;

private:
    QComboBox *m_page;
    QComboBox *m_orientation;
    QCheckBox *m_title, *m_overview, *m_chapters, *m_effects, *m_notes, *m_rooms, *m_bom;
    QListWidget *m_list;
    QListWidget *m_floors;
};

#endif // EXPORTDIALOG_H
