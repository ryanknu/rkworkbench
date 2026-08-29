#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "mainwindow.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QIcon>
#include <QListWidgetItem>
#include <QUrl>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
std::string configDir() {
    return QDir::homePath().toStdString() + "/.config/rkworkbench";
}
}

namespace {
struct EncodeTemplate { const char* comboText; const char* shortLabel; const char* command; };
const EncodeTemplate kEncodeTemplates[] = {
    {"CPU (libx265)", "CPU", "ffmpeg -i ${in} -map 0 -c:v libx265 -crf 18 -preset slow -pix_fmt yuv420p10le -c:a copy -c:s copy -c:d copy ${out}"},
    {"NVENC (HEVC)", "NVENC HEVC", "ffmpeg -hwaccel cuda -i ${in} -map 0 -c:v hevc_nvenc -preset p7 -rc vbr -cq 18 -pix_fmt p010le -c:a copy -c:s copy -c:d copy ${out}"},
    {"NVENC (AV1)", "NVENC AV1", "ffmpeg -hwaccel cuda -i ${in} -map 0 -c:v av1_nvenc -preset p7 -rc vbr -cq 18 -pix_fmt p010le -c:a copy -c:s copy -c:d copy ${out}"},
};
constexpr int kDefaultEncodeTemplateIndex = 1; // NVENC (HEVC)
}

SettingsDialog::SettingsDialog(MainWindow *parent, int initialCategory)
    : QDialog(parent), ui(new Ui::SettingsDialog), _mainWindow(parent)
{
    ui->setupUi(this);
    setModal(true);

    struct Category { const char* text; const char* icon; };
    const Category categories[] = {
        {"Working Directory", "folder"},
        {"TMDB", "internet-services"},
        {"Emby", "network-server"},
        {"Home Assistant", "network-wired"},
        {"USB Copy", "drive-removable-media"},
        {"Encode", "video-x-generic"},
    };
    for (const auto &cat : categories) {
        auto *item = new QListWidgetItem(cat.text, ui->categoryList);
        QIcon icon = QIcon::fromTheme(cat.icon);
        if (!icon.isNull()) item->setIcon(icon);
    }
    connect(ui->categoryList, &QListWidget::currentRowChanged, ui->pages, &QStackedWidget::setCurrentIndex);
    ui->categoryList->setCurrentRow(initialCategory);
    connect(ui->browseWorkingDirBtn, &QPushButton::clicked, this, &SettingsDialog::_browseWorkingDir);
    connect(ui->haTestBtn, &QPushButton::clicked, this, &SettingsDialog::_testHomeAssistantConnection);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::_onSaveClicked);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(ui->preprocessorTemplates, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0 || index >= (int)std::size(kEncodeTemplates)) return;
        ui->preprocessorCommand->setText(kEncodeTemplates[index].command);
    });

    _testNetworkManager = new QNetworkAccessManager(this);

    _loadCurrentValues();
}

SettingsDialog::~SettingsDialog() {
    delete ui;
}

void SettingsDialog::_loadCurrentValues() {
    _originalWorkingDir = _mainWindow->workingDirPath();
    ui->workingDirEdit->setText(QString::fromStdString(_originalWorkingDir));

    bool tmdbConfigured = QFile::exists(QString::fromStdString(configDir()) + "/tmdb.key") || !_mainWindow->tmdbApiKey().isEmpty();
    ui->tmdbStatusLabel->setText(tmdbConfigured
        ? "A key is already saved. It's not shown here for security - leave this field blank to keep it, or enter a new one to replace it."
        : "No key saved yet. TMDB lookups won't work until one is entered.");
    ui->tmdbApiKeyEdit->clear();

    ui->embyTvLocationEdit->setText(_mainWindow->embyTvLocation());
    ui->embyMovieLocationEdit->setText(_mainWindow->embyMovieLocation());
    ui->rsyncAfterEncodeCheckbox->setChecked(false);
    {
        std::ifstream file(configDir() + "/emby.json");
        if (file.is_open()) {
            std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            try {
                auto j = json::parse(contents);
                ui->rsyncAfterEncodeCheckbox->setChecked(j.value("rsync_after_encode", false));
            } catch (...) {}
        }
    }

    ui->haBaseUrlEdit->setText(_mainWindow->haBaseUrl());
    ui->haTokenEdit->clear();
    ui->haStatusLabel->setText(_mainWindow->isHomeAssistantConfigured()
        ? "A token is already saved. It's not shown here for security - leave this field blank to keep it, or enter a new one to replace it."
        : "Not configured yet - sensor push is disabled until a URL and token are saved.");
    ui->haTestStatusLabel->clear();

    ui->deleteUsbCheckbox->setChecked(false);
    {
        std::ifstream file(configDir() + "/usb.json");
        if (file.is_open()) {
            std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            try {
                auto j = json::parse(contents);
                ui->deleteUsbCheckbox->setChecked(j.value("delete_after_copy", false));
            } catch (...) {}
        }
    }

    ui->preprocessorTemplates->setCurrentIndex(kDefaultEncodeTemplateIndex);
    ui->preprocessorCommand->setText(kEncodeTemplates[kDefaultEncodeTemplateIndex].command);
    {
        std::ifstream file(configDir() + "/encode.json");
        if (file.is_open()) {
            std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            try {
                auto j = json::parse(contents);
                int index = j.value("template_index", kDefaultEncodeTemplateIndex);
                if (index >= 0 && index < (int)std::size(kEncodeTemplates)) {
                    ui->preprocessorTemplates->setCurrentIndex(index);
                }
                auto command = j.value("command", std::string(kEncodeTemplates[kDefaultEncodeTemplateIndex].command));
                ui->preprocessorCommand->setText(QString::fromStdString(command));
            } catch (...) {}
        }
    }
}

