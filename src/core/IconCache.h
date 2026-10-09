#ifndef ICONCACHE_H
#define ICONCACHE_H

#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <memory>

class QPainter;
class QSvgRenderer;

// Shared cache for library symbols (SVG rendered as vectors, raster images as pixmaps)
class IconCache
{
public:
    static IconCache &instance();

    void  draw(QPainter *p, const QString &path, const QRectF &target);
    QIcon icon(const QString &path);
    // same, always on a white rounded tile (app icons shown in lists that can be dark)
    QIcon tiledIcon(const QString &path);
    bool  has(const QString &path) { return !path.isEmpty() && get(path).valid; }

private:
    struct Entry {
        std::shared_ptr<QSvgRenderer> svg;
        QPixmap pixmap;
        bool valid = false;
    };
    Entry &get(const QString &path);
    QHash<QString, Entry> m_cache;
    QHash<QString, QIcon> m_icons;
    QIcon make(const QString &path, bool tile);
};

#endif // ICONCACHE_H
