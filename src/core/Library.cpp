#include "Library.h"

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QDebug>

#include <functional>

DeviceKind deviceKindFromString(const QString &s)
{
    const QString t = s.trimmed().toLower();
    if (t == "light")   return DeviceKind::Light;
    if (t == "speaker") return DeviceKind::Speaker;
    if (t == "strip")   return DeviceKind::Strip;
    if (t == "wifi" || t == "ap" || t == "accesspoint" || t == "router") return DeviceKind::Wifi;
    return DeviceKind::Generic;
}

QString deviceKindToString(DeviceKind k)
{
    switch (k) {
    case DeviceKind::Light:   return "light";
    case DeviceKind::Speaker: return "speaker";
    case DeviceKind::Strip:   return "strip";
    case DeviceKind::Wifi:    return "wifi";
    default:                  return "generic";
    }
}

Library &Library::instance()
{
    static Library lib;
    return lib;
}

QStringList Library::searchPaths() const
{
    QStringList paths;
    const QByteArray env = qgetenv("SHP_LIBRARY_PATH");
    if (!env.isEmpty())
        paths << QString::fromLocal8Bit(env);
#ifdef SHP_SOURCE_LIBRARY_DIR
    // development build: the XML being edited in the source tree wins over the copy in the build
    // folder (that copy is only refreshed when the app is built)
    if (QDir(QStringLiteral(SHP_SOURCE_LIBRARY_DIR)).exists())
        paths << QStringLiteral(SHP_SOURCE_LIBRARY_DIR);
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    paths << appDir + "/Libraries"
          << appDir + "/../Resources/Libraries";          // macOS bundle
    return paths;
}

int Library::reload()
{
    m_roots.clear();
    m_byKey.clear();
    m_entryList.clear();
    m_entries.clear();
    m_sections.clear();
    m_files.clear();
    m_errors.clear();
    return loadAll();
}

int Library::loadAll()
{
    for (const QString &base : searchPaths()) {
        QDir dir(base);
        if (!dir.exists())
            continue;
        const auto libDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &libDir : libDirs) {
            const auto files = QDir(libDir.absoluteFilePath())
                                   .entryInfoList({"*.xml", "*.stilib"}, QDir::Files, QDir::Name);
            for (const QFileInfo &f : files)
                loadFile(f.absoluteFilePath());
        }
        if (!m_entries.empty())
            break;              // first location that contains libraries wins
    }
    // the user's own libraries, next to the built-in ones: Documents/SmartHomePlanner/Libraries/<name>/<name>.xml
    const QString user = userLibraryPath();
    if (QDir(user).exists() && !m_files.isEmpty() && !QFileInfo(m_files.first()).absolutePath().startsWith(QDir(user).absolutePath())) {
        const auto libDirs = QDir(user).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &libDir : libDirs) {
            const auto files = QDir(libDir.absoluteFilePath()).entryInfoList({"*.xml", "*.stilib"}, QDir::Files, QDir::Name);
            for (const QFileInfo &f : files)
                loadFile(f.absoluteFilePath());
        }
    }
    return int(m_entries.size());
}

QString Library::userLibraryPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/SmartHomePlanner/Libraries");
}

void Library::addSection(const SectionDef &s)
{
    for (SectionDef &existing : m_sections) {
        if (existing.id == s.id) {
            existing = s;
            return;
        }
    }
    m_sections.append(s);
}

const SectionDef *Library::section(const QString &id) const
{
    for (const SectionDef &s : m_sections)
        if (s.id == id)
            return &s;
    return nullptr;
}

QString Library::sectionName(const QString &id) const
{
    const SectionDef *s = section(id);
    return s ? s->name : id;
}

const LibraryEntry *Library::entry(const QString &key) const
{
    return m_byKey.value(key, nullptr);
}

