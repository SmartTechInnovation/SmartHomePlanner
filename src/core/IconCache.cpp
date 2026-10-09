#include "IconCache.h"

#include <QFileInfo>
#include <QPainter>
#include <QSvgRenderer>
#include <algorithm>

IconCache &IconCache::instance()
{
    static IconCache c;
    return c;
}

IconCache::Entry &IconCache::get(const QString &path)
{
    auto it = m_cache.find(path);
    if (it != m_cache.end())
        return it.value();
    Entry e;
    if (path.endsWith(".svg", Qt::CaseInsensitive)) {
        auto r = std::make_shared<QSvgRenderer>(path);
        if (r->isValid()) {
            r->setAspectRatioMode(Qt::KeepAspectRatio);
            e.svg = r;
            e.valid = true;
        }
    } else if (QFileInfo::exists(path)) {
        e.pixmap = QPixmap(path);
        e.valid = !e.pixmap.isNull();
    }
    return m_cache.insert(path, e).value();
}

void IconCache::draw(QPainter *p, const QString &path, const QRectF &target)
{
    Entry &e = get(path);
    if (!e.valid) {
        p->save();
        p->setPen(QPen(Qt::red, 0));
        p->setBrush(QColor(255, 0, 0, 40));
        p->drawRect(target);
        p->drawLine(target.topLeft(), target.bottomRight());
        p->drawLine(target.topRight(), target.bottomLeft());
        p->restore();
        return;
    }
    if (e.svg) {
        e.svg->render(p, target);
    } else {
        QSizeF s = e.pixmap.size();
        s.scale(target.size(), Qt::KeepAspectRatio);
        QRectF r(QPointF(), s);
        r.moveCenter(target.center());
        p->save();
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->drawPixmap(r, e.pixmap, e.pixmap.rect());
        p->restore();
    }
}

QIcon IconCache::icon(const QString &path)
{
    // library symbols (files, not the app's own ":/" icons) are dark drawings made for white paper:
    // they get a white rounded tile so they stay visible on a dark (macOS dark mode) background
    return make(path, !path.startsWith(QLatin1Char(':')));
}

QIcon IconCache::tiledIcon(const QString &path)
{
    return make(path, true);
}

QIcon IconCache::make(const QString &path, bool tile)
{
    const QString key = (tile ? QStringLiteral("T|") : QStringLiteral("P|")) + path;
    auto it = m_icons.find(key);
    if (it != m_icons.end())
        return it.value();
    // rendered here (no dependency on the SVG icon-engine plugin), several sizes for sharp toolbars
    QIcon ic;
    for (int size : {16, 24, 32, 48, 64}) {
        for (qreal dpr : {1.0, 2.0}) {
            const int px = int(size * dpr);
            QPixmap pm(px, px);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.scale(dpr, dpr);
            double m = size >= 32 ? size / 24.0 : 1.0;
            if (tile) {
                const QRectF t(0.5, 0.5, size - 1.0, size - 1.0);
                p.setPen(QPen(QColor(200, 200, 200), 1.0 / dpr));
                p.setBrush(Qt::white);
                p.drawRoundedRect(t, size * 0.2, size * 0.2);
                m = std::max(m, size * 0.12);
            }
            draw(&p, path, QRectF(m, m, size - 2 * m, size - 2 * m));
            p.end();
            pm.setDevicePixelRatio(dpr);
            ic.addPixmap(pm);
        }
    }
    m_icons.insert(key, ic);
    return ic;
}
