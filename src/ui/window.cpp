#include "window.h"
#include "cookie_settings.h"
#include "core/cookies.h"
#include "core/theme.h"
#include "core/download.h"
#include "portal_picker.h"
#include <QApplication>
#include <QCheckBox>
#include <QIntValidator>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QPainter>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUrl>
#include <QTimer>
#include <QVBoxLayout>

namespace ytdlp {
namespace {
QLabel *label(const QString &text, const QString &name = {}) {
    auto *widget = new QLabel(text); widget->setObjectName(name); widget->setTextFormat(Qt::PlainText); return widget;
}
QPushButton *button(const QString &text, const QString &name = {}) {
    auto *widget = new QPushButton(text); widget->setObjectName(name); widget->setCursor(Qt::PointingHandCursor); return widget;
}
QWidget *textBody(const QString &text) {
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body);
    auto *copy = label(text); copy->setWordWrap(true); copy->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(copy); return body;
}
void openPath(const QString &path) { if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }
QPixmap brandMark(const Theme &theme, qreal scale) {
    QPixmap mark(qRound(44 * scale), qRound(44 * scale)); mark.setDevicePixelRatio(scale); mark.fill(Qt::transparent);
    QPainter painter(&mark); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen);
    painter.setBrush(theme.color("accent")); painter.drawRoundedRect(QRectF(0, 0, 44, 44), 13, 13);
    painter.setPen(QPen(theme.color("accent_text"), 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(22, 10, 22, 27); painter.drawLine(16, 21, 22, 27); painter.drawLine(28, 21, 22, 27);
    painter.drawLine(13, 32, 31, 32); return mark;
}
}
Window::Window(Paths paths, QWidget *parent, bool connectService) : QMainWindow(parent), paths_(std::move(paths)), client_(paths_, this) {
    setObjectName("ytdlpWindow"); setWindowTitle("Yt-Dlp"); setMinimumSize(660, 680); resize(940, 810);
    buildInterface(); applyTheme();
    connect(&client_, &Client::snapshot, this, &Window::updateSnapshot);
    connect(&client_, &Client::error, this, [this](const QString &error) { message(error, true); });
    connect(&client_, &Client::event, this, [this](const QJsonObject &data) {
        if (data["event"] == "show") { showNormal(); raise(); activateWindow(); url_->setFocus(); }
        if (data["event"] == "error") message(data["error"].toString(), true);
    });
    new QShortcut(QKeySequence("Ctrl+L"), this, [this] { if (!modal_->isVisible()) { url_->setFocus(); url_->selectAll(); } });
    new QShortcut(QKeySequence("Ctrl+Return"), this, [this] { if (!modal_->isVisible()) addDownload(); });
    new QShortcut(QKeySequence("Ctrl+,"), this, [this] { if (!modal_->isVisible()) openSettings(); });
    url_->setFocus(); if (connectService) QTimer::singleShot(0, &client_, &Client::start);
}
void Window::buildInterface() {
    auto *root = new QWidget; setCentralWidget(root);
    auto *rootLayout = new QVBoxLayout(root); rootLayout->setContentsMargins(0, 0, 0, 0);
    content_ = new QWidget(root); rootLayout->addWidget(content_);
    auto *layout = new QVBoxLayout(content_); layout->setContentsMargins(28, 26, 28, 18); layout->setSpacing(18);
    auto *header = new QHBoxLayout; header->setSpacing(14);
    brand_ = new QLabel; brand_->setFixedSize(44, 44); header->addWidget(brand_);
    auto *wordmark = new QVBoxLayout; wordmark->setSpacing(0); wordmark->addWidget(label("Yt-Dlp", "wordmark")); wordmark->addWidget(label("A little less friction. A lot more offline.", "muted"));
    header->addLayout(wordmark); header->addStretch();
    auto *settings = button("Settings", "settingsButton"); connect(settings, &QPushButton::clicked, this, &Window::openSettings); header->addWidget(settings);
    layout->addLayout(header); layout->addWidget(buildComposer()); layout->addWidget(buildQueue(), 1);
    status_ = label("Paste a link to get started.", "status"); status_->setWordWrap(true); status_->setMinimumHeight(26); layout->addWidget(status_);
    auto *footerLayout = new QHBoxLayout; footer_ = label("Native client · yt-dlp", "muted"); footerLayout->addWidget(footer_); footerLayout->addStretch();
    auto *shortcuts = label("Ctrl+L  Link   ·   Ctrl+Enter  Add", "muted"); footerLayout->addWidget(shortcuts); layout->addLayout(footerLayout);
    modal_ = new Modal(root, content_);
}
QWidget *Window::buildComposer() {
    auto *card = new QFrame; card->setObjectName("composerCard"); auto *layout = new QVBoxLayout(card); layout->setContentsMargins(20, 18, 20, 18); layout->setSpacing(12);
    auto *heading = new QHBoxLayout; heading->addWidget(label("Make it yours, offline.", "sectionTitle")); heading->addStretch(); heading->addWidget(label("NEW DOWNLOAD", "eyebrow")); layout->addLayout(heading);
    auto *linkRow = new QHBoxLayout; url_ = new QLineEdit; url_->setObjectName("urlInput"); url_->setPlaceholderText("Paste a video or audio link…"); url_->setClearButtonEnabled(true); linkRow->addWidget(url_, 1);
    connect(url_, &QLineEdit::returnPressed, this, &Window::addDownload);
    auto *paste = button("Paste"); linkRow->addWidget(paste); connect(paste, &QPushButton::clicked, this, [this] { url_->setText(QApplication::clipboard()->text().trimmed()); });
    auto *inspect = button("Info", "inspectButton"); linkRow->addWidget(inspect); connect(inspect, &QPushButton::clicked, this, &Window::inspectLink); layout->addLayout(linkRow);
    auto *options = new QHBoxLayout; options->setSpacing(8);
    video_ = button("Video", "videoMode"); audio_ = button("Audio", "audioMode"); video_->setCheckable(true); audio_->setCheckable(true);
    connect(video_, &QPushButton::clicked, this, [this] { setMode("video"); }); connect(audio_, &QPushButton::clicked, this, [this] { setMode("audio"); });
    options->addWidget(video_); options->addWidget(audio_); options->addSpacing(8);
    quality_ = new QComboBox; quality_->setObjectName("qualityInput"); quality_->setMinimumWidth(140); options->addWidget(quality_);
    videoFormat_ = new QComboBox; videoFormat_->setObjectName("videoFormatInput"); videoFormat_->setAccessibleName("Video format"); videoFormat_->setMinimumWidth(95);
    videoFormat_->setToolTip("Video file format. Auto keeps the selected streams; other formats convert with ffmpeg when needed.");
    for (const auto &format : videoFormats()) videoFormat_->addItem(format == "auto" ? "Auto" : format.toUpper(), format);
    options->addWidget(videoFormat_); options->addStretch();
    auto *add = button("＋  Add to queue", "primaryButton"); connect(add, &QPushButton::clicked, this, &Window::addDownload); options->addWidget(add); layout->addLayout(options);
    auto *folderRow = new QHBoxLayout; folderRow->addWidget(label("Save to", "muted")); destination_ = new QLineEdit; destination_->setObjectName("destinationInput"); folderRow->addWidget(destination_, 1);
    auto *browse = button("Browse", "browseButton"); connect(browse, &QPushButton::clicked, this, &Window::openFolderPicker); folderRow->addWidget(browse);
    auto *folder = button("Open"); connect(folder, &QPushButton::clicked, this, [this] { openPath(expandPath(destination_->text())); }); folderRow->addWidget(folder); layout->addLayout(folderRow);
    setMode("video"); return card;
}
QWidget *Window::buildQueue() {
    auto *widget = new QWidget; auto *layout = new QVBoxLayout(widget); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10);
    auto *header = new QHBoxLayout; header->addWidget(label("Your queue", "sectionTitle")); count_ = label("0 downloads", "muted"); header->addWidget(count_); header->addStretch();
    pause_ = button("Pause queue", "pauseButton"); connect(pause_, &QPushButton::clicked, this, [this] { client_.send(snapshot_["paused"].toBool() ? "resume" : "pause"); }); header->addWidget(pause_);
    auto *clear = button("Clear finished", "clearButton"); connect(clear, &QPushButton::clicked, this, &Window::confirmClear); header->addWidget(clear); layout->addLayout(header);
    queuePages_ = new QStackedWidget;
    auto *empty = new QFrame; empty->setObjectName("emptyCard"); auto *emptyLayout = new QVBoxLayout(empty); emptyLayout->addStretch();
    auto *arrow = label("↓", "emptyArrow"); arrow->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(arrow);
    auto *title = label("Your next favorite, ready to go.", "sectionTitle"); title->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(title);
    emptyText_ = label("Add a link above. Downloads run one at a time\nand keep going when you close this window.", "muted"); emptyText_->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(emptyText_); emptyLayout->addStretch(); queuePages_->addWidget(empty);
    jobs_ = new QListWidget; jobs_->setObjectName("downloadList"); jobs_->setFrameShape(QFrame::NoFrame); jobs_->setSelectionMode(QAbstractItemView::SingleSelection); jobs_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel); jobs_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    delegate_ = new JobDelegate(jobs_); jobs_->setItemDelegate(delegate_); queuePages_->addWidget(jobs_); layout->addWidget(queuePages_, 1);
    jobs_->setMouseTracking(true); jobs_->viewport()->installEventFilter(delegate_);
    connect(delegate_, &JobDelegate::removeRequested, this, [this](const QString &id) {
        client_.send("remove", {{"id", id}}, [this](const QJsonValue &) { message("Removed from your queue. Downloaded files stay in place."); });
    });
    connect(jobs_, &QListWidget::itemSelectionChanged, this, &Window::updateActions); connect(jobs_, &QListWidget::itemDoubleClicked, this, [this] { openDetails(); });
    auto *actions = new QHBoxLayout; details_ = button("Details", "detailsButton"); cancel_ = button("Cancel download", "cancelButton"); retry_ = button("Retry", "retryButton");
    connect(details_, &QPushButton::clicked, this, &Window::openDetails);
    connect(cancel_, &QPushButton::clicked, this, [this] { client_.send("cancel", {{"id", selectedJob()["id"]}}); });
    connect(retry_, &QPushButton::clicked, this, [this] { client_.send("retry", {{"id", selectedJob()["id"]}}); });
    actions->addWidget(details_); actions->addWidget(cancel_); actions->addWidget(retry_); actions->addStretch(); actions->addWidget(label("Double-click a download for details", "muted")); layout->addLayout(actions); updateActions(); return widget;
}
void Window::setMode(const QString &mode) {
    mode_ = mode; video_->setChecked(mode == "video"); audio_->setChecked(mode == "audio"); quality_->clear();
    videoFormat_->setVisible(mode == "video");
    if (mode == "video") {
        for (const auto &height : {"best", "2160", "1440", "1080", "720", "480"}) quality_->addItem(QString(height) == "best" ? "Best available" : QString(height) + "p", height);
    } else {
        quality_->addItem("Keep original", "keep"); for (const auto &format : {"mp3", "m4a", "opus", "flac", "wav"}) quality_->addItem(QString(format).toUpper(), format);
    }
}
void Window::addDownload() {
    if (!initialized_) { message("Connecting to the download service…"); return; }
    QJsonObject options{{"url", url_->text().trimmed()}, {"mode", mode_}, {"dest", destination_->text().trimmed()},
        {mode_ == "video" ? "quality" : "audioFormat", quality_->currentData().toString()}};
    if (mode_ == "video") options["videoFormat"] = videoFormat_->currentData().toString();
    client_.send("add", options, [this, options](const QJsonValue &) {
        url_->clear(); message("Added to your queue."); auto defaults = options; defaults.remove("url"); client_.send("settings", defaults);
    });
}
void Window::updateSnapshot(const QJsonObject &data) {
    snapshot_ = data;
    if (data["theme"].isObject()) {
        try { const auto next = parseTheme(data["theme"].toObject()); if (next != theme_) { theme_ = next; applyTheme(); } } catch (const std::exception &error) { message(error.what(), true); }
    }
    if (!initialized_) {
        const auto defaults = data["settings"].toObject(); setMode(defaults["mode"].toString("video")); destination_->setText(defaults["dest"].toString());
        const auto value = defaults[mode_ == "video" ? "quality" : "audioFormat"].toString(); int index = quality_->findData(value);
        if (index < 0 && mode_ == "video") { quality_->addItem(value + "p", value); index = quality_->count() - 1; }
        quality_->setCurrentIndex(std::max(0, index));
        videoFormat_->setCurrentIndex(std::max(0, videoFormat_->findData(defaults["videoFormat"].toString("auto")))); initialized_ = true;
        const auto clipboard = QApplication::clipboard()->text().trimmed(); if (url_->text().isEmpty() && isWebUrl(clipboard)) url_->setText(clipboard);
    }
    const auto jobs = data["jobs"].toArray(); const auto selected = selectedJob()["id"].toString();
    delegate_->activeId = data["activeId"].toString(); int selectedRow = -1;
    QSignalBlocker block(jobs_);
    while (jobs_->count() > jobs.size()) delete jobs_->takeItem(jobs_->count() - 1);
    for (int i = 0; i < jobs.size(); ++i) {
        auto *item = i < jobs_->count() ? jobs_->item(i) : new QListWidgetItem(jobs_);
        const auto value = jobs[i].toObject(); item->setData(Qt::UserRole, value); item->setText(value["title"].toString().isEmpty() ? value["url"].toString() : value["title"].toString());
        item->setToolTip(value["url"].toString()); if (value["id"] == selected) selectedRow = i;
    }
    jobs_->setCurrentRow(selectedRow);
    count_->setText(QString("%1 download%2").arg(jobs.size()).arg(jobs.size() == 1 ? "" : "s"));
    queuePages_->setCurrentIndex(jobs.isEmpty() ? 0 : 1); pause_->setText(data["paused"].toBool() ? "Resume queue" : "Pause queue"); updateActions(); jobs_->viewport()->update();
    footer_->setText("Native client · " + data["settings"].toObject()["binary"].toString() + " · " + theme_.name);
    if (!data["themeError"].toString().isEmpty()) message("Theme: " + data["themeError"].toString(), true);
}
QJsonObject Window::selectedJob() const { return jobs_ && jobs_->currentItem() ? jobs_->currentItem()->data(Qt::UserRole).toJsonObject() : QJsonObject(); }
void Window::updateActions() {
    const auto job = selectedJob(); const auto status = job["status"].toString();
    details_->setEnabled(!job.isEmpty()); cancel_->setEnabled(status == "queued" || status == "running"); retry_->setEnabled(status == "failed" || status == "cancelled" || status == "done");
}
void Window::message(const QString &text, bool error) {
    status_->setText(text); status_->setProperty("error", error);
    status_->setStyleSheet("color: " + theme_.hex(error ? "danger" : "muted") + ";");
    if (error && modal_->isVisible()) modal_->showError(text);
}
void Window::applyTheme() {
    const auto c = [this](const QString &key) { return theme_.hex(key); };
    setStyleSheet(QString(R"(
        QMainWindow, QWidget { background: %1; color: %4; font-family: "Inter", "Noto Sans", sans-serif; font-size: 13px; }
        QFrame#composerCard, QFrame#emptyCard, QFrame#modalCard { background: %2; border: 1px solid %8; border-radius: 16px; }
        QLabel { background: transparent; border: none; }
        QLabel#wordmark { font-size: 26px; font-weight: 700; }
        QLabel#sectionTitle { font-size: 18px; font-weight: 600; }
        QLabel#eyebrow { color: %5; font-size: 10px; font-weight: 600; letter-spacing: 1px; }
        QLabel#muted, QLabel#status { color: %5; }
        QLabel#modalError { color: %9; }
        QLabel#emptyArrow { font-size: 44px; color: %6; }
        QPushButton { background: %2; border: 1px solid %8; border-radius: 8px; padding: 8px 12px; min-height: 18px; }
        QPushButton:hover { background: %3; border-color: %5; }
        QPushButton:focus, QLineEdit:focus, QComboBox:focus { border: 1px solid %6; }
        QPushButton:checked, QPushButton#primaryButton { background: %6; color: %7; border-color: %6; font-weight: 600; }
        QPushButton:disabled { color: %5; }
        QPushButton#primaryButton:disabled { background: %3; color: %5; border-color: %8; }
        QLineEdit, QComboBox, QSpinBox { background: %1; border: 1px solid %8; border-radius: 8px; padding: 8px 10px; min-height: 18px; selection-background-color: %6; selection-color: %7; }
        QCheckBox::indicator { width: 18px; height: 18px; background: %1; border: 1px solid %8; border-radius: 6px; }
        QCheckBox::indicator:checked { background: %6; border-color: %6; }
        QCheckBox::indicator:focus { border-color: %6; }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox QAbstractItemView { background: %2; color: %4; selection-background-color: %6; selection-color: %7; border: 1px solid %8; }
        QListWidget, QScrollArea, QPlainTextEdit { border: none; background: transparent; }
        QPlainTextEdit { background: %1; border-radius: 8px; padding: 10px; selection-background-color: %6; selection-color: %7; }
        QWidget#modalBackdrop { background: transparent; }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
        QScrollBar::handle:vertical { background: %8; min-height: 28px; border-radius: 4px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )").arg(c("background"), c("surface"), c("surface_alt"), c("text"), c("muted"), c("accent"), c("accent_text"), c("border"))
        .arg(c("danger")));
    auto backdrop = theme_.color("background"); backdrop.setAlpha(210); modal_->setBackdrop(backdrop);
    status_->setStyleSheet("color: " + c(status_->property("error").toBool() ? "danger" : "muted") + ";");
    brand_->setPixmap(brandMark(theme_, devicePixelRatioF())); delegate_->theme = theme_; jobs_->viewport()->update();
    setWindowIcon(QIcon(brandMark(theme_, 2)));
}
void Window::openSettings() {
    if (!initialized_) return;
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body); auto *form = new QFormLayout;
    auto *binary = new QLineEdit(snapshot_["settings"].toObject()["binary"].toString()); binary->setObjectName("binaryInput");
    auto *history = new QLineEdit(QString::number(snapshot_["settings"].toObject()["maxHistory"].toInt())); history->setObjectName("historyInput");
    history->setValidator(new QIntValidator(1, 99999, history));
    form->addRow("yt-dlp executable", binary); form->addRow("Finished history limit", history); layout->addLayout(form);
    auto *copy = label("Choose a yt-dlp executable on your PATH or an absolute path. Audio conversion and video merging require ffmpeg."); copy->setWordWrap(true); layout->addWidget(copy);
    auto *cookies = new CookieSettings(snapshot_["settings"].toObject()); layout->addWidget(cookies);
    auto *picker = new QCheckBox; picker->setObjectName("nativePickerInput"); picker->setAccessibleName("Use native file picker");
    picker->setChecked(snapshot_["settings"].toObject()["useNativePicker"].toBool(true));
    auto *pickerRow = new QFormLayout; pickerRow->addRow("Use native file picker", picker); layout->addLayout(pickerRow);
    auto *pickerHint = label("On: choose folders in this window. Off: use your operating system's folder dialog.", "muted"); pickerHint->setWordWrap(true); layout->addWidget(pickerHint); layout->addStretch();
    auto *save = button("Save settings", "primaryButton"); connect(save, &QPushButton::clicked, this, [this, binary, history, cookies, picker] {
        try {
            auto settings = cookies->options(); settings["binary"] = binary->text().trimmed(); settings["maxHistory"] = history->text().toInt(); settings["useNativePicker"] = picker->isChecked();
            client_.send("settings", settings, [this](const QJsonValue &) { modal_->dismiss(); message("Settings saved. Retry the failed download to use the new cookie settings."); });
        } catch (const std::exception &error) { message(error.what(), true); }
    }); modal_->open("Settings", body, save);
}
void Window::openDetails() {
    const auto job = selectedJob(); if (job.isEmpty()) return;
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body);
    auto *summary = label((job["title"].toString().isEmpty() ? job["url"].toString() : job["title"].toString()) + "\n\nStatus: " + job["status"].toString() + "\nLink: " + job["url"].toString() + "\nSaved to: " + job["dest"].toString() + "\nJob: " + job["id"].toString());
    summary->setWordWrap(true); summary->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(summary);
    auto *log = new QPlainTextEdit; log->setReadOnly(true); log->setPlainText(job["error"].toString().isEmpty() ? job["log"].toString() : job["error"].toString()); log->setMinimumHeight(120); layout->addWidget(log, 1);
    auto *row = new QHBoxLayout; auto *folder = button("Open folder"); auto *file = button("Open file", "primaryButton"); file->setEnabled(!job["file"].toString().isEmpty() && job["status"] == "done");
    if (!authenticationHint(job["error"].toString()).isEmpty()) {
        auto *cookies = button("Configure cookies"); cookies->setObjectName("configureCookiesButton");
        connect(cookies, &QPushButton::clicked, this, &Window::openSettings); row->addWidget(cookies);
    }
    connect(folder, &QPushButton::clicked, this, [job] { openPath(job["dest"].toString()); }); connect(file, &QPushButton::clicked, this, [job] { openPath(job["file"].toString()); }); row->addWidget(folder); row->addWidget(file); layout->addLayout(row); modal_->open("Download details", body);
}
void Window::confirmClear() {
    auto *body = textBody("Clear finished downloads from the queue? Downloaded files stay in their folders. Waiting and running downloads stay in the queue.");
    body->layout()->setContentsMargins(0, 0, 0, 0);
    auto *clear = button("Clear finished history", "primaryButton");
    connect(clear, &QPushButton::clicked, this, [this] { client_.send("clear", {{"confirmed", true}}, [this](const QJsonValue &) { modal_->dismiss(); }); }); modal_->open("Clear finished", body, clear, Modal::Sizing::Content);
}
void Window::openFolderPicker() {
    if (!snapshot_["settings"].toObject()["useNativePicker"].toBool(true)) {
        const auto start = expandPath(destination_->text().isEmpty() ? QDir::homePath() : destination_->text());
        chooseFolderWithPortal(this, "Choose a folder", [this](const QString &directory) { if (!directory.isEmpty()) destination_->setText(directory); },
            [this, start] { const auto directory = QFileDialog::getExistingDirectory(this, "Choose a folder", start); if (!directory.isEmpty()) destination_->setText(directory); });
        return;
    }
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body); auto *row = new QHBoxLayout;
    auto *path = new QLineEdit(expandPath(destination_->text().isEmpty() ? QDir::homePath() : destination_->text())); path->setObjectName("folderPath"); auto *up = button("Up"); row->addWidget(path, 1); row->addWidget(up); layout->addLayout(row);
    auto *directories = new QListWidget; directories->setObjectName("folderList"); layout->addWidget(directories, 1);
    auto refresh = [path, directories] {
        directories->clear(); const QDir directory(expandPath(path->text()));
        for (const auto &name : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) { auto *item = new QListWidgetItem(name, directories); item->setData(Qt::UserRole, directory.absoluteFilePath(name)); }
    };
    connect(path, &QLineEdit::returnPressed, body, refresh);
    connect(up, &QPushButton::clicked, body, [path, refresh] { QDir directory(expandPath(path->text())); directory.cdUp(); path->setText(directory.absolutePath()); refresh(); });
    connect(directories, &QListWidget::itemDoubleClicked, body, [path, refresh](QListWidgetItem *item) { path->setText(item->data(Qt::UserRole).toString()); refresh(); });
    auto *choose = button("Use this folder", "primaryButton"); layout->addWidget(choose);
    connect(choose, &QPushButton::clicked, body, [this, path] { const auto directory = expandPath(path->text()); if (!QFileInfo(directory).isDir()) { message("Choose an existing folder, or type a new path in Save to.", true); return; } destination_->setText(directory); modal_->dismiss(); });
    refresh(); modal_->open("Choose a folder", body);
}
void Window::inspectLink() {
    const auto url = url_->text().trimmed(); message("Reading media information…");
    client_.send("info", {{"url", url}}, [this](const QJsonValue &data) { openMetadata(data.toObject()); message("Media information is ready."); });
}
void Window::openMetadata(const QJsonObject &data) {
    auto *body = textBody(data["title"].toString() + "\n\nBy " + data["uploader"].toString() + "\nDuration: " + durationText(data["duration"].toDouble()) + "\nSource: " + data["extractor_key"].toString());
    auto *formats = new QPlainTextEdit; formats->setReadOnly(true); QString text;
    for (const auto &value : data["formats"].toArray()) { const auto format = value.toObject(); text += format["format_id"].toString() + "  ·  " + format["ext"].toString() + "  ·  " + format["resolution"].toString() + "  ·  " + format["format_note"].toString() + '\n'; }
    formats->setPlainText(text); body->layout()->addWidget(formats); modal_->open("Media information", body);
}
void Window::resizeEvent(QResizeEvent *event) { QMainWindow::resizeEvent(event); if (modal_) modal_->setGeometry(centralWidget()->rect()); }
void Window::closeEvent(QCloseEvent *event) {
    try { writeJson(paths_.config + "/window.json", {{"width", width()}, {"height", height()}}); } catch (...) {}
    QMainWindow::closeEvent(event);
}
}
