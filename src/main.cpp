#include "core/Library.h"
#include "core/Project.h"
#include "export/PdfExporter.h"
#include "scene/DeviceItem.h"
#include "scene/GroupItem.h"
#include "scene/PanelItems.h"
#include "scene/PlanScene.h"
#include "scene/RoomItem.h"
#include "scene/SceneSerializer.h"
#include "scene/StripItem.h"
#include "scene/WallItem.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QImage>
#include <QLibraryInfo>
#include <QLoggingCategory>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QTranslator>
#include <QEventLoop>

namespace {

// Small demo apartment, used by --make-demo (handy to try the features and for tests)
void buildDemoFloor(PlanScene *scene);
void buildDemoUpstairs(PlanScene *scene);
void buildDemoPanel(PlanScene *scene);

void buildDemo(Project *project)
{
    PlanScene *ground = project->current();
    project->renameFloor(0, QStringLiteral("Parter"));
    buildDemoFloor(ground);
    PlanScene *up = project->addFloor(QStringLiteral("Etaj 1"));
    buildDemoUpstairs(up);
    PlanScene *panel = project->addFloor(QStringLiteral("Tablou principal"), PlanScene::PanelPage, ground);
    buildDemoPanel(panel);
}

const LibraryEntry *findEntry(const QString &name)
{
    for (const LibraryEntry *e : Library::instance().entries())
        if (e->name == name && e->variant.isEmpty())
            return e;
    return nullptr;
}

void buildDemoPanel(PlanScene *scene)
{
    auto rail = [&](double y, double len) {
        auto *r = new RailItem(len);
        r->setPos(0, y);
        scene->addItem(r);
        return r;
    };
    rail(0, 450);
    rail(160, 450);
    auto put = [&](const QString &name, double x, double railY, const QString &note = QString()) -> EquipmentItem * {
        const LibraryEntry *e = findEntry(name);
        if (!e)
            return nullptr;
        DeviceData d = DeviceData::fromEntry(*e);
        d.id = scene->nextId(QStringLiteral("@") + (e->idPrefix.isEmpty() ? QStringLiteral("Q") : e->idPrefix));
        d.note = note;
        auto *q = new EquipmentItem(d, QSizeF(e->width, e->height), e->ports, e->portNames);
        scene->addItem(q);
        q->setPos(x, railY + RailItem::kHeight / 2 - e->height / 2);
        return q;
    };
    EquipmentItem *rcd = put("Dif 2p", 9, 0, "Intrare 40A");
    EquipmentItem *spd = put("Surge protector 2P", 54, 0);
    EquipmentItem *q1 = put("MCB 1p", 108, 0, "Lumini living");
    EquipmentItem *q2 = put("MCB 1p", 126, 0, "Lumini dormitor");
    EquipmentItem *q3 = put("MCB 1p", 144, 0, "Prize bucătărie");
    EquipmentItem *q4 = put("MCB 1p", 162, 0, "Sursa 24V");
    put("MCB 2p", 189, 0, "Rezervă");
    EquipmentItem *psu = put("Power supply 24V DC", 9, 160);
    EquipmentItem *ctl = put("Smart controller 24V", 90, 160, "Controler principal");
    EquipmentItem *ext = put("Relay extension 14 out", 261, 160);
    auto wire = [&](EquipmentItem *a, int pa, EquipmentItem *b, int pb, QColor c, const QString &sec, const QString &label = QString()) {
        if (!a || !b)
            return;
        auto *w = new WireItem;
        w->setColor(c);
        w->setCrossSection(sec);
        w->setLabel(label);
        scene->addItem(w);
        w->setEnds(a->data().uid, pa, b->data().uid, pb);
    };
    const QColor brown(139, 69, 19), blue(30, 100, 220), red(220, 30, 30), dblue(20, 40, 140), green(0, 150, 70);
    wire(rcd, 2, q1, 0, brown, "2.5");
    wire(q1, 0, q2, 0, brown, "2.5");
    wire(q2, 0, q3, 0, brown, "2.5");
    wire(q3, 0, q4, 0, brown, "2.5");
    wire(rcd, 2, spd, 0, brown, "2.5");
    wire(rcd, 3, spd, 1, blue, "2.5");
    wire(q4, 1, psu, 0, brown, "1.5");
    wire(rcd, 3, psu, 1, blue, "1.5");
    wire(psu, 3, ctl, 0, red, "1.5", "+24V");
    wire(psu, 5, ctl, 1, dblue, "1.5", "0V");
    wire(ctl, 0, ext, 0, red, "0.75");
    wire(ctl, 1, ext, 1, dblue, "0.75");
    wire(ctl, 2, ext, 2, green, "0.5", "Link");
    wire(ctl, 3, ext, 3, green, "0.5", "Link");
    scene->updateLinks();
}

void buildDemoFloor(PlanScene *scene)
{
    scene->info().name = QStringLiteral("Apartament demo");
    scene->info().client = QStringLiteral("Client demo");
    scene->info().address = QStringLiteral("Str. Exemplu 1, Chisinau");
    scene->info().author = QStringLiteral("Smart Home Planner");
    scene->info().notes = QStringLiteral("Toate circuitele de iluminat merg în tabloul principal. Spoturile 24V PWM se cablează 2x0.75 din driverul LED.");

    auto wall = [&](QPolygonF pts, bool closed, double t = 20) {
        auto *w = new WallItem;
        w->setThickness(t);
        w->setPos(pts.first());
        w->setPoints(pts.translated(-pts.first()));
        w->setClosed(closed);
        scene->addItem(w);
    };
    // outer walls 10 x 7 m
    wall(QPolygonF{{0, 0}, {1000, 0}, {1000, 700}, {0, 700}}, true, 30);
    // inner walls with door openings
    wall(QPolygonF{{550, 0}, {550, 300}}, false, 12);
    wall(QPolygonF{{550, 390}, {550, 700}}, false, 12);
    wall(QPolygonF{{0, 400}, {250, 400}}, false, 12);
    wall(QPolygonF{{340, 400}, {550, 400}}, false, 12);

    auto room = [&](const QString &name, QPolygonF pts, QColor c, QPointF labelOffset) {
        auto *r = new RoomItem;
        r->setName(name);
        r->setColor(c);
        r->setLabelOffset(labelOffset);
        r->setPos(pts.first());
        r->setPoints(pts.translated(-pts.first()));
        scene->addItem(r);
    };
    room("Living", QPolygonF{{556, 15}, {985, 15}, {985, 685}, {556, 685}}, QColor(120, 170, 220), QPointF(0, 100));
    room("Dormitor", QPolygonF{{15, 15}, {544, 15}, {544, 394}, {15, 394}}, QColor(160, 200, 140), QPointF(-120, 0));
    room("Bucătărie", QPolygonF{{15, 406}, {544, 406}, {544, 685}, {15, 685}}, QColor(230, 180, 120), QPointF(0, -60));

    const Library &lib = Library::instance();
    auto find = [&](const QString &name) -> const LibraryEntry * {
        for (const LibraryEntry *e : lib.entries())
            if (e->name == name && e->variant.isEmpty())
                return e;
        return nullptr;
    };
    auto place = [&](const QString &name, QPointF p, bool on = false) -> DeviceItem * {
        const LibraryEntry *e = find(name);
        if (!e)
            return nullptr;
        DeviceData d = DeviceData::fromEntry(*e);
        d.id = scene->nextId(d.section);
        d.on = on;
        auto *it = new DeviceItem(d);
        it->setPos(p);
        scene->addItem(it);
        return it;
    };
    for (int x : {680, 860})
        for (int y : {150, 350, 550})
            place("LED Spot WW PWM", QPointF(x, y), true);
    place("Plafoniera LOXONE RGBW Tree", QPointF(280, 200), true);
    place("LED Spot Dual Tone PWM", QPointF(150, 540), true);
    place("LED Spot Dual Tone PWM", QPointF(400, 540), false);
    place("Touch Tree", QPointF(575, 360));
    place("Touch Tree", QPointF(520, 60));
    place("Touch Tree", QPointF(300, 420));
    place("Senzor de prezenta Loxone Tree", QPointF(770, 250));
    place("Senzor de prezenta Loxone Tree", QPointF(280, 300));
    place("Detector de fum Air", QPointF(420, 120));
    place("Difuzor Speaker 7", QPointF(700, 600), true);
    place("Difuzor Speaker 7", QPointF(880, 600), true);
    place("De baza", QPointF(30, 250));
    place("De baza", QPointF(970, 400));
    place("Retea RJ45", QPointF(970, 300));
    place("Acces Point", QPointF(780, 60));       // Wi-Fi coverage: shown while selected
    place("Jaluzele / rolete", QPointF(1000, 120));

    if (const LibraryEntry *e = find("Banda Led WW")) {
        scene->addStripLine(*e, QPolygonF{{40, 670}, {520, 670}});
        scene->addStripCircle(*e, QPointF(770, 350), 90);
    }

    // circuits: switch + the lights it controls
    auto group = [&](const QString &name, int color, const QList<QPointF> &at, const QString &note, bool star = false) {
        QStringList uids;
        for (const QPointF &p : at) {
            for (QGraphicsItem *it : scene->items(p))
                if (!PlanScene::uidOf(it).isEmpty() && it->type() != ItemType::Equipment) {
                    uids << PlanScene::uidOf(it);
                    break;
                }
        }
        auto *g = new GroupItem;
        g->setName(name);
        g->setColor(GroupItem::paletteColor(color));
        g->setNote(note);
        scene->addItem(g);
        if (star) {
            // the first element (switch) linked to each of the others
            QVector<GroupItem::Link> links;
            for (int i = 1; i < uids.size(); ++i)
                links.push_back({uids.first(), uids[i]});
            g->setLinks(links);
        } else {
            g->setMembers(uids);
        }
    };
    group("G1", 0, {{575, 360}, {680, 350}, {680, 150}, {860, 150}, {860, 350}, {860, 550}, {680, 550}}, "Q1 – Lumini living");
    group("G2", 1, {{300, 420}, {150, 540}, {400, 540}, {280, 670}}, "Q3 – Bucătărie", true);
    group("G3", 2, {{520, 60}, {280, 200}}, "Q2 – Dormitor");
}

void buildDemoUpstairs(PlanScene *scene)
{
    auto wall = [&](QPolygonF pts, bool closed, double t) {
        auto *w = new WallItem;
        w->setThickness(t);
        w->setPos(pts.first());
        w->setPoints(pts.translated(-pts.first()));
        w->setClosed(closed);
        scene->addItem(w);
    };
    wall(QPolygonF{{0, 0}, {1000, 0}, {1000, 700}, {0, 700}}, true, 30);
    wall(QPolygonF{{500, 0}, {500, 280}}, false, 12);
    wall(QPolygonF{{500, 370}, {500, 700}}, false, 12);
    auto room = [&](const QString &name, QPolygonF pts, QColor c) {
        auto *r = new RoomItem;
        r->setName(name);
        r->setColor(c);
        r->setLabelOffset(QPointF(0, -120));
        r->setPos(pts.first());
        r->setPoints(pts.translated(-pts.first()));
        scene->addItem(r);
    };
    room("Dormitor 2", QPolygonF{{15, 15}, {494, 15}, {494, 685}, {15, 685}}, QColor(200, 160, 220));
    room("Birou", QPolygonF{{506, 15}, {985, 15}, {985, 685}, {506, 685}}, QColor(140, 200, 190));
    const Library &lib = Library::instance();
    auto place = [&](const QString &name, QPointF p, bool on = false) {
        for (const LibraryEntry *e : lib.entries()) {
            if (e->name == name && e->variant.isEmpty()) {
                DeviceData d = DeviceData::fromEntry(*e);
                d.id = scene->nextId(d.section);
                d.on = on;
                auto *it = new DeviceItem(d);
                it->setPos(p);
                scene->addItem(it);
                return;
            }
        }
    };
    place("Pendulum Slim Tree", QPointF(250, 350), true);
    place("LED Spot WW PWM", QPointF(680, 250), true);
    place("LED Spot WW PWM", QPointF(820, 250), true);
    place("LED Spot WW PWM", QPointF(680, 480), false);
    place("LED Spot WW PWM", QPointF(820, 480), false);
    place("Touch Tree", QPointF(470, 330));
    place("Touch Tree", QPointF(530, 330));
    place("Senzor de confort Tree", QPointF(250, 600));
    place("Retea RJ45", QPointF(970, 600));
    place("De baza", QPointF(970, 520));
    place("Difuzor de perete", QPointF(30, 350), true);
}

} // namespace