namespace {

struct Inherited {
    QString section;
};

void readEntryAttributes(const QDomElement &e, LibraryEntry &le)
{
    if (e.hasAttribute("section")) le.section = e.attribute("section");
    if (e.hasAttribute("type"))    le.kind    = deviceKindFromString(e.attribute("type"));
    if (e.hasAttribute("note"))    le.note    = e.attribute("note");
    if (e.hasAttribute("doc"))     le.doc     = e.attribute("doc");
    if (e.hasAttribute("shop"))    le.shop    = e.attribute("shop");
    if (e.hasAttribute("size"))    le.size    = e.attribute("size").toDouble();
    else if (e.hasAttribute("width")) le.size = e.attribute("width").toDouble();
    if (e.hasAttribute("beam"))    le.beam    = e.attribute("beam").toDouble();
    if (e.hasAttribute("cct"))     le.cct     = e.attribute("cct").toInt();
    if (e.hasAttribute("radius"))  le.radius  = e.attribute("radius").toDouble();
    if (e.hasAttribute("color"))   le.color   = QColor(e.attribute("color"));
    if (e.hasAttribute("glow"))    le.glow    = e.attribute("glow").toDouble();
    if (e.hasAttribute("wallloss")) le.wallLoss = e.attribute("wallloss").toDouble();
    if (e.hasAttribute("tile"))    le.tile    = (e.attribute("tile") == QLatin1String("1")
                                                 || e.attribute("tile").compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) ? 1 : 0;
    if (le.size <= 0) le.size = 30;
    if (e.hasAttribute("width"))   le.width   = e.attribute("width").toDouble();
    if (e.hasAttribute("height"))  le.height  = e.attribute("height").toDouble();
    if (e.hasAttribute("prefix"))  le.idPrefix = e.attribute("prefix");
    if (e.hasAttribute("panel"))   le.panelOnly = e.attribute("panel").toInt() != 0;
    if (e.attribute("type") == QLatin1String("rail"))
        le.rail = true;
    // terminals of panel equipment
    const QDomElement firstHp = e.firstChildElement("HotPoint");
    if (!firstHp.isNull()) {
        le.ports.clear();
        le.portNames.clear();
        for (QDomElement hp = firstHp; !hp.isNull(); hp = hp.nextSiblingElement("HotPoint")) {
            le.ports << QPointF(hp.attribute("x").toDouble(), hp.attribute("y").toDouble());
            le.portNames << hp.attribute("name");
        }
    }
}

// Panel parts: real size of the front in mm. Missing height -> from the picture's aspect ratio.
void finishPanelEntry(LibraryEntry &le)
{
    static const QRegularExpression railName(QStringLiteral("din\\s*rail"), QRegularExpression::CaseInsensitiveOption);
    if (!le.rail && railName.match(le.name).hasMatch())
        le.rail = true;
    if (!le.isPanelPart())
        return;
    if (le.width <= 0)
        le.width = le.size > 0 ? le.size : 18;
    if (le.height <= 0) {
        QSize sz = QImageReader(le.iconPath).size();
        if (le.iconPath.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
            QSvgRenderer r(le.iconPath);
            if (r.isValid())
                sz = r.defaultSize();
        }
        le.height = (sz.isValid() && sz.width() > 0) ? le.width * sz.height() / sz.width() : (le.rail ? 35 : 86);
    }
}

} // namespace

