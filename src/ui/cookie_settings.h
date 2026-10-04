#pragma once
#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

namespace ytdlp {
class CookieSettings : public QWidget {
public:
    explicit CookieSettings(const QJsonObject &settings, QWidget *parent = nullptr);
    QJsonObject options() const;
private:
    QComboBox *source_;
    QLineEdit *browser_;
    QLineEdit *file_;
    QLabel *browserLabel_;
    QLabel *fileLabel_;
    void refresh();
};
}