int main(int argc, char *argv[])
{
    // macOS accessibility asks tree views for rows while they are being rebuilt: harmless Qt noise
    QLoggingCategory::setFilterRules(QStringLiteral("qt.accessibility.table.warning=false"));
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Smart Tech Innovation"));
    QApplication::setOrganizationDomain(QStringLiteral("smartinnovation.md"));
    QApplication::setApplicationName(QStringLiteral("SmartHomePlanner"));
    QApplication::setApplicationDisplayName(QStringLiteral("Smart Home Planner"));
    QApplication::setApplicationVersion(QStringLiteral(SHP_VERSION));

    // language: saved choice, otherwise the system language
    QString lang = QSettings().value("language", QLocale::system().name().left(2)).toString();
    if (qEnvironmentVariableIsSet("SHP_LANG"))
        lang = qEnvironmentVariable("SHP_LANG");
    Library::instance().setLanguage(lang);
    QTranslator appTr, qtTr;
    if (lang == "ro") {
        if (appTr.load(QStringLiteral(":/i18n/SmartHomePlanner_ro.qm")))
            app.installTranslator(&appTr);
        if (qtTr.load(QLocale(QLocale::Romanian), "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            app.installTranslator(&qtTr);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Smart Home Planner"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption exportOpt("export-pdf", "Export <project> to PDF and exit.", "pdf");
    QCommandLineOption renderOpt("render-png", "Render <project> to PNG and exit.", "png");
    QCommandLineOption demoOpt("make-demo", "Create a demo project file and exit.", "shp");
    QCommandLineOption nightOpt("night", "With --render-png: night simulation.");
    QCommandLineOption shotOpt("screenshot", "Open <project>, select the first light and save a window screenshot.", "png");
    QCommandLineOption pageOpt("page", "With --screenshot / --render-png: index of the floor / panel page.", "index");
    parser.addOption(exportOpt);
    parser.addOption(renderOpt);
    parser.addOption(demoOpt);
    parser.addOption(nightOpt);
    parser.addOption(shotOpt);
    parser.addOption(pageOpt);
    parser.addPositionalArgument("project", "Project file (.shp) to open.");
    parser.process(app);

    const int n = Library::instance().loadAll();
    const QStringList args = parser.positionalArguments();

    // ---- batch modes
    if (parser.isSet(demoOpt) || parser.isSet(exportOpt) || parser.isSet(renderOpt)) {
        Project project;
        if (parser.isSet(demoOpt)) {
            buildDemo(&project);
            QString err;
            if (!SceneSerializer::saveProject(&project, parser.value(demoOpt), &err)) {
                qCritical("%s", qPrintable(err));
                return 1;
            }
            return 0;
        }
        if (args.isEmpty()) {
            qCritical("missing project file");
            return 1;
        }
        QString err;
        if (!SceneSerializer::loadProject(&project, args.first(), &err)) {
            qCritical("%s", qPrintable(err));
            return 1;
        }
        QCoreApplication::processEvents();
        if (parser.isSet(exportOpt)) {
            PdfExporter::Options o;
            if (!PdfExporter::exportPdf(&project, parser.value(exportOpt), o, &err)) {
                qCritical("%s", qPrintable(err));
                return 1;
            }
        }
        if (parser.isSet(renderOpt)) {
            if (parser.isSet(pageOpt))
                project.setCurrent(parser.value(pageOpt).toInt());
            PlanScene &scene = *project.current();
            scene.setNightMode(parser.isSet(nightOpt));
            scene.setShowBeams(!parser.isSet(nightOpt));
            const QRectF src = scene.contentRect().adjusted(-60, -60, 60, 60);
            QImage img((src.size() * 1.2).toSize(), QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            scene.render(&p, QRectF(QPointF(0, 0), img.size()), src);
            p.end();
            img.save(parser.value(renderOpt));
        }
        return 0;
    }

    MainWindow w;
    w.show();
    if (n == 0) {
        QMessageBox::warning(&w, QObject::tr("Library"),
                             QObject::tr("No element library was found. Searched in:\n%1")
                                 .arg(Library::instance().searchPaths().join("\n")));
    }
    if (!args.isEmpty())
        w.openProject(args.first());
    else if (!parser.isSet(shotOpt))
        w.openLastProject();        // last project worked on; a new one if it is gone
    if (parser.isSet(shotOpt)) {
        w.resize(1600, 1000);
        QEventLoop loop;
        QTimer::singleShot(400, &loop, &QEventLoop::quit);
        loop.exec();
        if (parser.isSet(pageOpt)) {
            w.project()->setCurrent(parser.value(pageOpt).toInt());
            QTimer::singleShot(300, &loop, &QEventLoop::quit);
            loop.exec();
        }
        if (auto *scene = w.project()->current()) {
            if (scene->isPanel()) {
                // the controller: shows where its wires come from and go to
                EquipmentItem *best = nullptr;
                for (EquipmentItem *e : scene->equipment())
                    if (!best || scene->wiresOf(e->data().uid).size() > scene->wiresOf(best->data().uid).size())
                        best = e;
                if (qEnvironmentVariableIsSet("SHP_SELECT_WIRE") && !scene->wires().isEmpty())
                    scene->wires().at(qEnvironmentVariableIntValue("SHP_SELECT_WIRE"))->setSelected(true);
                else if (best)
                    best->setSelected(true);
            } else {
                for (DeviceItem *d : scene->devices())
                    if (d->isLight()) { d->setSelected(true); break; }
            }
        }
        QTimer::singleShot(400, &loop, &QEventLoop::quit);
        loop.exec();
        w.grab().save(parser.value(shotOpt));
        return 0;
    }
    return app.exec();
}
