#ifndef PROJECTINFO_H
#define PROJECTINFO_H

#include <QDate>
#include <QString>

struct ProjectInfo {
    QString name = QStringLiteral("New project");
    QString client;
    QString address;
    QString author;
    QString company;
    QString phone;
    QDate   date = QDate::currentDate();
    QString notes;
};

struct ProjectSettings {
    double ceilingHeight = 270;   // default light mounting height [cm]
    double workPlane     = 0;     // light calculation plane [cm] (0 = floor)
    double symbolScale   = 1.0;   // multiplier for all symbol sizes
    double wallThickness = 20;    // default for new walls [cm]
    double gridSize      = 10;    // snap grid [cm]
};

#endif // PROJECTINFO_H
