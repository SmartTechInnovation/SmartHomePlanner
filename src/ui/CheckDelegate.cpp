#include "CheckDelegate.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

QSize CheckDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
    QSize s = QStyledItemDelegate::sizeHint(opt, idx);
    s.setHeight(std::max(s.height(), 24));
    return s;
}

void CheckDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRect r = opt.rect;
    const QPalette &pal = opt.palette;
    const bool enabled = (opt.state & QStyle::State_Enabled);
    if (enabled && (opt.state & QStyle::State_Selected))
        p->fillRect(r, pal.color(QPalette::Highlight).lighter(170));
    else if (enabled && (opt.state & QStyle::State_MouseOver))
        p->fillRect(r, pal.color(QPalette::AlternateBase));

    const bool checkable = idx.flags() & Qt::ItemIsUserCheckable;
    const bool checked = idx.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    double x = r.left() + 6;

    if (checkable) {
        const QRectF box = QRectF(boxRect(r)).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor on = enabled ? QColor(0, 122, 255) : QColor(170, 190, 215);
        p->setPen(QPen(checked ? on.darker(115) : QColor(140, 140, 140), 1.2));
        p->setBrush(checked ? on : pal.color(QPalette::Base));
        p->drawRoundedRect(box, 3.5, 3.5);
        if (checked) {
            p->setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPolyline(QPolygonF{QPointF(box.left() + 3.5, box.center().y() + 0.3),
                                      QPointF(box.left() + 6.7, box.bottom() - 3.5),
                                      QPointF(box.right() - 3.2, box.top() + 4)});
        }
        x = box.right() + 8;
    }

    // color swatch or icon
    const QVariant colorV = idx.data(ColorRole);
    const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
    if (colorV.isValid()) {
        const QColor c = colorV.value<QColor>();
        p->setPen(c.darker(140));
        p->setBrush(c);
        p->drawRoundedRect(QRectF(x, r.center().y() - 6.5, 13, 13), 3, 3);
        x += 13 + 8;
    } else if (!icon.isNull()) {
        icon.paint(p, QRect(int(x), r.center().y() - 8, 16, 16), Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
        x += 16 + 8;
    }

    // text and optional count
    const QVariant countV = idx.data(CountRole);
    QFont f = opt.font;
    if (countV.isValid())
        f.setBold(countV.toInt() > 0);
    p->setFont(f);
    const bool dim = !enabled || (checkable && !checked);
    p->setPen(dim ? pal.color(QPalette::Disabled, QPalette::Text) : pal.color(QPalette::Text));
    int right = r.right() - 6;
    if (countV.isValid()) {
        const QRect countRect(r.right() - 40, r.top(), 34, r.height());
        p->drawText(countRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(countV.toInt()));
        right = countRect.left() - 4;
    }
    const QRect textRect(int(x), r.top(), right - int(x), r.height());
    p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(f).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width()));
    p->restore();
}

bool CheckDelegate::editorEvent(QEvent *e, QAbstractItemModel *model, const QStyleOptionViewItem &opt, const QModelIndex &idx)
{
    if (!(idx.flags() & Qt::ItemIsUserCheckable) || !(idx.flags() & Qt::ItemIsEnabled))
        return QStyledItemDelegate::editorEvent(e, model, opt, idx);
    auto toggle = [&] {
        const bool checked = idx.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        model->setData(idx, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    };
    const bool inBox = [&] {
        if (e->type() != QEvent::MouseButtonRelease && e->type() != QEvent::MouseButtonDblClick)
            return false;
        return static_cast<QMouseEvent *>(e)->position().x() - opt.rect.left() < kBoxZone;
    }();
    if (e->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton && (m_wholeRow || inBox)) {
            toggle();
            return true;
        }
    } else if (e->type() == QEvent::MouseButtonDblClick) {
        if (m_wholeRow || inBox)
            return true;            // a double click = two clicks: both releases already toggle
    } else if (e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Space || ke->key() == Qt::Key_Select) {
            toggle();
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(e, model, opt, idx);
}
