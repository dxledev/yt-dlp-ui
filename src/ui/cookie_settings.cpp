#include "cookie_settings.h"
#include "core/cookies.h"
#include <QFormLayout>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <stdexcept>

namespace ytdlp {
CookieSettings::CookieSettings(const QJsonObject &settings, QWidget *parent) : QWidget(parent) {
    setObjectName("cookieSettings");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10);
    auto *heading = new QLabel("Sign-in cookies"); heading->setObjectName("sectionTitle"); layout->addWidget(heading);
    auto *form = new QFormLayout;
    source_ = new QComboBox; source_->setObjectName("cookieSource"); source_->addItem("No cookies", "");
    for (const auto &name : browserNames()) source_->addItem(name.left(1).toUpper() + name.mid(1), name);
    source_->addItem("Cookie file", "file"); form->addRow("Use cookies from", source_);
    browser_ = new QLineEdit(settings["cookiesFromBrowser"].toString()); browser_->setObjectName("cookieBrowserInput");
    browser_->setPlaceholderText("brave or brave:Profile 1");
    browserLabel_ = new QLabel("Browser / profile"); form->addRow(browserLabel_, browser_);
    file_ = new QLineEdit(settings["cookiesFile"].toString()); file_->setObjectName("cookieFileInput"); file_->setPlaceholderText("/path/to/cookies.txt");
    fileLabel_ = new QLabel("Netscape cookie file"); form->addRow(fileLabel_, file_); layout->addLayout(form);
    auto *hint = new QLabel("Select the browser where you are signed into YouTube. yt-dlp reads its cookies when you download or view media info. Save, then retry the failed download.");
    hint->setObjectName("muted"); hint->setWordWrap(true); layout->addWidget(hint);
    const auto selected = file_->text().isEmpty() ? browser_->text().section(QRegularExpression("[+:]"), 0, 0) : QString("file");
    source_->setCurrentIndex(std::max(0, source_->findData(selected)));
    connect(source_, &QComboBox::currentIndexChanged, this, [this] { refresh(); }); refresh();
}
void CookieSettings::refresh() {
    const auto source = source_->currentData().toString();
    const bool browser = !source.isEmpty() && source != "file";
    browser_->setVisible(browser); browserLabel_->setVisible(browser);
    file_->setVisible(source == "file"); fileLabel_->setVisible(source == "file");
    if (browser && browser_->text().section(QRegularExpression("[+:]"), 0, 0) != source) browser_->setText(source);
}
QJsonObject CookieSettings::options() const {
    const auto source = source_->currentData().toString();
    const auto browser = !source.isEmpty() && source != "file" ? browser_->text().trimmed() : QString();
    const auto file = source == "file" ? file_->text().trimmed() : QString();
    if (!source.isEmpty() && source != "file" && browser.section(QRegularExpression("[+:]"), 0, 0) != source)
        throw std::invalid_argument("Browser options must start with the selected browser");
    if (source == "file" && file.isEmpty()) throw std::invalid_argument("Enter a cookie file path");
    return cookieOptions({{"cookiesFromBrowser", browser}, {"cookiesFile", file}});
}
}