QString SettingsDialog::enteredTmdbApiKey() const {
    return ui->tmdbApiKeyEdit->text();
}

QString SettingsDialog::embyTvLocation() const {
    return ui->embyTvLocationEdit->text();
}

QString SettingsDialog::embyMovieLocation() const {
    return ui->embyMovieLocationEdit->text();
}

void SettingsDialog::_browseWorkingDir() {
    auto dir = QFileDialog::getExistingDirectory(this, "Choose Working Directory", ui->workingDirEdit->text());
    if (!dir.isEmpty()) {
        ui->workingDirEdit->setText(dir);
    }
}

// Falls back to whatever token is already on disk when the field is left
// blank, so re-saving the other HA fields (or re-testing) doesn't require
// retyping a secret that hasn't changed.
std::string SettingsDialog::_resolveHaToken() const {
    auto typed = ui->haTokenEdit->text().toStdString();
    if (!typed.empty()) return typed;

    std::ifstream in(configDir() + "/home_assistant.json");
    if (!in.is_open()) return "";
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try {
        auto j = json::parse(contents);
        return j.value("token", "");
    } catch (...) {
        return "";
    }
}

void SettingsDialog::_testHomeAssistantConnection() {
    auto baseUrl = ui->haBaseUrlEdit->text();
    if (baseUrl.endsWith('/')) baseUrl.chop(1);
    auto token = _resolveHaToken();

    if (baseUrl.isEmpty() || token.empty()) {
        ui->haTestStatusLabel->setText("Enter a URL and token first.");
        return;
    }

    ui->haTestBtn->setEnabled(false);
    ui->haTestStatusLabel->setText("Testing...");

    QNetworkRequest request(QUrl(baseUrl + "/api/states/sensor.rkwb_test_connection"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString::fromStdString("Bearer " + token).toUtf8());

    json body;
    body["state"] = "ok";
    body["attributes"]["friendly_name"] = "rkwb Test Connection";

    auto *reply = _testNetworkManager->post(request, QByteArray::fromStdString(body.dump()));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        ui->haTestBtn->setEnabled(true);
        if (reply->error() == QNetworkReply::NoError) {
            ui->haTestStatusLabel->setText("Connected");
        } else {
            ui->haTestStatusLabel->setText("Failed: " + reply->errorString());
        }
        reply->deleteLater();
    });
}

void SettingsDialog::_saveWorkingDirectory() {
    fs::create_directories(configDir());
    std::ofstream out(configDir() + "/wd.txt");
    out << ui->workingDirEdit->text().toStdString();
}

void SettingsDialog::_saveTmdb() {
    auto key = ui->tmdbApiKeyEdit->text().toStdString();
    if (key.empty()) return;
    fs::create_directories(configDir());
    std::ofstream out(configDir() + "/tmdb.key");
    out << key;
}

void SettingsDialog::_saveEmby() {
    json j;
    j["tv_location"] = ui->embyTvLocationEdit->text().toStdString();
    j["movie_location"] = ui->embyMovieLocationEdit->text().toStdString();
    j["rsync_after_encode"] = ui->rsyncAfterEncodeCheckbox->isChecked();
    fs::create_directories(configDir());
    std::ofstream out(configDir() + "/emby.json");
    out << j.dump(2);
}

void SettingsDialog::_saveHomeAssistant() {
    json j;
    j["base_url"] = ui->haBaseUrlEdit->text().toStdString();
    j["token"] = _resolveHaToken();
    fs::create_directories(configDir());
    std::ofstream out(configDir() + "/home_assistant.json");
    out << j.dump(2);
}

void SettingsDialog::_saveUsbCopy() {
    json j;
    j["delete_after_copy"] = ui->deleteUsbCheckbox->isChecked();
    fs::create_directories(configDir());
    std::ofstream out(configDir() + "/usb.json");
    out << j.dump(2);
}

void SettingsDialog::_saveEncode() {
    int index = ui->preprocessorTemplates->currentIndex();
    if (index < 0 || index >= (int)std::size(kEncodeTemplates)) index = kDefaultEncodeTemplateIndex;

    json j;
    j["template_index"] = index;
    j["template_label"] = kEncodeTemplates[index].shortLabel;
    j["command"] = ui->preprocessorCommand->text().toStdString();
    fs::create_directories(configDir());
    std::ofstream out(configDir() + "/encode.json");
    out << j.dump(2);
}

void SettingsDialog::_onSaveClicked() {
    _saveTmdb();
    _saveEmby();
    _saveHomeAssistant();
    _saveUsbCopy();
    _saveEncode();

    auto newDir = ui->workingDirEdit->text().toStdString();
    bool dirChanged = !newDir.empty() && newDir != _originalWorkingDir;
    if (dirChanged) {
        _saveWorkingDirectory();

        QString msg = "The working directory has changed. Restart rkwb now to apply it?";
        if (_mainWindow->hasActiveJobs()) {
            msg += "\n\nWarning: jobs are currently in progress and will be interrupted.";
        }
        auto result = QMessageBox::question(this, "Restart Required", msg, QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::Yes) {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
            accept();
            QCoreApplication::quit();
            return;
        }
    }

    accept();
}
