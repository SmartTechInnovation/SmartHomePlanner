#include "PdfExporter.h"

#include "core/Geometry.h"
#include "core/IconCache.h"
#include "core/Library.h"
#include "core/Project.h"
#include "scene/DeviceItem.h"
#include "scene/GroupItem.h"
#include "scene/PanelItems.h"
#include "scene/PlanScene.h"
#include "scene/RoomItem.h"
#include "scene/StripItem.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>
#include <QPainter>
#include <QPdfWriter>
#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextDocument>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

QString tr(const char *s) { return QCoreApplication::translate("PdfExporter", s); }

struct LegendRow {
    QString icon;
    QString name;
    QString note;
    QStringList ids;
    int qty = 0;
    double lengthCm = 0;
    bool strip = false;
};

struct Page {
    enum Type { Title, Rooms, Bom, Overview, Chapter, LegendCont, Panel, Connections };
    Page(Type t, const QString &s = QString(), int f = 0) : type(t), section(s), floor(f) {}
    Type type;
    QString section;            // chapter id
    int floor = 0;              // floor index
    int legendStart = 0;        // first legend row on this page
    int legendCount = 0;
    int roomStart = 0;
    int roomCount = 0;
};

// "L1, L2, L3, L5" -> "L1–L3, L5"
QString compressIds(QStringList ids)
{
    static const QRegularExpression re(QStringLiteral("^(.*?)(\\d+)$"));
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) {
        const auto ma = re.match(a), mb = re.match(b);
        if (ma.hasMatch() && mb.hasMatch() && ma.captured(1) == mb.captured(1))
            return ma.captured(2).toInt() < mb.captured(2).toInt();
        return a < b;
    });
    QStringList out;
    int i = 0;
    while (i < ids.size()) {
        const auto m = re.match(ids[i]);
        if (!m.hasMatch()) {
            if (!ids[i].isEmpty())
                out << ids[i];
            ++i;
            continue;
        }
        const QString prefix = m.captured(1);
        const int start = m.captured(2).toInt();
        int end = start;
        int j = i + 1;
        while (j < ids.size()) {
            const auto mj = re.match(ids[j]);
            if (!mj.hasMatch() || mj.captured(1) != prefix || mj.captured(2).toInt() != end + 1)
                break;
            end = mj.captured(2).toInt();
            ++j;
        }
        if (end - start >= 2)
            out << QStringLiteral("%1%2–%1%3").arg(prefix).arg(start).arg(end);
        else
            for (int k = i; k < j; ++k)
                out << ids[k];
        i = j;
    }
    return out.join(", ");
}

