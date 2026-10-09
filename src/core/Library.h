#ifndef LIBRARY_H
#define LIBRARY_H

#include <QColor>
#include <QHash>
#include <QPointF>
#include <QVector>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

// A chapter of the project (Lighting, Sensors, Music ...). Defined in the library XML.
struct SectionDef {
    QString id;
    QString name;
    QString prefix;      // used for automatic IDs: L1, L2, S1 ...
    QColor  color;
};

enum class DeviceKind { Generic, Light, Speaker, Strip, Wifi };

DeviceKind deviceKindFromString(const QString &s);
QString    deviceKindToString(DeviceKind k);

// One placeable element (an <Item> or one of its <Variant>s).
struct LibraryEntry {
    QString    key;          // unique: "<library>/<category path>/<item>[|<variant>]"
    QString    library;
    QString    name;         // item name
    QString    variant;      // variant name or empty
    QString    iconPath;     // absolute file path
    QString    section;      // section id
    DeviceKind kind = DeviceKind::Generic;
    QString    note;         // default note shown on the plan (e.g. "3x1.5", "Tree")
    QString    doc;          // documentation link / path
    QString    shop;         // shop link
    double     size   = 30;  // symbol size on the plan [cm]
    double     beam   = 0;   // light beam angle [deg]
    int        cct    = 3000;// light color temperature [K]
    double     radius = 0;   // speaker / Wi-Fi coverage radius [cm]
    double     wallLoss = 35;// Wi-Fi: % of the range lost by every wall crossed
    QColor     color;        // LED strip color
    // white rounded background behind the symbol on the plan: tile="1"/"0" in the XML;
    // without the attribute: on for SVG symbols, off for pictures (PNG ...)
    int        tile = -1;    // -1 = not set in the XML
    bool wantsTile() const { return tile >= 0 ? tile != 0 : iconPath.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive); }
    double     glow   = 35;  // LED strip glow width [cm]
    // ---- electrical panel equipment (DIN rail modules); sizes in mm
    double     width  = 0;   // front width [mm]
    double     height = 0;   // front height [mm]
    QVector<QPointF> ports;  // <HotPoint x y> terminals, in mm from the top-left corner
    QStringList portNames;   // <HotPoint name="L"> (empty = numbered 1, 2, 3 ...)
    QString    idPrefix;     // prefix of the automatic IDs (else: Q for panel equipment, section prefix otherwise)
    bool       rail = false; // a DIN rail
    bool       panelOnly = false; // panel="1"

    // goes into the electrical panel pages (not on the floor plans)
    bool isPanelPart() const { return rail || panelOnly || !ports.isEmpty(); }

    QString displayName() const { return variant.isEmpty() ? name : name + " (" + variant + ")"; }
};

struct LibraryNode {
    enum Type { Category, Item, Variant };
    Type    type = Category;
    QString name;
    QString iconPath;
    const LibraryEntry *entry = nullptr;     // for Item / Variant
    std::vector<std::unique_ptr<LibraryNode>> children;
};

class Library
{
public:
    static Library &instance();

    // UI language ("en", "ro" ...): <Section name_ro="..."> is used when present
    void setLanguage(const QString &lang) { m_lang = lang; }

    // Loads every library folder found in the search paths. Returns number of entries.
    int  loadAll();
    // drops everything and reads the XML files again (after editing a library)
    int  reload();
    bool loadFile(const QString &xmlPath);
    QStringList searchPaths() const;
    QStringList loadedFiles() const { return m_files; }
    QStringList errors() const { return m_errors; }    // XML files that could not be read

    const QList<SectionDef> &sections() const { return m_sections; }
    const SectionDef *section(const QString &id) const;
    QString sectionName(const QString &id) const;

    const LibraryEntry *entry(const QString &key) const;
    const QList<const LibraryEntry *> &entries() const { return m_entryList; }
    const std::vector<std::unique_ptr<LibraryNode>> &roots() const { return m_roots; }

private:
    Library() = default;
    void addSection(const SectionDef &s);

    QList<SectionDef> m_sections;
    std::vector<std::unique_ptr<LibraryEntry>> m_entries;
    QList<const LibraryEntry *> m_entryList;
    QHash<QString, const LibraryEntry *> m_byKey;
    std::vector<std::unique_ptr<LibraryNode>> m_roots;
    QStringList m_files;
    QStringList m_errors;
    QString m_lang = QStringLiteral("en");
};

#endif // LIBRARY_H
