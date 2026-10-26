#include "modal.h"
#include "modal_motion.h"
#include <QPainter>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace ytdlp {
Modal::Modal(QWidget *parent, QWidget *content) : QWidget(parent), content_(content) {
    setObjectName("modalBackdrop"); hide();
}
void Modal::open(const QString &title, QWidget *body, QWidget *footer, Sizing sizing) {
    const bool swapping = isVisible() && !closing_;
    if (isVisible() && closing_) finishDismiss();
    if (!swapping) { focus_ = QApplication::focusWidget(); backdropProgress_ = 0; }
    if (card_) { delete card_; card_ = nullptr; motion_ = nullptr; }
    sizing_ = sizing;
    card_ = new QFrame(this); card_->setObjectName("modalCard");
    auto *layout = new QVBoxLayout(card_); layout->setContentsMargins(24, 22, 24, 22); layout->setSpacing(16);
    auto *header = new QHBoxLayout;
    auto *heading = new QLabel(title); heading->setObjectName("sectionTitle");
    auto *close = new QPushButton("Close"); close->setObjectName("closeModal");
    connect(close, &QPushButton::clicked, this, &Modal::dismiss);
    header->addWidget(heading, 1); header->addWidget(close); layout->addLayout(header);
    scroll_ = new QScrollArea; scroll_->setWidgetResizable(true); scroll_->setFrameShape(QFrame::NoFrame);
    if (sizing_ == Sizing::Content) scroll_->viewport()->setStyleSheet("background: transparent;");
    scroll_->setWidget(body); layout->addWidget(scroll_, 1);
    if (footer) layout->addWidget(footer);
    error_ = new QLabel; error_->setObjectName("modalError"); error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true); error_->hide(); layout->addWidget(error_);
    card_->ensurePolished();
    closing_ = false; motion_ = new ModalMotion(card_); card_->setGraphicsEffect(motion_);
    connect(motion_, &ModalMotion::progressChanged, this, [this, swapping](qreal progress) { if (!swapping || closing_) { backdropProgress_ = progress; update(); } });
    connect(motion_, &ModalMotion::finished, this, [this](bool open) { if (!open && closing_) finishDismiss(); });
    content_->setEnabled(false); setGeometry(parentWidget()->rect()); positionCard(); show(); raise();
    if (!swapping) qApp->installEventFilter(this);
    close->setFocus(); motion_->animate(true);
}
void Modal::dismiss() {
    if (!isVisible() || closing_) return;
    closing_ = true; if (card_) card_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    if (motion_) motion_->animate(false); else finishDismiss();
}
void Modal::finishDismiss() {
    closing_ = false; hide(); qApp->removeEventFilter(this); content_->setEnabled(true);
    if (focus_) focus_->setFocus();
    if (card_) { card_->deleteLater(); card_ = nullptr; scroll_ = nullptr; error_ = nullptr; motion_ = nullptr; }
}
void Modal::paintEvent(QPaintEvent *) {
    if (!isVisible() || !backdrop_.isValid()) return;
    QPainter painter(this); auto color = backdrop_; color.setAlphaF(color.alphaF() * backdropProgress_); painter.fillRect(rect(), color);
}
void Modal::showError(const QString &message) { if (error_) { error_->setText(message); error_->show(); positionCard(); } }
int Modal::contentHeight(int cardWidth) const {
    const auto *layout = card_->layout();
    const auto margins = layout->contentsMargins();
    const auto *body = scroll_->widget();
    const int bodyWidth = cardWidth - margins.left() - margins.right() - 2 * (card_->frameWidth() + scroll_->frameWidth());
    const int bodyHeight = body->hasHeightForWidth() ? body->heightForWidth(bodyWidth) : body->sizeHint().height();
    return card_->sizeHint().height() - scroll_->sizeHint().height() + bodyHeight;
}
void Modal::positionCard() {
    if (!card_) return;
    const int cardWidth = std::min(650, width() - 40);
    const int preferredHeight = sizing_ == Sizing::Content ? contentHeight(cardWidth) : 540;
    const auto size = QSize(cardWidth, std::min(preferredHeight, height() - 40));
    card_->setGeometry((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
}
void Modal::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); positionCard(); }
void Modal::mousePressEvent(QMouseEvent *event) { if (card_ && !closing_ && !card_->geometry().contains(event->position().toPoint())) dismiss(); }
bool Modal::eventFilter(QObject *object, QEvent *event) {
    if (!isVisible()) return false;
    if (closing_ && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) return true;
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) { dismiss(); return true; }
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            const auto buttons = card_->findChildren<QWidget *>();
            QList<QWidget *> focusable;
            for (auto *widget : buttons) if (widget->isVisible() && widget->isEnabled() && (widget->focusPolicy() & Qt::TabFocus)) focusable.append(widget);
            if (!focusable.isEmpty()) {
                int index = focusable.indexOf(QApplication::focusWidget());
                const bool backward = key->key() == Qt::Key_Backtab || (key->modifiers() & Qt::ShiftModifier);
                index = (index + (backward ? -1 : 1) + focusable.size()) % focusable.size();
                focusable[index]->setFocus(); return true;
            }
        }
    }
    return QWidget::eventFilter(object, event);
}
}