QVector<LegendRow> legendFor(PlanScene *scene, const QString &section)
{
    QMap<QString, LegendRow> map;
    for (DeviceItem *d : scene->devices()) {
        const DeviceData &dd = d->data();
        if (!section.isEmpty() && dd.section != section)
            continue;
        const QString key = dd.displayName() + "|" + dd.note + "|" + dd.iconPath;
        LegendRow &r = map[key];
        r.icon = dd.iconPath;
        r.name = dd.displayName();
        r.note = dd.note;
        r.ids << dd.id;
        r.qty++;
    }
    for (StripItem *s : scene->strips()) {
        const DeviceData &dd = s->data();
        if (!section.isEmpty() && dd.section != section)
            continue;
        const QString key = "strip|" + dd.displayName() + "|" + dd.note;
        LegendRow &r = map[key];
        r.icon = dd.iconPath;
        r.name = dd.displayName();
        r.note = dd.note;
        r.ids << dd.id;
        r.qty++;
        r.strip = true;
        r.lengthCm += s->length();
    }
    QVector<LegendRow> rows = map.values().toVector();
    std::sort(rows.begin(), rows.end(), [](const LegendRow &a, const LegendRow &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return rows;
}

// one line of the bill of materials
struct BomRow {
    bool chapter = false;       // chapter heading line
    QString name;
    QColor color;               // chapter color
    QString icon;
    QString floors;             // "Parter 4, Etaj 1 2"
    QString qty;
    QString shop;
};

QVector<BomRow> billOfMaterials(const QList<PlanScene *> &floors, int *totalOut)
{
    struct Acc {
        QString icon, name, shop;
        int qty = 0;
        double lengthCm = 0;
        bool strip = false;
        QMap<int, int> perFloor;
    };
    QMap<QString, QMap<QString, Acc>> bySection;      // section -> element -> totals
    auto add = [&](int fi, const DeviceData &d, bool strip, double len) {
        Acc &a = bySection[d.section][(strip ? "s|" : "d|") + d.key + "|" + d.displayName() + "|" + d.iconPath];
        a.icon = d.iconPath;
        a.name = d.displayName();
        if (a.shop.isEmpty())
            a.shop = d.shop.trimmed();
        a.qty++;
        a.strip = strip;
        a.lengthCm += len;
        a.perFloor[fi]++;
    };
    for (int fi = 0; fi < floors.size(); ++fi) {
        for (DeviceItem *d : floors[fi]->devices())
            add(fi, d->data(), false, 0);
        for (StripItem *st : floors[fi]->strips())
            add(fi, st->data(), true, st->length());
        for (EquipmentItem *e : floors[fi]->equipment())
            add(fi, e->data(), false, 0);
        for (RailItem *r : floors[fi]->rails()) {
            DeviceData d;
            d.key = QStringLiteral("rail");
            d.name = tr("DIN rail 35 mm");
            d.section = QStringLiteral("electrical");
            for (const LibraryEntry *le : Library::instance().entries())
                if (le->rail) {
                    d.iconPath = le->iconPath;
                    d.shop = le->shop;
                    break;
                }
            add(fi, d, true, r->length() / 10.0);     // mm -> cm
        }
    }
    QVector<BomRow> rows;
    int total = 0;
    QStringList order;
    for (const SectionDef &sec : Library::instance().sections())
        order << sec.id;
    for (const QString &id : bySection.keys())
        if (!order.contains(id))
            order << id;
    for (const QString &id : order) {
        if (!bySection.contains(id))
            continue;
        const SectionDef *sec = Library::instance().section(id);
        BomRow head;
        head.chapter = true;
        head.name = sec ? sec->name : id;
        head.color = sec ? sec->color : QColor(120, 120, 120);
        QVector<Acc> items = bySection[id].values().toVector();
        std::sort(items.begin(), items.end(), [](const Acc &a, const Acc &b) { return a.name.localeAwareCompare(b.name) < 0; });
        int n = 0;
        for (const Acc &a : items)
            n += a.qty;
        head.qty = QString::number(n);
        total += n;
        rows << head;
        for (const Acc &a : items) {
            BomRow r;
            r.icon = a.icon;
            r.name = a.name;
            r.color = head.color;
            r.qty = QString::number(a.qty);
            if (a.strip)
                r.qty += QStringLiteral(" (") + Geo::formatMeters(a.lengthCm, 1) + ")";
            QStringList pf;
            for (auto it = a.perFloor.cbegin(); it != a.perFloor.cend(); ++it)
                pf << floors[it.key()]->floorName() + QStringLiteral(": ") + QString::number(it.value());
            r.floors = pf.join(QStringLiteral(", "));
            r.shop = a.shop;
            rows << r;
        }
    }
    if (totalOut)
        *totalOut = total;
    return rows;
}

class Renderer
{
public:
    // lupdate files the strings of this class under the "Renderer" context
    static QString tr(const char *s) { return QCoreApplication::translate("Renderer", s); }

    Renderer(PlanScene *scene, QPdfWriter &w, const PdfExporter::Options &o)
        : m_scene(scene), m_writer(w), m_opt(o)
    {
        m_res = o.resolution;
        const QRect pr = w.pageLayout().paintRectPixels(m_res);
        m_page = QRectF(0, 0, pr.width(), pr.height());
    }

    double mm(double v) const { return v * m_res / 25.4; }

    QFont font(double pt, bool bold = false) const
    {
        QFont f(QStringLiteral("Helvetica"));
        f.setPointSizeF(pt);
        f.setBold(bold);
        return f;
    }

    QRectF contentArea() const
    {
        return m_page.adjusted(0, mm(16), 0, -mm(9));
    }

    void header(QPainter &p, const QString &title, const QColor &accent, int pageNo, int pageCount)
    {
        const ProjectInfo &info = m_scene->info();
        const QRectF top(0, 0, m_page.width(), mm(12));
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(accent.isValid() ? accent : QColor(40, 60, 90));
        p.drawRect(QRectF(0, 0, mm(4), top.height()));
        p.setPen(QColor(20, 20, 20));
        p.setFont(font(15, true));
        p.drawText(top.adjusted(mm(7), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, title);
        p.setFont(font(10));
        p.setPen(QColor(90, 90, 90));
        p.drawText(top, Qt::AlignRight | Qt::AlignVCenter,
                   info.name + (info.client.isEmpty() ? QString() : QStringLiteral("  •  ") + info.client));
        p.setPen(QPen(QColor(180, 180, 180), mm(0.3)));
        p.drawLine(QPointF(0, top.bottom() + mm(1.5)), QPointF(m_page.width(), top.bottom() + mm(1.5)));

        const QRectF foot(0, m_page.height() - mm(6), m_page.width(), mm(6));
        p.drawLine(QPointF(0, foot.top() - mm(1)), QPointF(m_page.width(), foot.top() - mm(1)));
        p.setFont(font(8));
        p.setPen(QColor(110, 110, 110));
        QStringList left;
        if (!info.company.isEmpty()) left << info.company;
        if (!info.author.isEmpty()) left << info.author;
        if (!info.address.isEmpty()) left << info.address;
        left << QLocale().toString(info.date.isValid() ? info.date : QDate::currentDate(), QLocale::ShortFormat);
        p.drawText(foot, Qt::AlignLeft | Qt::AlignVCenter, left.join(QStringLiteral("  •  ")));
        p.drawText(foot, Qt::AlignRight | Qt::AlignVCenter,
                   tr("Page %1 / %2").arg(pageNo).arg(pageCount));
        p.restore();
    }

    // Renders the plan into 'target' (with the current section filter). Returns the scale 1:N.
    double drawPlan(QPainter &p, const QRectF &target, const QRectF &source, bool drawScale = true)
    {
        if (source.isEmpty()) {
            p.setPen(QColor(150, 150, 150));
            p.setFont(font(12));
            p.drawText(target, Qt::AlignCenter, tr("The plan is empty"));
            return 0;
        }
        const double s = std::min(target.width() / source.width(), target.height() / source.height());
        QRectF t(0, 0, source.width() * s, source.height() * s);
        t.moveCenter(target.center());
        p.save();
        m_scene->render(&p, t, source, Qt::KeepAspectRatio);
        p.restore();

        // device pixels per cm of the real building
        const double pxPerCm = s;
        const double mmPerCm = pxPerCm / (m_res / 25.4);
        const double ratio = 10.0 / mmPerCm;     // 1 cm of paper = ratio cm in reality
        if (drawScale) {
            // scale bar: choose a nice length
            const double nice[] = {50, 100, 200, 500, 1000, 2000, 5000};
            double lenCm = 100;
            for (double n : nice) {
                if (n * pxPerCm >= mm(25)) { lenCm = n; break; }
                lenCm = n;
            }
            const double barPx = lenCm * pxPerCm;
            const QPointF o(target.left() + mm(2), target.bottom() - mm(3));
            p.save();
            p.setPen(QPen(Qt::black, mm(0.3)));
            p.setBrush(Qt::black);
            p.drawRect(QRectF(o.x(), o.y() - mm(1.5), barPx / 2, mm(1.5)));
            p.setBrush(Qt::white);
            p.drawRect(QRectF(o.x() + barPx / 2, o.y() - mm(1.5), barPx / 2, mm(1.5)));
            p.setFont(font(8));
            p.drawText(QRectF(o.x(), o.y() - mm(6), barPx + mm(30), mm(4)), Qt::AlignLeft | Qt::AlignBottom,
                       QStringLiteral("0      %1      ≈ 1:%2").arg(Geo::formatMeters(lenCm, lenCm < 100 ? 1 : 0))
                           .arg(int(std::round(ratio))));
            p.restore();
        }
        return ratio;
    }

    // Legend in 'area', rows [start, start+count). Returns the number of rows that fit.
    int legendCapacity(const QRectF &area) const
    {
        return std::max(1, int((area.height() - mm(9)) / mm(9)));
    }

    void drawLegend(QPainter &p, const QRectF &area, const QVector<LegendRow> &rows, int start, int count,
                    const QColor &accent)
    {
        const double rowH = mm(9);
        const double w = area.width();
        // columns: symbol | IDs | element | note | qty
        const double cSym = mm(11), cIds = w * 0.22, cQty = w * 0.13;
        const double cNote = w * 0.22;
        const double cName = w - cSym - cIds - cQty - cNote;
        double y = area.top();
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(accent.isValid() ? accent.lighter(170) : QColor(230, 230, 230));
        p.drawRect(QRectF(area.left(), y, w, rowH));
        p.setPen(QColor(30, 30, 30));
        p.setFont(font(8.5, true));
        double x = area.left();
        auto cell = [&](double cw, const QString &t, Qt::Alignment a = Qt::AlignLeft | Qt::AlignVCenter) {
            p.drawText(QRectF(x + mm(1), y, cw - mm(2), rowH), a | Qt::TextWordWrap, t);
            x += cw;
        };
        cell(cSym, QString());
        cell(cIds, tr("ID"));
        cell(cName, tr("Element"));
        cell(cNote, tr("Note"));
        cell(cQty, tr("Qty"), Qt::AlignRight | Qt::AlignVCenter);
        y += rowH;
        p.setFont(font(8));
        for (int i = start; i < std::min<int>(rows.size(), start + count); ++i) {
            const LegendRow &r = rows[i];
            if ((i - start) % 2 == 1) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(246, 246, 246));
                p.drawRect(QRectF(area.left(), y, w, rowH));
            }
            p.setPen(QColor(30, 30, 30));
            x = area.left();
            IconCache::instance().draw(&p, r.icon, QRectF(x + mm(1.5), y + mm(1), rowH - mm(2), rowH - mm(2)));
            x += cSym;
            cell(cIds, compressIds(r.ids));
            cell(cName, r.name);
            cell(cNote, r.note);
            QString q = QString::number(r.qty);
            if (r.strip)
                q += QStringLiteral(" (") + Geo::formatMeters(r.lengthCm, 1) + ")";
            cell(cQty, q, Qt::AlignRight | Qt::AlignVCenter);
            y += rowH;
        }
        p.setPen(QPen(QColor(200, 200, 200), mm(0.2)));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(area.left(), area.top(), w, y - area.top()));
        p.restore();
    }

    // generic text table; returns height used
    double drawTable(QPainter &p, const QRectF &area, const QStringList &header, const QVector<double> &fracs,
                     const QVector<QStringList> &rows, const QVector<QColor> &swatches = {}, bool textTable = false)
    {
        const double rowH = mm(7);
        double y = area.top();
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(232, 236, 242));
        p.drawRect(QRectF(area.left(), y, area.width(), rowH));
        auto drawRow = [&](const QStringList &cells, bool bold, const QColor &sw) {
            double x = area.left();
            p.setFont(font(8.5, bold));
            p.setPen(QColor(30, 30, 30));
            for (int c = 0; c < cells.size() && c < fracs.size(); ++c) {
                const double cw = area.width() * fracs[c];
                QRectF cellR(x + mm(1.2), y, cw - mm(2.4), rowH);
                if (c == 0 && sw.isValid()) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(sw);
                    p.drawRoundedRect(QRectF(x + mm(1.2), y + mm(1.8), mm(3.4), mm(3.4)), mm(0.6), mm(0.6));
                    cellR.setLeft(cellR.left() + mm(5));
                    p.setPen(QColor(30, 30, 30));
                }
                const Qt::Alignment al = (c == 0 || textTable) ? Qt::AlignLeft : Qt::AlignRight;
                p.drawText(cellR, al | Qt::AlignVCenter, fm(p).elidedText(cells[c], Qt::ElideRight, cellR.width()));
                x += cw;
            }
            y += rowH;
        };
        drawRow(header, true, QColor());
        for (int i = 0; i < rows.size(); ++i) {
            if (y + rowH > area.bottom())
                break;
            if (i % 2 == 1) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(248, 248, 248));
                p.drawRect(QRectF(area.left(), y, area.width(), rowH));
            }
            const bool total = rows[i].value(0).startsWith(QStringLiteral("Σ"));
            drawRow(rows[i], total, i < swatches.size() ? swatches[i] : QColor());
        }
        p.setPen(QPen(QColor(200, 200, 200), mm(0.2)));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(area.left(), area.top(), area.width(), y - area.top()));
        p.restore();
        return y - area.top();
    }

    // clickable link (the PDF engine turns anchors into link annotations)
    void drawLink(QPainter &p, const QRectF &r, const QString &url)
    {
        if (url.isEmpty())
            return;
        const QFont f = font(8);
        QFontMetricsF fm(f, &m_writer);
        QString shown = url;
        shown.remove(QRegularExpression(QStringLiteral("^https?://(www\\.)?")));
        shown = fm.elidedText(shown, Qt::ElideMiddle, r.width() - mm(1));
        QTextDocument doc;
        doc.documentLayout()->setPaintDevice(&m_writer);
        doc.setDefaultFont(f);
        doc.setDocumentMargin(0);
        doc.setHtml(QStringLiteral("<a href=\"%1\" style=\"color:#1565C0\">%2</a>")
                        .arg(url.toHtmlEscaped(), shown.toHtmlEscaped()));
        p.save();
        p.translate(r.left(), r.center().y() - doc.size().height() / 2);
        doc.drawContents(&p);
        p.restore();
    }

    double bomRowHeight() const { return mm(8); }
    int bomCapacity(const QRectF &area) const { return std::max(1, int(area.height() / bomRowHeight()) - 1); }

    void drawBom(QPainter &p, const QRectF &area, const QVector<BomRow> &rows, int start, int count, bool multi,
                 const QString &totalText)
    {
        const double rowH = bomRowHeight();
        const double w = area.width();
        const double cSym = mm(11);
        const double cQty = w * 0.11;
        const double cFloors = multi ? w * 0.22 : 0;
        const double cShop = w * 0.30;
        const double cName = w - cSym - cQty - cFloors - cShop;
        double y = area.top();
        double x = area.left();
        p.save();
        auto cell = [&](double cw, const QString &t, Qt::Alignment a = Qt::AlignLeft | Qt::AlignVCenter) {
            if (cw > 0)
                p.drawText(QRectF(x + mm(1.2), y, cw - mm(2.4), rowH), a | Qt::TextWordWrap, t);
            x += cw;
        };
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(232, 236, 242));
        p.drawRect(QRectF(area.left(), y, w, rowH));
        p.setPen(QColor(30, 30, 30));
        p.setFont(font(8.5, true));
        cell(cSym, QString());
        cell(cName, tr("Element"));
        cell(cFloors, tr("Floors"));
        cell(cQty, tr("Qty"), Qt::AlignRight | Qt::AlignVCenter);
        cell(cShop, tr("Shop link"));
        y += rowH;
        int stripe = 0;
        for (int i = start; i < std::min<int>(rows.size(), start + count); ++i) {
            const BomRow &r = rows[i];
            x = area.left();
            if (r.chapter) {
                p.setPen(Qt::NoPen);
                QColor bg = r.color;
                bg.setAlpha(55);
                p.setBrush(bg);
                p.drawRect(QRectF(area.left(), y, w, rowH));
                p.setBrush(r.color);
                p.drawRect(QRectF(area.left(), y, mm(1.5), rowH));
                p.setPen(QColor(20, 20, 20));
                p.setFont(font(9.5, true));
                x += mm(2);
                cell(cSym + cName + cFloors - mm(2), r.name);
                cell(cQty, r.qty, Qt::AlignRight | Qt::AlignVCenter);
                stripe = 0;
            } else {
                if (stripe++ % 2 == 1) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(247, 247, 247));
                    p.drawRect(QRectF(area.left(), y, w, rowH));
                }
                p.setPen(QColor(30, 30, 30));
                p.setFont(font(8.5));
                IconCache::instance().draw(&p, r.icon, QRectF(x + mm(1.5), y + mm(1), rowH - mm(2), rowH - mm(2)));
                x += cSym;
                cell(cName, r.name);
                p.setPen(QColor(90, 90, 90));
                p.setFont(font(8));
                cell(cFloors, r.floors);
                p.setPen(QColor(20, 20, 20));
                p.setFont(font(9, true));
                cell(cQty, r.qty, Qt::AlignRight | Qt::AlignVCenter);
                if (r.shop.isEmpty()) {
                    p.setPen(QColor(170, 170, 170));
                    p.setFont(font(8));
                    cell(cShop, QStringLiteral("–"));
                } else {
                    drawLink(p, QRectF(x + mm(1.2), y, cShop - mm(2.4), rowH), r.shop);
                }
            }
            y += rowH;
        }
        if (!totalText.isEmpty() && start + count > rows.size()) {
            p.setPen(QPen(QColor(120, 120, 120), mm(0.3)));
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            x = area.left();
            p.setPen(QColor(20, 20, 20));
            p.setFont(font(9.5, true));
            cell(cSym + cName + cFloors, QStringLiteral("Σ ") + tr("Total elements"));
            cell(cQty, totalText, Qt::AlignRight | Qt::AlignVCenter);
            y += rowH;
        }
        p.setPen(QPen(QColor(200, 200, 200), mm(0.2)));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(area.left(), area.top(), w, y - area.top()));
        p.restore();
    }

    QFontMetricsF fm(QPainter &p) const { return QFontMetricsF(p.font(), &m_writer); }

    PlanScene *m_scene;
    QPdfWriter &m_writer;
    PdfExporter::Options m_opt;
    int m_res;
    QRectF m_page;
};

} // namespace

