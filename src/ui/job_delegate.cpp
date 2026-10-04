#include "job_delegate.h"
#include "core/cookies.h"
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QAbstractItemView>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QToolTip>

namespace ytdlp {
QString byteText(double bytes) {
    const QStringList units{"B", "KiB", "MiB", "GiB", "TiB"}; int unit = 0;
    while (bytes >= 1024 && unit < units.size() - 1) { bytes /= 1024; ++unit; }
    return QString::number(bytes, 'f', unit == 0 ? 0 : 1) + ' ' + units[unit];
}
QString durationText(double seconds) {
    if (seconds < 0) return "—";
    const int total = int(seconds);
    return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
}
QRect JobDelegate::removeButtonRect(const QRect &row) {
    const auto area = row.adjusted(0, 4, -2, -4);
    return {area.right() - 45, area.top() + 10, 32, 32};
}
bool JobDelegate::canRemove(const QModelIndex &index) const {
    const auto job = index.data(Qt::UserRole).toJsonObject();
    return !job["id"].toString().isEmpty() && job["status"] != "running" && job["id"] != activeId;
}
void JobDelegate::paintRemoveButton(QPainter *painter, const QRect &row, const QModelIndex &index) const {
    const auto rect = removeButtonRect(row); const bool enabled = canRemove(index);
    const bool hovered = enabled && index == hovered_;
    painter->save(); painter->setPen(Qt::NoPen);
    if (hovered) { painter->setBrush(theme.color("surface_alt")); painter->drawRoundedRect(rect, 6, 6); }
    if (!enabled) painter->setOpacity(0.4);
    painter->setPen(QPen(theme.color(hovered ? "danger" : "muted"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush); painter->translate(rect.center() - QPoint(9, 9));
    painter->drawLine(2, 4, 16, 4); painter->drawRoundedRect(QRectF(6, 1, 6, 3), 1, 1);
    painter->drawPolyline(QPolygonF{QPointF(4, 4), QPointF(5, 17), QPointF(13, 17), QPointF(14, 4)});
    painter->drawLine(7, 7, 7, 13); painter->drawLine(11, 7, 11, 13); painter->restore();
}
bool JobDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) {
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease && event->type() != QEvent::MouseButtonDblClick)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    const auto *mouse = static_cast<QMouseEvent *>(event);
    const bool inside = removeButtonRect(option.rect).contains(mouse->position().toPoint()) && mouse->button() == Qt::LeftButton;
    const auto id = index.data(Qt::UserRole).toJsonObject()["id"].toString();
    if (event->type() == QEvent::MouseButtonPress) pressedId_ = inside && canRemove(index) ? id : QString();
    if (event->type() == QEvent::MouseButtonRelease) {
        const bool remove = inside && canRemove(index) && pressedId_ == id; pressedId_.clear();
        if (remove) emit removeRequested(id);
    }
    return inside || QStyledItemDelegate::editorEvent(event, model, option, index);
}
bool JobDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) {
    if (removeButtonRect(option.rect).contains(event->pos())) {
        QToolTip::showText(event->globalPos(), canRemove(index) ? "Remove from history. Downloaded files stay in place." : "Cancel this download before removing it.", view);
        return true;
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}
bool JobDelegate::eventFilter(QObject *object, QEvent *event) {
    auto *view = qobject_cast<QAbstractItemView *>(parent());
    if (!view || object != view->viewport()) return QStyledItemDelegate::eventFilter(object, event);
    if (event->type() == QEvent::Leave) {
        hovered_ = QPersistentModelIndex(); view->viewport()->unsetCursor(); view->viewport()->update();
    } else if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonDblClick) {
        const auto position = static_cast<QMouseEvent *>(event)->position().toPoint(); const auto index = view->indexAt(position);
        const bool inside = index.isValid() && removeButtonRect(view->visualRect(index)).contains(position);
        if (event->type() == QEvent::MouseButtonDblClick && inside) { pressedId_.clear(); return true; }
        const QPersistentModelIndex next = inside && canRemove(index) ? index : QModelIndex();
        if (next != hovered_) { hovered_ = next; view->viewport()->update(); }
        view->viewport()->setCursor(next.isValid() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    return QStyledItemDelegate::eventFilter(object, event);
}
void JobDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    painter->save(); painter->setRenderHint(QPainter::Antialiasing);
    const auto job = index.data(Qt::UserRole).toJsonObject();
    const auto status = job["status"].toString();
    const auto area = option.rect.adjusted(0, 4, -2, -4);
    const bool selected = option.state & QStyle::State_Selected;
    painter->setBrush(theme.color(selected ? "surface_alt" : "surface"));
    painter->setPen(QPen(theme.color(selected ? "accent" : "border"), 1)); painter->drawRoundedRect(area, 12, 12);
    const auto color = theme.color(status == "failed" ? "danger" : status == "done" ? "success" : status == "running" ? "accent" : "muted");
    const QRect badge(area.right() - 148, area.top() + 14, 90, 22);
    painter->setPen(color); auto badgeFont = option.font; badgeFont.setPointSize(10); badgeFont.setBold(true); painter->setFont(badgeFont);
    const auto phase = job["phase"].toString();
    painter->drawText(badge, Qt::AlignRight | Qt::AlignVCenter, status == "running" ? (phase == "processing" ? "Processing" : "Downloading") : status.left(1).toUpper() + status.mid(1));
    auto font = option.font; font.setPointSize(11); font.setBold(true); painter->setFont(font); painter->setPen(theme.color("text"));
    const auto title = job["title"].toString().isEmpty() ? job["url"].toString() : job["title"].toString();
    const QRect titleRect(area.left() + 16, area.top() + 13, area.width() - 178, 24);
    painter->drawText(titleRect, Qt::AlignVCenter, QFontMetrics(font).elidedText(title, Qt::ElideRight, titleRect.width()));
    font.setBold(false); font.setPointSize(10); painter->setFont(font); painter->setPen(theme.color("muted"));
    auto type = job["mode"] == "audio" ? "Audio · " + job["audioFormat"].toString() : "Video · " + (job["quality"] == "best" ? QString("Best available") : job["quality"].toString() + "p");
    const auto format = job["videoFormat"].toString("auto");
    if (job["mode"] != "audio" && format != "auto") type += " · " + format.toUpper();
    painter->drawText(QRect(area.left() + 16, area.top() + 40, area.width() - 32, 20), Qt::AlignVCenter,
        QFontMetrics(font).elidedText(type + "  ·  " + job["dest"].toString(), Qt::ElideMiddle, area.width() - 32));
    const QRectF track(area.left() + 16, area.bottom() - 18, area.width() - 32, 4);
    painter->setPen(Qt::NoPen); painter->setBrush(theme.color("border")); painter->drawRoundedRect(track, 2, 2);
    const double percent = job["percent"].toDouble();
    if (percent > 0) { painter->setBrush(color); painter->drawRoundedRect(QRectF(track.topLeft(), QSizeF(track.width() * percent / 100, 4)), 2, 2); }
    QString detail = status == "queued" ? "Waiting in queue" : status == "done" ? "Saved to your folder" : status == "cancelled" ? "Cancelled" : status == "failed" ? job["error"].toString().section('\n', -1) :
        (job["percent"].isDouble() ? QString::number(percent, 'f', 1) + "%" : "Preparing…")
        + (job["speed"].toDouble() > 0 ? "  ·  " + byteText(job["speed"].toDouble()) + "/s" : "")
        + (job["eta"].isDouble() ? "  ·  " + durationText(job["eta"].toDouble()) + " left" : "");
    if (status == "failed" && !authenticationHint(job["error"].toString()).isEmpty()) detail = authenticationHint(job["error"].toString());
    painter->setPen(status == "failed" ? color : theme.color("muted"));
    painter->drawText(QRect(area.left() + 16, area.top() + 63, area.width() - 32, 18), Qt::AlignVCenter,
        QFontMetrics(font).elidedText(detail, Qt::ElideRight, area.width() - 32));
    paintRemoveButton(painter, option.rect, index); painter->restore();
}
}
