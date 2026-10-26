#pragma once
#include <QColor>
#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QScrollArea>
#include <QWidget>

namespace ytdlp {
class ModalMotion;
class Modal : public QWidget {
    Q_OBJECT
public:
    enum class Sizing { Expanded, Content };
    Modal(QWidget *parent, QWidget *content);
    void open(const QString &title, QWidget *body, QWidget *footer = nullptr, Sizing sizing = Sizing::Expanded);
    void dismiss();
    void showError(const QString &message);
    void setBackdrop(const QColor &color) { backdrop_ = color; update(); }
    bool eventFilter(QObject *object, QEvent *event) override;
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
private:
    QWidget *content_;
    QFrame *card_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QLabel *error_ = nullptr;
    Sizing sizing_ = Sizing::Expanded;
    QPointer<QWidget> focus_;
    ModalMotion *motion_ = nullptr;
    QColor backdrop_;
    bool closing_ = false;
    qreal backdropProgress_ = 0;
    void finishDismiss();
    int contentHeight(int cardWidth) const;
    void positionCard();
};
}