bool Library::loadFile(const QString &xmlPath)
{
    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Library: cannot open" << xmlPath;
        return false;
    }
    QDomDocument doc;
    const auto res = doc.setContent(&f);
    if (!res) {
        qWarning() << "Library: XML error in" << xmlPath << res.errorMessage << "line" << res.errorLine;
        m_errors << QStringLiteral("%1, line %2: %3").arg(QFileInfo(xmlPath).fileName()).arg(res.errorLine).arg(res.errorMessage);
        return false;
    }
    const QDomElement root = doc.documentElement();
    if (root.tagName() != "Library")
        return false;

    const QString libName = root.attribute("name", QFileInfo(xmlPath).baseName());
    const QString assetsDir = QFileInfo(xmlPath).absolutePath() + "/Assets/";
    auto iconFile = [&](const QString &icon) {
        return icon.isEmpty() ? QString() : assetsDir + icon;
    };

    // Sections
    const QDomElement secs = root.firstChildElement("Sections");
    for (QDomElement s = secs.firstChildElement("Section"); !s.isNull(); s = s.nextSiblingElement("Section")) {
        SectionDef def;
        def.id     = s.attribute("id");
        def.name   = s.attribute("name", def.id);
        if (s.hasAttribute("name_" + m_lang))
            def.name = s.attribute("name_" + m_lang);
        def.prefix = s.attribute("prefix", def.id.left(1).toUpper());
        def.color  = QColor(s.attribute("color", "#777777"));
        if (!def.id.isEmpty())
            addSection(def);
    }

    auto libNode = std::make_unique<LibraryNode>();
    libNode->type = LibraryNode::Category;
    libNode->name = libName;
    libNode->iconPath = iconFile(root.attribute("icon"));

    std::function<void(const QDomElement &, LibraryNode *, const QString &, const QString &)> walk;
    walk = [&](const QDomElement &parentEl, LibraryNode *parentNode, const QString &path, const QString &inheritedSection) {
        for (QDomElement e = parentEl.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (e.tagName() == "Category") {
                auto node = std::make_unique<LibraryNode>();
                node->type = LibraryNode::Category;
                node->name = e.attribute("name");
                node->iconPath = iconFile(e.attribute("icon"));
                walk(e, node.get(), path + "/" + node->name, e.attribute("section", inheritedSection));
                parentNode->children.push_back(std::move(node));
            } else if (e.tagName() == "Item") {
                LibraryEntry base;
                base.library  = libName;
                base.name     = e.attribute("name");
                base.iconPath = iconFile(e.attribute("icon"));
                base.section  = inheritedSection.isEmpty() ? QStringLiteral("other") : inheritedSection;
                readEntryAttributes(e, base);
                finishPanelEntry(base);
                QString key = path + "/" + base.name;
                int dup = 2;
                while (m_byKey.contains(key))
                    key = path + "/" + base.name + "#" + QString::number(dup++);
                base.key = key;

                auto itemNode = std::make_unique<LibraryNode>();
                itemNode->type = LibraryNode::Item;
                itemNode->name = base.name;
                itemNode->iconPath = base.iconPath;

                auto stored = std::make_unique<LibraryEntry>(base);
                itemNode->entry = stored.get();
                m_byKey.insert(stored->key, stored.get());
                m_entryList.append(stored.get());
                m_entries.push_back(std::move(stored));

                for (QDomElement v = e.firstChildElement("Variant"); !v.isNull(); v = v.nextSiblingElement("Variant")) {
                    LibraryEntry ve = base;
                    ve.variant = v.attribute("name");
                    if (v.hasAttribute("icon") && !v.attribute("icon").isEmpty())
                        ve.iconPath = iconFile(v.attribute("icon"));
                    readEntryAttributes(v, ve);
                    finishPanelEntry(ve);
                    ve.key = base.key + "|" + ve.variant;
                    auto vStored = std::make_unique<LibraryEntry>(ve);
                    auto vNode = std::make_unique<LibraryNode>();
                    vNode->type = LibraryNode::Variant;
                    vNode->name = ve.variant;
                    vNode->iconPath = ve.iconPath;
                    vNode->entry = vStored.get();
                    m_byKey.insert(vStored->key, vStored.get());
                    m_entryList.append(vStored.get());
                    m_entries.push_back(std::move(vStored));
                    itemNode->children.push_back(std::move(vNode));
                }
                parentNode->children.push_back(std::move(itemNode));
            }
        }
    };
    walk(root, libNode.get(), libName, root.attribute("section"));
    m_roots.push_back(std::move(libNode));
    m_files << xmlPath;
    return true;
}
