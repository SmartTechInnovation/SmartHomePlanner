#ifndef CHECKDELEGATE_H
#define CHECKDELEGATE_H

#include <QStyledItemDelegate>

// Check-box list rows drawn by the app itself: the native macOS style does not draw the
// check boxes of item views reliably (often only on the current row).
//   [x] (icon | color swatch)  Text ............ count
// Items need Qt::ItemIsUserCheckable to get a box.
class CheckDelegate : public QStyledItemDelegate
{
public:
    enum Role { ColorRole = Qt::UserRole + 40, CountRole };

    // wholeRow: a click anywhere on the row toggles (else only on the box / swatch)
    explicit CheckDelegate(QObject *parent = nullptr, bool wholeRow = true)
        : QStyledItemDelegate(parent), m_wholeRow(wholeRow) {}

    static QRect boxRect(const QRect &row) { return QRect(row.left() + 6, row.center().y() - 8, 16, 16); }
    static constexpr int kBoxZone = 48;         // x below this = the box area

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    bool editorEvent(QEvent *e, QAbstractItemModel *model, const QStyleOptionViewItem &opt, const QModelIndex &idx) override;

private:
    bool m_wholeRow;
};

#endif // CHECKDELEGATE_H
