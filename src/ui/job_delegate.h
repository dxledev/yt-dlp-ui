#pragma once
#include "core/theme.h"
#include <QStyledItemDelegate>
#include <QPersistentModelIndex>

namespace ytdlp {
QString byteText(double bytes);
QString durationText(double seconds);
class JobDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit JobDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    Theme theme = presetTheme("slate");
    QString activeId;
    static QRect removeButtonRect(const QRect &row);
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {300, 114}; }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override;
signals:
    void removeRequested(const QString &id);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    QPersistentModelIndex hovered_;
    QString pressedId_;
    bool canRemove(const QModelIndex &index) const;
    void paintRemoveButton(QPainter *painter, const QRect &row, const QModelIndex &index) const;
};
}