namespace {

int countIn(PlanScene *scene, const QString &section)
{
    int n = 0;
    for (DeviceItem *d : scene->devices())
        if (d->data().section == section) ++n;
    for (StripItem *st : scene->strips())
        if (st->data().section == section) ++n;
    for (EquipmentItem *e : scene->equipment())
        if (e->data().section == section) ++n;
    return n;
}

struct RoomRow {
    QString floor;
    RoomItem *room;
};

} // namespace

bool PdfExporter::exportPdf(Project *project, const QString &path, const Options &opt, QString *error)
{
    const Library &lib = Library::instance();
    QList<int> floorIdx = opt.floors;
    if (floorIdx.isEmpty())
        for (int i = 0; i < project->floorCount(); ++i)
            floorIdx << i;
    QList<PlanScene *> floors;
    for (int i : floorIdx)
        if (PlanScene *s = project->floor(i))
            floors << s;
    if (floors.isEmpty()) {
        if (error) *error = tr("Nothing to export");
        return false;
    }
    const bool multi = floors.size() > 1;

    QSet<QString> usedAll;
    for (PlanScene *s : floors)
        usedAll |= s->usedSections();

    QPdfWriter writer(path);
    writer.setResolution(opt.resolution);
    writer.setPageLayout(QPageLayout(QPageSize(opt.pageSize), opt.orientation, QMarginsF(10, 10, 10, 10),
                                     QPageLayout::Millimeter));
    writer.setTitle(project->info().name);
    writer.setCreator(QStringLiteral("Smart Home Planner"));

    Renderer R(floors.first(), writer, opt);
    const QRectF content = R.contentArea();
    const double legendW = std::max(R.mm(95), content.width() * 0.30);
    const QRectF planArea(content.left(), content.top(), content.width() - legendW - R.mm(6), content.height());
    const QRectF legendArea(content.right() - legendW, content.top(), legendW, content.height());
    const QRectF fullLegendArea = content;

    // ---- remember the scene state, prepare every floor
    struct Saved { QSet<QString> hidden; bool notes; };
    QHash<PlanScene *, Saved> saved;
    QHash<PlanScene *, QRectF> sources;
    for (PlanScene *s : floors) {
        saved.insert(s, {s->hiddenSections(), s->showNotes()});
        s->clearSelection();
        s->setShowNotes(opt.notes);
        s->setHiddenSections({});
        sources.insert(s, s->contentRect().adjusted(-40, -40, 40, 40));
    }
    auto restore = [&] {
        for (PlanScene *s : floors) {
            s->setExporting(false);
            s->setShowNotes(saved[s].notes);
            s->setHiddenSections(saved[s].hidden);
        }
    };

    // ---- rooms of all floors
    QVector<RoomRow> rooms;
    double totalArea = 0;
    for (PlanScene *s : floors) {
        QList<RoomItem *> rs = s->rooms();
        std::sort(rs.begin(), rs.end(), [](RoomItem *a, RoomItem *b) { return a->name().localeAwareCompare(b->name()) < 0; });
        for (RoomItem *r : rs) {
            rooms.push_back({s->floorName(), r});
            totalArea += r->area();
        }
    }
    auto roomCells = [&](const RoomRow &r) {
        QStringList c;
        if (multi) c << r.floor;
        c << r.room->name() << Geo::formatArea(r.room->area()) << Geo::formatMeters(r.room->perimeter());
        return c;
    };
    auto totalRow = [&] {
        QStringList tot{QStringLiteral("Σ ") + tr("Total")};
        if (multi) tot << QString();
        tot << Geo::formatArea(totalArea) << QString();
        return tot;
    };
    const QStringList roomHeader = multi ? QStringList{tr("Floor"), tr("Room"), tr("Area"), tr("Perimeter")}
                                         : QStringList{tr("Room"), tr("Area"), tr("Perimeter")};
    const QVector<double> roomFracs = multi ? QVector<double>{0.28, 0.32, 0.2, 0.2} : QVector<double>{0.5, 0.25, 0.25};
    const int roomsOnTitle = 14;

    // ---- plan the pages
    QVector<Page> pages;
    QHash<QString, QVector<LegendRow>> legends;     // key: floor|section
    if (opt.titlePage)
        pages.push_back(Page(Page::Title));
    if (opt.roomsTable && rooms.size() > roomsOnTitle) {
        const int perPage = std::max(1, int((content.height() - R.mm(20)) / R.mm(7)));
        for (int i = 0; i < rooms.size(); i += perPage) {
            Page pg(Page::Rooms);
            pg.roomStart = i;
            pg.roomCount = perPage;
            pages.push_back(pg);
        }
    }
    int bomTotal = 0;
    const QVector<BomRow> bom = billOfMaterials(floors, &bomTotal);
    if (opt.bom && !bom.isEmpty()) {
        // +1 row for the total line on the last page
        const int cap = R.bomCapacity(content);
        for (int i = 0; i < bom.size() + 1; i += cap) {
            Page pg(Page::Bom);
            pg.legendStart = i;
            pg.legendCount = cap;
            pages.push_back(pg);
        }
    }
    const int connPerPage = std::max(1, int(content.height() / R.mm(7)) - 1);
    for (int fi = 0; fi < floors.size(); ++fi) {
        PlanScene *s = floors[fi];
        if (s->isPanel()) {
            // electrical panel: drawing + equipment list, then the connection table
            pages.push_back(Page(Page::Panel, QString(), fi));
            const int nw = s->wires().size();
            for (int i = 0; i < nw; i += connPerPage) {
                Page c(Page::Connections, QString(), fi);
                c.legendStart = i;
                c.legendCount = connPerPage;
                pages.push_back(c);
            }
            continue;
        }
        if (opt.overviewPage)
            pages.push_back(Page(Page::Overview, QString(), fi));
        if (!opt.chapterPages)
            continue;
        const QSet<QString> used = s->usedSections();
        for (const SectionDef &sec : lib.sections()) {
            if (!used.contains(sec.id) || (!opt.chapters.isEmpty() && !opt.chapters.contains(sec.id)))
                continue;
            const QString key = QString::number(fi) + "|" + sec.id;
            legends[key] = legendFor(s, sec.id);
            const QVector<LegendRow> &rows = legends[key];
            Page pg(Page::Chapter, sec.id, fi);
            pg.legendCount = std::min<int>(R.legendCapacity(legendArea), rows.size());
            pages.push_back(pg);
            int next = pg.legendCount;
            while (next < rows.size()) {
                Page c(Page::LegendCont, sec.id, fi);
                c.legendStart = next;
                c.legendCount = std::min<int>(R.legendCapacity(fullLegendArea), rows.size() - next);
                pages.push_back(c);
                next += c.legendCount;
            }
        }
    }
    if (pages.isEmpty())
        pages.push_back(Page(Page::Overview, QString(), 0));

    QPainter p;
    if (!p.begin(&writer)) {
        if (error) *error = tr("Cannot write the file %1").arg(path);
        restore();
        return false;
    }
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const ProjectInfo &info = project->info();
    auto floorTitle = [&](int fi, const QString &t) {
        return multi ? floors[fi]->floorName() + QStringLiteral(" – ") + t : t;
    };

    for (int pi = 0; pi < pages.size(); ++pi) {
        const Page &pg = pages[pi];
        if (pi > 0)
            writer.newPage();
        PlanScene *scene = floors.value(pg.floor, floors.first());
        R.m_scene = scene;
        const QRectF source = sources.value(scene);

        if (pg.type == Page::Title) {
            const QRectF page = R.m_page;
            const double colW = page.width() * (opt.orientation == QPageLayout::Landscape ? 0.42 : 1.0);
            double y = R.mm(6);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(40, 60, 90));
            p.drawRect(QRectF(0, 0, R.mm(5), page.height()));
            p.setPen(QColor(25, 25, 25));
            p.setFont(R.font(30, true));
            p.drawText(QRectF(R.mm(10), y, colW - R.mm(10), R.mm(30)), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, info.name);
            y += R.mm(30);
            p.setFont(R.font(13));
            p.setPen(QColor(90, 90, 90));
            p.drawText(QRectF(R.mm(10), y, colW, R.mm(8)), Qt::AlignLeft | Qt::AlignVCenter, tr("Smart home installation plan"));
            y += R.mm(14);

            QVector<QStringList> infoRows;
            auto addInfo = [&](const QString &k, const QString &v) {
                if (!v.trimmed().isEmpty())
                    infoRows << QStringList{k, v};
            };
            addInfo(tr("Client"), info.client);
            addInfo(tr("Address"), info.address);
            addInfo(tr("Phone"), info.phone);
            addInfo(tr("Designer"), info.author);
            addInfo(tr("Company"), info.company);
            addInfo(tr("Date"), QLocale().toString(info.date.isValid() ? info.date : QDate::currentDate(), QLocale::LongFormat));
            if (multi) {
                QStringList names;
                for (PlanScene *s : floors)
                    names << s->floorName();
                addInfo(tr("Floors"), names.join(", "));
            }
            addInfo(tr("Ceiling height"), Geo::formatMeters(floors.first()->settings().ceilingHeight));
            p.setFont(R.font(10));
            for (const QStringList &r : infoRows) {
                p.setPen(QColor(110, 110, 110));
                p.drawText(QRectF(R.mm(10), y, R.mm(38), R.mm(6.5)), Qt::AlignLeft | Qt::AlignVCenter, r[0]);
                p.setPen(QColor(20, 20, 20));
                p.drawText(QRectF(R.mm(48), y, colW - R.mm(50), R.mm(6.5)), Qt::AlignLeft | Qt::AlignVCenter, r[1]);
                y += R.mm(6.5);
            }
            if (!info.notes.trimmed().isEmpty()) {
                y += R.mm(3);
                p.setFont(R.font(10, true));
                p.drawText(QRectF(R.mm(10), y, colW, R.mm(6)), Qt::AlignLeft | Qt::AlignVCenter, tr("Notes"));
                y += R.mm(6);
                p.setFont(R.font(9.5));
                QRectF usedR;
                p.drawText(QRectF(R.mm(10), y, colW - R.mm(12), R.mm(40)), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                           info.notes, &usedR);
                y += std::min(usedR.height(), R.mm(40)) + R.mm(4);
            }
            y += R.mm(4);

            // summary per chapter (all floors) with the first page of each chapter
            QVector<QStringList> sumRows;
            QVector<QColor> sw;
            int total = 0;
            for (const SectionDef &sec : lib.sections()) {
                if (!usedAll.contains(sec.id))
                    continue;
                int n = 0;
                for (PlanScene *s : floors)
                    n += countIn(s, sec.id);
                total += n;
                int pageNo = 0;
                for (int k = 0; k < pages.size(); ++k)
                    if (pages[k].type == Page::Chapter && pages[k].section == sec.id) { pageNo = k + 1; break; }
                sumRows << QStringList{sec.name, QString::number(n), pageNo ? QString::number(pageNo) : QStringLiteral("–")};
                sw << sec.color;
            }
            sumRows << QStringList{QStringLiteral("Σ ") + tr("Total"), QString::number(total), QString()};
            const double h = R.drawTable(p, QRectF(R.mm(10), y, colW - R.mm(12), R.mm(7) * (sumRows.size() + 1) + 1),
                                         {tr("Chapter"), tr("Elements"), tr("Page")}, {0.6, 0.2, 0.2}, sumRows, sw);
            y += h + R.mm(6);

            if (opt.roomsTable && !rooms.isEmpty() && rooms.size() <= roomsOnTitle) {
                QVector<QStringList> roomRows;
                for (const RoomRow &r : rooms)
                    roomRows << roomCells(r);
                roomRows << totalRow();
                R.drawTable(p, QRectF(R.mm(10), y, colW - R.mm(12), page.height() - y - R.mm(10)), roomHeader, roomFracs, roomRows);
            } else if (opt.roomsTable && !rooms.isEmpty()) {
                p.setFont(R.font(9.5));
                p.drawText(QRectF(R.mm(10), y, colW, R.mm(6)), Qt::AlignLeft | Qt::AlignVCenter,
                           tr("%1 rooms, total area %2 – see the rooms page").arg(rooms.size()).arg(Geo::formatArea(totalArea)));
            }

            if (opt.orientation == QPageLayout::Landscape) {
                PlanScene *prevScene = floors.first();
                for (PlanScene *fs : floors)
                    if (!fs->isPanel()) {
                        prevScene = fs;
                        break;
                    }
                R.m_scene = prevScene;
                prevScene->setHiddenSections({});
                prevScene->setExporting(true, false);
                const QRectF prev(colW + R.mm(8), R.mm(6), page.width() - colW - R.mm(10), page.height() - R.mm(12));
                p.setPen(QPen(QColor(210, 210, 210), R.mm(0.3)));
                p.setBrush(Qt::NoBrush);
                p.drawRect(prev);
                R.drawPlan(p, prev.adjusted(R.mm(3), R.mm(8), -R.mm(3), -R.mm(3)), sources.value(prevScene), false);
                if (multi) {
                    p.setFont(R.font(10, true));
                    p.setPen(QColor(70, 70, 70));
                    p.drawText(prev.adjusted(R.mm(3), R.mm(2), 0, 0), Qt::AlignLeft | Qt::AlignTop, prevScene->floorName());
                }
            }
            p.setFont(R.font(8));
            p.setPen(QColor(130, 130, 130));
            p.drawText(QRectF(R.mm(10), page.height() - R.mm(6), colW, R.mm(6)), Qt::AlignLeft | Qt::AlignVCenter,
                       tr("Page %1 / %2").arg(pi + 1).arg(pages.size()));
        } else if (pg.type == Page::Rooms) {
            R.header(p, tr("Rooms and areas"), QColor(40, 60, 90), pi + 1, pages.size());
            QVector<QStringList> roomRows;
            for (int i = pg.roomStart; i < std::min<int>(rooms.size(), pg.roomStart + pg.roomCount); ++i)
                roomRows << roomCells(rooms[i]);
            if (pg.roomStart + pg.roomCount >= rooms.size())
                roomRows << totalRow();
            R.drawTable(p, content, roomHeader, roomFracs, roomRows);
        } else if (pg.type == Page::Bom) {
            R.header(p, tr("Bill of materials"), QColor(40, 60, 90), pi + 1, pages.size());
            R.drawBom(p, content, bom, pg.legendStart, pg.legendCount, multi, QString::number(bomTotal));
        } else if (pg.type == Page::Overview) {
            scene->setHiddenSections({});
            scene->setExporting(true, false);
            R.header(p, floorTitle(pg.floor, tr("Plan – all elements")), QColor(40, 60, 90), pi + 1, pages.size());
            R.drawPlan(p, planArea, source);
            QVector<QStringList> rows;
            QVector<QColor> sw;
            const QSet<QString> used = scene->usedSections();
            for (const SectionDef &sec : lib.sections()) {
                if (!used.contains(sec.id))
                    continue;
                rows << QStringList{sec.name, QString::number(countIn(scene, sec.id))};
                sw << sec.color;
            }
            const double usedH = R.drawTable(p, legendArea, {tr("Chapter"), tr("Elements")}, {0.7, 0.3}, rows, sw);
            const QList<GroupItem *> groups = scene->groups();
            if (!groups.isEmpty()) {
                QVector<QStringList> grows;
                QVector<QColor> gsw;
                for (GroupItem *g : groups) {
                    QStringList ids;
                    for (QGraphicsItem *m : g->memberItems()) {
                        if (m->type() == ItemType::Device)
                            ids << static_cast<DeviceItem *>(m)->data().id;
                        else if (m->type() == ItemType::Strip)
                            ids << static_cast<StripItem *>(m)->data().id;
                    }
                    grows << QStringList{g->name(), compressIds(ids), g->note()};
                    gsw << g->color();
                }
                const QRectF ga(legendArea.left(), legendArea.top() + usedH + R.mm(6), legendArea.width(),
                                legendArea.height() - usedH - R.mm(6));
                R.drawTable(p, ga, {tr("Group"), tr("Elements"), tr("Note")}, {0.22, 0.46, 0.32}, grows, gsw, true);
            }
        } else if (pg.type == Page::Panel) {
            scene->setHiddenSections({});
            scene->setExporting(true, false);
            R.header(p, scene->parentFloor() ? scene->floorName() + QStringLiteral(" (") + scene->parentFloor()->floorName() + ")"
                                             : scene->floorName(),
                     QColor(74, 74, 74), pi + 1, pages.size());
            R.drawPlan(p, planArea, source, false);
            QList<EquipmentItem *> eqs = scene->equipment();
            std::sort(eqs.begin(), eqs.end(), [](EquipmentItem *a, EquipmentItem *b) {
                const QString ia = a->data().id, ib = b->data().id;
                if (ia.size() != ib.size())
                    return ia.size() < ib.size();
                return ia < ib;
            });
            QVector<QStringList> rows;
            for (EquipmentItem *e : eqs)
                rows << QStringList{e->data().id, e->data().displayName(), e->data().note};
            R.drawTable(p, legendArea, {tr("ID"), tr("Equipment"), tr("Note")}, {0.16, 0.5, 0.34}, rows, {}, true);
        } else if (pg.type == Page::Connections) {
            R.header(p, scene->floorName() + QStringLiteral(" – ") + tr("connections"), QColor(74, 74, 74), pi + 1, pages.size());
            QList<WireItem *> ws = scene->wires();
            std::sort(ws.begin(), ws.end(), [](WireItem *a, WireItem *b) {
                return a->endText(true).localeAwareCompare(b->endText(true)) < 0;
            });
            QVector<QStringList> rows;
            QVector<QColor> sw;
            for (int i = pg.legendStart; i < std::min<int>(ws.size(), pg.legendStart + pg.legendCount); ++i) {
                WireItem *w = ws[i];
                const EquipmentItem *a = w->fromItem();
                const EquipmentItem *b = w->toItem();
                rows << QStringList{WireItem::colorName(w->color()),
                                    w->endText(true) + (a ? QStringLiteral("  (") + a->data().displayName() + ")" : QString()),
                                    w->endText(false) + (b ? QStringLiteral("  (") + b->data().displayName() + ")" : QString()),
                                    w->crossSection() + QStringLiteral(" mm²"), w->label()};
                sw << w->color();
            }
            R.drawTable(p, content, {tr("Wire"), tr("From"), tr("To"), tr("Section"), tr("Label")},
                        {0.17, 0.29, 0.29, 0.09, 0.16}, rows, sw, true);
        } else {
            const SectionDef *sec = lib.section(pg.section);
            const QString name = sec ? sec->name : pg.section;
            const QColor accent = sec ? sec->color : QColor(40, 60, 90);
            const QVector<LegendRow> &rows = legends[QString::number(pg.floor) + "|" + pg.section];
            if (pg.type == Page::Chapter) {
                QSet<QString> hidden;
                for (const SectionDef &o : lib.sections())
                    if (o.id != pg.section)
                        hidden.insert(o.id);
                scene->setHiddenSections(hidden);
                scene->setExporting(true, opt.effects);
                R.header(p, floorTitle(pg.floor, name), accent, pi + 1, pages.size());
                R.drawPlan(p, planArea, source);
                R.drawLegend(p, legendArea, rows, pg.legendStart, pg.legendCount, accent);
            } else {
                R.header(p, floorTitle(pg.floor, name) + " – " + tr("legend (continued)"), accent, pi + 1, pages.size());
                R.drawLegend(p, fullLegendArea, rows, pg.legendStart, pg.legendCount, accent);
            }
        }
    }
    p.end();
    restore();
    return true;
}
