#ifndef PDFEXPORTER_H
#define PDFEXPORTER_H

#include <QPageLayout>
#include <QPageSize>
#include <QString>
#include <QList>
#include <QStringList>

class Project;

// Exports the project to PDF:
//   1. title page (project data, summary per chapter, rooms with areas, plan preview)
//      + bill of materials (every element type per chapter, quantity, shop link)
//   2. for every floor: plan with all elements, then one page per chapter (only that chapter's
//      elements) with a legend: symbol, IDs, element, note, quantity
class PdfExporter
{
public:
    struct Options {
        QPageSize::PageSizeId pageSize = QPageSize::A3;
        QPageLayout::Orientation orientation = QPageLayout::Landscape;
        bool titlePage = true;
        bool overviewPage = true;
        bool chapterPages = true;
        bool effects = true;
        bool notes = true;
        bool roomsTable = true;
        bool bom = true;                // bill of materials: quantity of every element + shop links
        QStringList chapters;           // section ids; empty = all used chapters
        QList<int> floors;              // floor indexes; empty = all floors
        int resolution = 300;
    };

    static bool exportPdf(Project *project, const QString &path, const Options &opt, QString *error = nullptr);
};

#endif // PDFEXPORTER_H
