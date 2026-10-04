#pragma once
#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QScrollArea>
#include <QWidget>

namespace ytdlp {
class Modal : public QWidget {
    Q_OBJECT
public:
    enum class Sizing { Expanded, Content };
    Modal(QWidget *parent, QWidget *content);
    void open(const QString &title, QWidget *body, QWidget *footer = nullptr, Sizing sizing = Sizing::Expanded);
    void dismiss();
    void showError(const QString &message);
    bool eventFilter(QObject *object, QEvent *event) override;
protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
private:
    QWidget *content_;
    QFrame *card_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QLabel *error_ = nullptr;
    Sizing sizing_ = Sizing::Expanded;
    QPointer<QWidget> focus_;
    int contentHeight(int cardWidth) const;
    void positionCard();
};
}
