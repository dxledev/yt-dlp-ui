#pragma once
#include "core/storage.h"
#include "ipc/client.h"
#include "job_delegate.h"
#include "modal.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>

namespace ytdlp {
class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(Paths paths, QWidget *parent = nullptr, bool connectService = true);
    void openSettings();
    void openDetails();
    void openFolderPicker();
    void inspectLink();
    Modal *modal() const { return modal_; }
protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    Paths paths_;
    Client client_;
    Theme theme_ = presetTheme("slate");
    QWidget *content_ = nullptr;
    Modal *modal_ = nullptr;
    QLabel *brand_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *footer_ = nullptr;
    QLabel *count_ = nullptr;
    QLabel *emptyText_ = nullptr;
    QLineEdit *url_ = nullptr;
    QLineEdit *destination_ = nullptr;
    QComboBox *quality_ = nullptr;
    QComboBox *videoFormat_ = nullptr;
    QPushButton *video_ = nullptr;
    QPushButton *audio_ = nullptr;
    QPushButton *pause_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *retry_ = nullptr;
    QPushButton *details_ = nullptr;
    QListWidget *jobs_ = nullptr;
    JobDelegate *delegate_ = nullptr;
    QStackedWidget *queuePages_ = nullptr;
    QJsonObject snapshot_;
    QString mode_ = "video";
    bool initialized_ = false;
    void buildInterface();
    QWidget *buildComposer();
    QWidget *buildQueue();
    void applyTheme();
    void setMode(const QString &mode);
    void addDownload();
    void updateActions();
    QJsonObject selectedJob() const;
    void message(const QString &text, bool error = false);
    void confirmClear();
    void openMetadata(const QJsonObject &data);
private slots:
    void updateSnapshot(const QJsonObject &data);
};
}
