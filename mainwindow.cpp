#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "settingsdialog.h"
#include <QDebug>
#include <QMenu>
#include <QAction>
#include <QAudioOutput>
#include <QProcess>
#include <QInputDialog>
#include <QFileDialog>
#include <QPixmap>
#include <QSettings>
#include <QDateTime>
#include <fstream>
#include <string>
#include <format>
#include <optional>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <QMouseEvent>
#include <QMediaMetaData>
#include <QMediaFormat>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDir>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

QString q(const std::string& str)
{
    return QString::fromStdString(str);
}


void setMouseTrackingRecursive(QWidget *parent, bool enable) {
    if (!parent) return;

    // Set for the parent widget
    parent->setMouseTracking(enable);

    // Recursively set for all children
    QList<QWidget *> children = parent->findChildren<QWidget *>();
    for (QWidget *child : children) {
        child->setMouseTracking(enable);
    }
}

void MainWindow::_addTreeItem(std::string treeName, std::string id, std::string parentId, std::string parentText, std::string text, std::string color, std::string after) {
    auto tree = ui->showsTree;
    if (treeName == "Files") {
        tree = ui->disksTree;
    } else if (treeName == "Films") {
        tree = ui->filmsTree;
    }

    // Find or create parent
    auto* model = dynamic_cast<QStandardItemModel *>(tree->model());
    auto items = model->findItems(parentText.data());
    QStandardItem* parent;
    if (items.empty()) {
        parent = new QStandardItem(parentText.data());
        parent->setSelectable(true);
        if (!parentId.empty()) {
            parent->setData(q(parentId.data()), Qt::UserRole);
        }
        model->invisibleRootItem()
            ->appendRow(parent);
    } else {
        parent = items.at(0);
        if (!parentId.empty() && parent->data(Qt::UserRole).toString().isEmpty()) {
             parent->setData(q(parentId.data()), Qt::UserRole);
             parent->setSelectable(true);
        }
    }

    // Make item
    auto item = new QStandardItem(text.data());
    item->setData(q(id.data()), Qt::UserRole);
    item->setData(q(text.data()), Qt::UserRole + 2);
    if (color != "Default") {
        item->setForeground(QBrush(QColor(color.c_str())));
    }

    parent->appendRow(item);
    tree->expandAll();
}

void MainWindow::_removeTreeItemById(std::string treeName, std::string id) {
    auto tree = ui->showsTree;
    if (treeName == "Files") {
        tree = ui->disksTree;
    } else if (treeName == "Films") {
        tree = ui->filmsTree;
    }

    auto* model = dynamic_cast<QStandardItemModel *>(tree->model());
    auto items = model->match(model->index(0, 0), Qt::UserRole, q(id), 1, Qt::MatchExactly | Qt::MatchRecursive);

    if (!items.empty() && items.at(0).isValid()) {
        auto index = items.at(0);
        auto parentIndex = index.parent();
        if (parentIndex.isValid()) {
            auto* parentItem = model->itemFromIndex(parentIndex);
            parentItem->removeRow(index.row());
            if (parentItem->rowCount() == 0) {
                model->removeRow(parentItem->row());
            }
        } else {
            model->removeRow(index.row());
        }
    }
}

void MainWindow::_changeTreeItemColor(std::string treeName, std::string id, std::string color) {
    auto tree = ui->showsTree;
    if (treeName == "Files") {
        tree = ui->disksTree;
    } else if (treeName == "Films") {
        tree = ui->filmsTree;
    }

    auto* model = dynamic_cast<QStandardItemModel *>(tree->model());
    auto items = model->match(model->index(0, 0), Qt::UserRole, q(id), 1, Qt::MatchExactly | Qt::MatchRecursive);

    if (!items.empty() && items.at(0).isValid()) {
        auto item = model->itemFromIndex(items.at(0));
        if (color == "Default") {
            item->setData(QVariant(), Qt::ForegroundRole);
        } else {
            item->setForeground(QBrush(QColor(color.c_str())));
        }
    }
}

void MainWindow::_changeTreeItemText(std::string treeName, std::string id, std::string text) {
    auto tree = ui->showsTree;
    if (treeName == "Files") {
        tree = ui->disksTree;
    } else if (treeName == "Films") {
        tree = ui->filmsTree;
    }

    auto* model = dynamic_cast<QStandardItemModel *>(tree->model());
    auto items = model->match(model->index(0, 0), Qt::UserRole, q(id), 1, Qt::MatchExactly | Qt::MatchRecursive);

    if (!items.empty() && items.at(0).isValid()) {
        auto item = model->itemFromIndex(items.at(0));
        item->setText(q(text.c_str()));
    }
}

void MainWindow::_selectTreeItem(std::string treeName, std::string id) {
    auto tree = ui->showsTree;
    if (treeName == "Files") {
        tree = ui->disksTree;
    } else if (treeName == "Films") {
        tree = ui->filmsTree;
    }

    auto* model = dynamic_cast<QStandardItemModel *>(tree->model());
    auto items = model->match(model->index(0, 0), Qt::UserRole, q(id), 1, Qt::MatchExactly | Qt::MatchRecursive);

    if (!items.empty() && items.at(0).isValid()) {
        auto index = items.at(0);
        tree->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        tree->setCurrentIndex(index);
        tree->scrollTo(index);
        if (index.parent().isValid()) {
            tree->setExpanded(index.parent(), true);
        }
    }
}

void MainWindow::_changeGarbageSize(std::uint64_t size) {
    double gb = static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0);
    ui->gcBtn->setText(q(std::format("Collect\nGarbage\n({:.1f}G)", gb)));
    ui->gcBtn->setDisabled(size < 1);
}

void MainWindow::_clearTrees() {
    auto* disksModel = dynamic_cast<QStandardItemModel *>(ui->disksTree->model());
    disksModel->removeRows(0, disksModel->rowCount());
    auto* showsModel = dynamic_cast<QStandardItemModel *>(ui->showsTree->model());
    showsModel->removeRows(0, showsModel->rowCount());
    auto* filmsModel = dynamic_cast<QStandardItemModel *>(ui->filmsTree->model());
    filmsModel->removeRows(0, filmsModel->rowCount());

    _clearMetadataPanel();
}

/**
 * Retrieves the ID of the selected item in the tree.
 * Assumes the ID is set as the UserRole data on item in the data model.
 */
std::string MainWindow::_getIdForSelectedItemInTree(QTreeView *&tree)
{
    QModelIndex index = tree->currentIndex();
    if (!index.isValid()) {
        return "";
    }
    QVariant data = index.model()->data(index, Qt::UserRole);
    return data.toString().toStdString();
}

void MainWindow::setAppModel(AppModel *theModel, std::string mediaDir) {
    appModel = theModel;
    _mediaDir = mediaDir;

    // Spin up background thread (2)
    start_rust_processing(this, mediaDir.c_str(), callback_wrapper);

    // Initial population of UI from `theModel`.
    ui->tmdbModeBtn->setText(q(appModel->tmdbMode()));

    // Initialize tree models
    auto* disksModel = new QStandardItemModel(this);
    auto* showsModel = new QStandardItemModel(this);
    auto* filmsModel = new QStandardItemModel(this);
    disksModel->setHorizontalHeaderLabels({ "Disks" });
    showsModel->setHorizontalHeaderLabels({ "Shows" });
    filmsModel->setHorizontalHeaderLabels({ "Films" });
    ui->disksTree->setModel(disksModel);
    ui->showsTree->setModel(showsModel);
    ui->filmsTree->setModel(filmsModel);

    connect(ui->stitchBtn, &QPushButton::clicked, this, [this]() {
        perform_stitch();
        this->ffmpegQueueCount++;
        this->_updateFfmpegStatus();
    });

    connect(ui->stitchList->model(), &QAbstractItemModel::rowsMoved, this, [this](const QModelIndex &, int start, int end, const QModelIndex &, int destination) {
        int from = start;
        int to = destination;
        if (to > from) to--; // Adjust for internal move
        reorder_stitch(from, to);
    });

    ui->stitchList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->stitchList, &QWidget::customContextMenuRequested, [this](const QPoint &pos) {
        auto index = ui->stitchList->indexAt(pos);
        QMenu menu;
        if (index.isValid()) {
            auto removeAction = menu.addAction("Remove");
            connect(removeAction, &QAction::triggered, [index]() {
                remove_from_stitch(index.row());
            });
        }
        auto clearAction = menu.addAction("Clear All");
        connect(clearAction, &QAction::triggered, []() {
            clear_stitch();
        });
        menu.exec(ui->stitchList->viewport()->mapToGlobal(pos));
    });

    ui->disksTree->setRootIsDecorated(false);
    ui->disksTree->setItemsExpandable(false);

    auto handleTsSeek = [&](const QModelIndex &index) {
        _mRequestedPlayerPosition = 0;
        if (!index.isValid()) return;
        auto text = index.data(Qt::DisplayRole).toString().toStdString();
        size_t tsPos = text.find("ts=");
        if (tsPos != std::string::npos) {
            size_t endPos = text.find("]", tsPos);
            if (endPos == std::string::npos) endPos = text.length();
            std::string tsStr = text.substr(tsPos + 3, endPos - (tsPos + 3));
            try {
                _mRequestedPlayerPosition = std::stoll(tsStr);
            } catch (...) {}
        }
    };

    // When selecting an entry on the disks tree, load item in player.
    connect(ui->disksTree->selectionModel(), &QItemSelectionModel::selectionChanged, [this, handleTsSeek](const QItemSelection &selected, const QItemSelection &) {
        if (!selected.indexes().isEmpty()) {
            auto index = selected.indexes().first();
            handleTsSeek(index);
            _mSeekPending = true;
            auto titleId = index.data(Qt::UserRole).toString().toStdString();
            auto fileName = get_filename_for_title_id(titleId.c_str());
            if (!fileName) return;
            std::string path(fileName);
            free_string(fileName);

            _clearMetadataPanel();
            fetch_mkv_info(path.c_str());
            ui->metadataTitle->setText(q(path));

            _loadInPlayer(q(path));
        } else {
            _mRequestedPlayerPosition = 0;
            _mSeekPending = true;
        }
    });

    connect(ui->showsTree->selectionModel(), &QItemSelectionModel::selectionChanged, [this, handleTsSeek](const QItemSelection &selected, const QItemSelection &) {
        if (!selected.indexes().isEmpty()) {
            auto index = selected.indexes().first();
            handleTsSeek(index);
            auto id = index.data(Qt::UserRole).toString().toStdString();
            _clearMetadataPanel();
            if (!id.empty()) {
                fetch_tmdb_still(id.c_str(), true);
                auto fileName = get_filename_for_tv_episode_id(id.c_str());
                if (fileName) {
                    _mSeekPending = true;
                    fetch_mkv_info(fileName);
                    _loadInPlayer(q(fileName));
                    free_string(fileName);
                } else {
                    player->setPosition(_mRequestedPlayerPosition);
                }
                if (has_portable_for_tv_episode(id.c_str())) {
                    ui->metadataPortable->setText("Portable version: Yes");
                    ui->metadataPortable->setStyleSheet("color: green; font-weight: bold;");
                } else {
                    ui->metadataPortable->setText("Portable version: No");
                    ui->metadataPortable->setStyleSheet("");
                }
            }
        } else {
            _mRequestedPlayerPosition = 0;
            _mSeekPending = true;
            _clearMetadataPanel();
        }
    });

    connect(ui->filmsTree->selectionModel(), &QItemSelectionModel::selectionChanged, [this, handleTsSeek](const QItemSelection &selected, const QItemSelection &) {
        if (!selected.indexes().isEmpty()) {
            auto index = selected.indexes().first();
            handleTsSeek(index);
            auto id = index.data(Qt::UserRole).toString().toStdString();
            _clearMetadataPanel();
            if (!id.empty()) {
                fetch_tmdb_still(id.c_str(), false);
                auto fileName = get_filename_for_film_video_id(id.c_str());
                if (fileName) {
                    _mSeekPending = true;
                    fetch_mkv_info(fileName);
                    _loadInPlayer(q(fileName));
                    free_string(fileName);
                } else {
                    player->setPosition(_mRequestedPlayerPosition);
                }
                if (has_portable_for_film_video(id.c_str())) {
                    ui->metadataPortable->setText("Portable version: Yes");
                    ui->metadataPortable->setStyleSheet("color: green; font-weight: bold;");
                } else {
                    ui->metadataPortable->setText("Portable version: No");
                    ui->metadataPortable->setStyleSheet("");
                }
            }
        } else {
            _mRequestedPlayerPosition = 0;
            _mSeekPending = true;
            _clearMetadataPanel();
        }
    });
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->ffmpegStatusWidget->setMinimumHeight(30);
    ui->rsyncStatusWidget->setMinimumHeight(30);
    ui->copyStatusWidget->setMinimumHeight(30);
    ui->stitchGroup->hide();
    _loadEmbySettings();
    _loadUsbCopySettings();
    _loadEncodeSettings();
    _updateEncodeBtnText();
    ui->ffmpegStatusWidget->hide();
    ui->rsyncStatusWidget->hide();
    ui->copyStatusWidget->hide();
    ui->tracksGroup->hide();
    setMouseTrackingRecursive(this, true);

    ui->filmsTree->hide();
    _findVlc();

    // Spinner timer
    spinnerTimer = new QTimer(this);
    connect(spinnerTimer, &QTimer::timeout, this, [&]() {
        const QString frames[] = {"|", "/", "-", "\\"};
        spinnerIndex = (spinnerIndex + 1) % 4;
        ui->spinnerLabel->setText(frames[spinnerIndex]);
        ui->rsyncSpinnerLabel->setText(frames[spinnerIndex]);
        ui->copySpinnerLabel->setText(frames[spinnerIndex]);
    });

    // USB device detection timer
    usbStatusTimer = new QTimer(this);
    connect(usbStatusTimer, &QTimer::timeout, this, [&]() {
        _updateUsbStatus();
    });
    usbStatusTimer->start(2000);
    _updateUsbStatus();

    // Home Assistant sensor push (no-op if unconfigured)
    haNetworkManager = new QNetworkAccessManager(this);
    reinitHomeAssistant();

    // Settings dialog
    auto openSettings = [this](int initialCategory) {
        SettingsDialog dlg(this, initialCategory);
        dlg.exec();

        // Reload unconditionally - harmless if nothing changed, and picks up
        // whatever was saved (or a freshly-typed, not-yet-restarted key/value).
        _loadEmbySettings();
        _loadUsbCopySettings();
        _loadEncodeSettings();
        _updateEncodeBtnText();
        reinitHomeAssistant();
        auto key = dlg.enteredTmdbApiKey();
        if (!key.isEmpty()) _tmdbApiKey = key;
    };

    connect(ui->settingsBtn, &QPushButton::clicked, this, [openSettings]() {
        openSettings(SettingsDialog::WorkingDirectoryCategory);
    });

    connect(ui->encodeSettingsBtn, &QPushButton::clicked, this, [openSettings]() {
        openSettings(SettingsDialog::EncodeCategory);
    });

    // Connect media player
    audioOutput = new QAudioOutput;
    player = new QMediaPlayer;
    player->setVideoOutput(ui->videoWidget);
    player->setAudioOutput(audioOutput);
    connect(player, &QMediaPlayer::durationChanged, [&](qint64 v) {
        ui->videoSeek->setMaximum(v);
        if (_mSeekPending && v > 0) {
            _mSeekPending = false;
            qint64 requested = _mRequestedPlayerPosition;
            if (v > 15000 && requested > v - 10000) {
                requested = v - 10000;
            }
            player->setPosition(requested);
        }
    });
    connect(player, &QMediaPlayer::positionChanged, [&](qint64 v) {
        ui->videoSeek->setValue(v);
        ui->seekPos->setText(q(std::format("{}", v)));
    });
    connect(ui->videoSeek, &QSlider::sliderMoved, [&](int v) {
        player->setPosition(v);
        ui->seekPos->setText(q(std::format("{}", (qint64)v)));
    });
    connect(ui->seekFwd, &QPushButton::clicked, [&] {
        auto pos = player->position();
        player->setPosition(pos + 42);
    });
    connect(ui->seekRev, &QPushButton::clicked, [&] {
        auto pos = player->position();
        player->setPosition(pos - 42);
    });

    // Media player, when the content loads, skip to requested position.
    connect(player, &QMediaPlayer::mediaStatusChanged, [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
            _selectSeekableAudioTrack();
            if (_mSeekPending) {
                auto maximum = player->duration();
                if (maximum > 0) {
                    _mSeekPending = false;
                    auto requested = _mRequestedPlayerPosition;
                    if (maximum > 15000 && requested > maximum - 10000) {
                        requested = maximum - 10000;
                    }
                    player->setPosition(requested);
                }
            }
        }
    });

    // Set context menu on titles
    ui->disksTree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->disksTree, &QWidget::customContextMenuRequested, [&](const QPoint &pos) {
        qDebug() << "disksTree customContextMenuRequested at" << pos;
        // TODO: See if this works with the selectedItem helper fn
        auto index = ui->disksTree->indexAt(pos);
        if (!index.isValid()) {
            qDebug() << "index is not valid";
            return;
        }
        ui->disksTree->setCurrentIndex(index);

        QMenu menu;
        auto titleId = index.data(Qt::UserRole).toString().toStdString();
        auto fileName = get_filename_for_title_id(titleId.c_str());
        if (fileName) {
            std::string path(fileName);
            free_string(fileName);

            QAction * loadAction = menu.addAction(q("Load in Player"));
            connect(loadAction, &QAction::triggered, [this, path]() {
                _loadInPlayer(QString::fromStdString(path));
            });

            if (_vlcFound) {
                QAction * vlcAction = menu.addAction(q("Open in VLC"));
                connect(vlcAction, &QAction::triggered, [this, path]() {
                    QStringList args = _vlcArgs;
                    args.append(QString::fromStdString(path));
                    if (_vlcProgram == "flatpak") {
                        args.append("@@");
                    }
                    qDebug() << "Launching VLC:" << _vlcProgram << args;
                    if (!QProcess::startDetached(_vlcProgram, args)) {
                        qDebug() << "Failed to start VLC process";
                    }
                });
            }

            QAction * addToStitchAction = menu.addAction(q("Add to Stitch"));
            connect(addToStitchAction, &QAction::triggered, [path]() {
                add_to_stitch(path.c_str());
            });
        }

        QAction * deleteAction = menu.addAction(q("Delete Title"));
        QAction * unDeleteAction = menu.addAction(q("Undelete Title"));
        QAction * matchScanAction = nullptr;
        if (index.parent().isValid()) {
            matchScanAction = menu.addAction(q("Match Scan"));
        }

        connect(deleteAction, &QAction::triggered, [&]() {
            // Stop the media player, if we remove the file it's accessing we'll segfault.
            player->stop();
            player->setSource(QUrl());

            auto titleId = _getIdForSelectedItemInTree(ui->disksTree);
            delete_title(titleId.c_str());
        });

        connect(unDeleteAction, &QAction::triggered, [&]() {
            auto titleId = _getIdForSelectedItemInTree(ui->disksTree);
            undelete_title(titleId.c_str());
        });

        if (matchScanAction) {
            connect(matchScanAction, &QAction::triggered, [&]() {
                auto titleId = _getIdForSelectedItemInTree(ui->disksTree);
                auto command = _encodeCommand.toStdString();
                match_scan(titleId.c_str(), command.c_str());
            });
        }

        menu.exec(ui->disksTree->viewport()->mapToGlobal(pos));
    });

    // Set context menu on shows
    ui->showsTree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->showsTree, &QWidget::customContextMenuRequested, [&](const QPoint &pos) {
        qDebug() << "showsTree customContextMenuRequested at" << pos;
        auto index = ui->showsTree->indexAt(pos);
        if (!index.isValid()) {
            qDebug() << "index is not valid";
            return;
        }
        ui->showsTree->setCurrentIndex(index);

        auto id = index.model()->data(index, Qt::UserRole).toString().toStdString();
        qDebug() << "id from UserRole:" << q(id);

        std::string showId;
        std::string episodeId;

        if (index.parent().isValid()) {
            episodeId = id;
            showId = index.parent().data(Qt::UserRole).toString().toStdString();
        } else {
            showId = id;
        }

        if (showId.empty()) return;
        qDebug() << "showId:" << q(showId) << "episodeId:" << q(episodeId);

        QMenu menu;

        QBrush brush = index.data(Qt::ForegroundRole).value<QBrush>();
        if (brush.color() == QColor("green") || brush.color() == QColor("cyan")) {
            auto fileName = get_filename_for_tv_episode_id(id.c_str());
            if (fileName) {
                std::string path(fileName);
                free_string(fileName);

                QAction * loadAction = menu.addAction(q("Load in Player"));
                connect(loadAction, &QAction::triggered, [this, path]() {
                    _loadInPlayer(QString::fromStdString(path));
                });

                if (_vlcFound) {
                    QAction * vlcAction = menu.addAction(q("Open in VLC"));
                    connect(vlcAction, &QAction::triggered, [this, path]() {
                        QStringList args = _vlcArgs;
                        args.append(QString::fromStdString(path));
                        if (_vlcProgram == "flatpak") {
                            args.append("@@");
                        }
                        qDebug() << "Launching VLC:" << _vlcProgram << args;
                        if (!QProcess::startDetached(_vlcProgram, args)) {
                            qDebug() << "Failed to start VLC process";
                        }
                    });
                }
            }
        }

        QAction * uploadAction = menu.addAction(q("Upload Show (rsync)"));
        QAction * loadFromNasAction = menu.addAction(q("Load from NAS"));
        QAction * reencodeShowAction = menu.addAction(q("Re-encode Show (ffmpeg)"));
        QAction * createPortableAction = menu.addAction(q("Create Portable Version"));
        QAction * confirmShowAction = menu.addAction(q("Confirm Plays (Show)"));

        if (episodeId.empty()) {
            auto deleteMenu = menu.addMenu(q("Remove Metadata"));
            auto deleteShow = deleteMenu->addAction(q("Remove Show"));
            connect(deleteShow, &QAction::triggered, [this, showId]() {
                delete_tv_show(showId.c_str());
            });
        }

        if (!episodeId.empty()) {
            QString episodeText = index.data(Qt::DisplayRole).toString();
            QString seasonText = "Season";
            if (episodeText.startsWith("S") && episodeText.length() >= 3 && isdigit(episodeText[1].toLatin1()) && isdigit(episodeText[2].toLatin1())) {
                seasonText = "Season " + episodeText.mid(1, 2);
            }

            auto confirmAction = menu.addAction(q("Confirm Plays"));
            auto unidentifyAction = menu.addAction(q("Unidentify"));
            auto reencodeAction = menu.addAction(q("Re-encode (ffmpeg)"));

            if (has_original_for_tv_episode(episodeId.c_str())) {
                auto restoreAction = menu.addAction(q("Restore Original"));
                connect(restoreAction, &QAction::triggered, [episodeId]() {
                    restore_original_for_tv_episode(episodeId.c_str());
                });
            }

            auto deleteMenu = menu.addMenu(q("Remove Metadata"));
            auto deleteShow = deleteMenu->addAction(q("Remove Show"));
            auto deleteSeason = deleteMenu->addAction(q(std::format("Remove {}", seasonText.toStdString())));

            connect(deleteShow, &QAction::triggered, [this, showId]() {
                delete_tv_show(showId.c_str());
            });

            connect(deleteSeason, &QAction::triggered, [this, showId, episodeText]() {
                int season = 0;
                if (episodeText.startsWith("S") && episodeText.length() >= 3 && isdigit(episodeText[1].toLatin1()) && isdigit(episodeText[2].toLatin1())) {
                    season = episodeText.mid(1, 2).toInt();
                }
                if (season > 0) {
                    delete_tv_season(showId.c_str(), season);
                }
            });

            connect(confirmAction, &QAction::triggered, [episodeId]() {
                confirm_tv_episode_plays(episodeId.c_str());
            });

            connect(unidentifyAction, &QAction::triggered, [episodeId]() {
                unidentify_tv_episode(episodeId.c_str());
            });

            connect(reencodeAction, &QAction::triggered, [this, episodeId, showId]() {
                auto command = _encodeCommand.toStdString();
                reencode_tv_episode(episodeId.c_str(), command.c_str());
                this->_encodeUploadTargets[episodeId] = showId;
                this->ffmpegQueueCount++;
                this->_updateFfmpegStatus();
            });
        }

        connect(uploadAction, &QAction::triggered, [this, showId]() {
            auto tvLoc = _embyTvLocation.toStdString();
            auto movieLoc = _embyMovieLocation.toStdString();
            rsync_show(showId.c_str(), tvLoc.c_str(), movieLoc.c_str());
            this->rsyncQueueCount++;
            this->_updateRsyncStatus();
        });

        connect(loadFromNasAction, &QAction::triggered, [this, showId]() {
            auto tvLoc = _embyTvLocation.toStdString();
            auto movieLoc = _embyMovieLocation.toStdString();
            rsync_from_nas(showId.c_str(), tvLoc.c_str(), movieLoc.c_str());
            this->rsyncQueueCount++;
            this->_updateRsyncStatus();
        });

        connect(createPortableAction, &QAction::triggered, [this, index]() {
            auto* model = dynamic_cast<QStandardItemModel *>(ui->showsTree->model());
            QModelIndex showIndex = index.parent().isValid() ? index.parent() : index;
            std::string uploadShowId = model->data(showIndex, Qt::UserRole).toString().toStdString();
            int rows = model->rowCount(showIndex);
            if (index.parent().isValid()) {
                std::string epId = model->data(index, Qt::UserRole).toString().toStdString();
                if (!epId.empty()) {
                    portable_encode(epId.c_str());
                    this->_encodeUploadTargets[epId] = uploadShowId;
                    this->ffmpegQueueCount++;
                }
            } else {
                for (int i = 0; i < rows; ++i) {
                    QModelIndex epIndex = model->index(i, 0, showIndex);
                    std::string epId = model->data(epIndex, Qt::UserRole).toString().toStdString();
                    if (!epId.empty()) {
                        portable_encode(epId.c_str());
                        this->_encodeUploadTargets[epId] = uploadShowId;
                        this->ffmpegQueueCount++;
                    }
                }
            }
            this->_updateFfmpegStatus();
        });

        connect(reencodeShowAction, &QAction::triggered, [this, index]() {
            auto* model = dynamic_cast<QStandardItemModel *>(ui->showsTree->model());
            QModelIndex showIndex = index.parent().isValid() ? index.parent() : index;
            std::string uploadShowId = model->data(showIndex, Qt::UserRole).toString().toStdString();
            int rows = model->rowCount(showIndex);
            auto command = _encodeCommand.toStdString();
            for (int i = 0; i < rows; ++i) {
                QModelIndex epIndex = model->index(i, 0, showIndex);
                std::string epId = model->data(epIndex, Qt::UserRole).toString().toStdString();
                if (epId.empty()) continue;
                if (has_original_for_tv_episode(epId.c_str())) continue;

                auto fileName = get_filename_for_tv_episode_id(epId.c_str());
                if (!fileName) continue;
                free_string(fileName);

                reencode_tv_episode(epId.c_str(), command.c_str());
                this->_encodeUploadTargets[epId] = uploadShowId;
                this->ffmpegQueueCount++;
            }
            this->_updateFfmpegStatus();
        });

        connect(confirmShowAction, &QAction::triggered, [this, index]() {
            auto* model = dynamic_cast<QStandardItemModel *>(ui->showsTree->model());
            QModelIndex showIndex = index.parent().isValid() ? index.parent() : index;
            int rows = model->rowCount(showIndex);
            for (int i = 0; i < rows; ++i) {
                QModelIndex epIndex = model->index(i, 0, showIndex);
                QBrush brush = epIndex.data(Qt::ForegroundRole).value<QBrush>();
                if (brush.color() != QColor("green")) continue;

                std::string epId = model->data(epIndex, Qt::UserRole).toString().toStdString();
                if (!epId.empty()) {
                    confirm_tv_episode_plays(epId.c_str());
                }
            }
        });

        menu.exec(ui->showsTree->viewport()->mapToGlobal(pos));
    });

    // Set context menu on films
    ui->filmsTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->filmsTree, &QWidget::customContextMenuRequested, [&](const QPoint &pos) {
        qDebug() << "filmsTree customContextMenuRequested at" << pos;
        auto index = ui->filmsTree->indexAt(pos);
        if (!index.isValid()) {
            qDebug() << "index is not valid";
            return;
        }
        ui->filmsTree->setCurrentIndex(index);

        auto id = index.model()->data(index, Qt::UserRole).toString().toStdString();
        qDebug() << "id from UserRole:" << q(id);

        std::string filmId;
        if (index.parent().isValid()) {
            filmId = index.parent().data(Qt::UserRole).toString().toStdString();
        } else {
            filmId = id;
        }

        if (filmId.empty()) return;

        QMenu menu;

        QBrush brush = index.data(Qt::ForegroundRole).value<QBrush>();
        if (brush.color() == QColor("green") || brush.color() == QColor("cyan")) {
            auto fileName = get_filename_for_film_video_id(id.c_str());
            if (fileName) {
                std::string path(fileName);
                free_string(fileName);

                QAction * loadAction = menu.addAction(q("Load in Player"));
                connect(loadAction, &QAction::triggered, [this, path]() {
                    _loadInPlayer(QString::fromStdString(path));
                });

                if (_vlcFound) {
                    QAction * vlcAction = menu.addAction(q("Open in VLC"));
                    connect(vlcAction, &QAction::triggered, [this, path]() {
                        QStringList args = _vlcArgs;
                        args.append(QString::fromStdString(path));
                        if (_vlcProgram == "flatpak") {
                            args.append("@@");
                        }
                        qDebug() << "Launching VLC:" << _vlcProgram << args;
                        if (!QProcess::startDetached(_vlcProgram, args)) {
                            qDebug() << "Failed to start VLC process";
                        }
                    });
                }
            }
        }

        QAction * uploadAction = menu.addAction(q("Upload Film (rsync)"));
        QAction * loadFromNasAction = menu.addAction(q("Load from NAS"));
        QAction * createPortableAction = menu.addAction(q("Create Portable Version"));
        QAction * confirmFilmAction = menu.addAction(q("Confirm Plays (Film)"));

        std::string filmVideoId;
        if (index.parent().isValid()) {
            filmVideoId = id;
        }

        if (filmVideoId.empty()) {
            auto deleteMenu = menu.addMenu(q("Remove Metadata"));
            auto deleteFilmAction = deleteMenu->addAction(q("Remove Film"));
            connect(deleteFilmAction, &QAction::triggered, [filmId]() {
                delete_film(filmId.c_str());
            });
        }

        if (!filmVideoId.empty()) {
            auto confirmAction = menu.addAction(q("Confirm Plays"));
            auto unidentifyAction = menu.addAction(q("Unidentify"));
            auto reencodeAction = menu.addAction(q("Re-encode (ffmpeg)"));

            if (has_original_for_film_video(filmVideoId.c_str())) {
                auto restoreAction = menu.addAction(q("Restore Original"));
                connect(restoreAction, &QAction::triggered, [filmVideoId]() {
                    restore_original_for_film_video(filmVideoId.c_str());
                });
            }

            auto deleteMenu = menu.addMenu(q("Remove Metadata"));
            auto deleteFilmAction = deleteMenu->addAction(q("Remove Film"));
            auto deleteVideoAction = deleteMenu->addAction(q("Remove Video"));

            connect(deleteFilmAction, &QAction::triggered, [filmId]() {
                delete_film(filmId.c_str());
            });

            connect(deleteVideoAction, &QAction::triggered, [filmVideoId]() {
                delete_film_video(filmVideoId.c_str());
            });

            connect(confirmAction, &QAction::triggered, [filmVideoId]() {
                confirm_film_video_plays(filmVideoId.c_str());
            });

            connect(unidentifyAction, &QAction::triggered, [filmVideoId]() {
                unidentify_film_video(filmVideoId.c_str());
            });

            connect(reencodeAction, &QAction::triggered, [this, filmVideoId, filmId]() {
                auto command = _encodeCommand.toStdString();
                reencode_film_video(filmVideoId.c_str(), command.c_str());
                this->_encodeUploadTargets[filmVideoId] = filmId;
                this->ffmpegQueueCount++;
                this->_updateFfmpegStatus();
            });
        }

        connect(uploadAction, &QAction::triggered, [this, filmId]() {
            auto tvLoc = _embyTvLocation.toStdString();
            auto movieLoc = _embyMovieLocation.toStdString();
            rsync_show(filmId.c_str(), tvLoc.c_str(), movieLoc.c_str());
            this->rsyncQueueCount++;
            this->_updateRsyncStatus();
        });

        connect(loadFromNasAction, &QAction::triggered, [this, filmId]() {
            auto tvLoc = _embyTvLocation.toStdString();
            auto movieLoc = _embyMovieLocation.toStdString();
            rsync_from_nas(filmId.c_str(), tvLoc.c_str(), movieLoc.c_str());
            this->rsyncQueueCount++;
            this->_updateRsyncStatus();
        });

        connect(createPortableAction, &QAction::triggered, [this, index]() {
            auto* model = dynamic_cast<QStandardItemModel *>(ui->filmsTree->model());
            QModelIndex filmIndex = index.parent().isValid() ? index.parent() : index;
            std::string uploadFilmId = model->data(filmIndex, Qt::UserRole).toString().toStdString();
            int rows = model->rowCount(filmIndex);
            if (index.parent().isValid()) {
                std::string videoId = model->data(index, Qt::UserRole).toString().toStdString();
                if (!videoId.empty()) {
                    portable_encode(videoId.c_str());
                    this->_encodeUploadTargets[videoId] = uploadFilmId;
                    this->ffmpegQueueCount++;
                }
            } else {
                for (int i = 0; i < rows; ++i) {
                    QModelIndex vIndex = model->index(i, 0, filmIndex);
                    std::string vId = model->data(vIndex, Qt::UserRole).toString().toStdString();
                    if (!vId.empty()) {
                        portable_encode(vId.c_str());
                        this->_encodeUploadTargets[vId] = uploadFilmId;
                        this->ffmpegQueueCount++;
                    }
                }
            }
            this->_updateFfmpegStatus();
        });

        connect(confirmFilmAction, &QAction::triggered, [this, index]() {
            auto* model = dynamic_cast<QStandardItemModel *>(ui->filmsTree->model());
            QModelIndex filmIndex = index.parent().isValid() ? index.parent() : index;
            int rows = model->rowCount(filmIndex);
            for (int i = 0; i < rows; ++i) {
                QModelIndex vIndex = model->index(i, 0, filmIndex);
                QBrush brush = vIndex.data(Qt::ForegroundRole).value<QBrush>();
                if (brush.color() != QColor("green")) continue;

                std::string vId = model->data(vIndex, Qt::UserRole).toString().toStdString();
                if (!vId.empty()) {
                    confirm_film_video_plays(vId.c_str());
                }
            }
        });

        menu.exec(ui->filmsTree->viewport()->mapToGlobal(pos));
    });

    // Button handlers
    connect(ui->playBtn, &QPushButton::clicked, [&]() {
        player->play();
    });

    connect(ui->pauseBtn, &QPushButton::clicked, [&]() {
        player->pause();
    });

    connect(ui->tmdbFetchBtn, &QPushButton::clicked, [&]() {
        bool ok = false;
        auto idText = QInputDialog::getText(this, "TMDB Lookup", "TMDB ID:",
            QLineEdit::Normal, QString(), &ok);
        if (!ok || idText.isEmpty()) return;

        auto idEdit = idText.toStdString();
        auto apiKeyEdit = _tmdbApiKey.toStdString();
        auto id = idEdit.c_str();
        auto apiKey = apiKeyEdit.c_str();

        if (ui->tmdbModeBtn->text() == "TV") {
            lookup_tv(id, apiKey);
        } else {
            lookup_film(id, apiKey);
        }
    });

    connect(ui->tmdbModeBtn, &QPushButton::clicked, [&]() {
        appModel->toggleTmdbMode();
        if (appModel->tmdbMode() == "TV") {
            ui->filmsTree->hide();
            ui->showsTree->show();
        } else {
            ui->filmsTree->show();
            ui->showsTree->hide();
        }
        ui->tmdbModeBtn->setText(q(appModel->tmdbMode()));
    });

    connect(ui->identifyBtn, &QPushButton::clicked, [&]() {
        auto isTv = ui->tmdbModeBtn->text() == "TV";

        auto to = isTv
            ? _getIdForSelectedItemInTree(ui->showsTree)
            : _getIdForSelectedItemInTree(ui->filmsTree);

        auto from = _getIdForSelectedItemInTree(ui->disksTree);

        if (isTv)
            map_tv_episode(from.c_str(), to.c_str());
        else
            map_film_video(from.c_str(), to.c_str());
    });

    connect(ui->gcBtn, &QPushButton::clicked, [&]() {
        collect_garbage();
    });

    connect(ui->copyUsbBtn, &QPushButton::clicked, [&]() {
        copy_from_usb(_deleteUsbAfterCopy);
        this->copyQueueCount++;
        this->_updateCopyStatus();
    });

    connect(ui->importBtn, &QPushButton::clicked, [&]() {
        // QFileDialog has no built-in "file or folder" mode. Forcing the
        // non-native dialog into Directory mode with ShowDirsOnly off lists
        // files alongside folders and still lets Choose accept a highlighted
        // file, so a single dialog can pick either.
        QFileDialog dialog(this, "Import File or Folder");
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::DontUseNativeDialog, true);
        dialog.setOption(QFileDialog::ShowDirsOnly, false);
        dialog.setNameFilter("Video files (*.mkv *.webm)");

        if (dialog.exec() != QDialog::Accepted) return;

        auto selected = dialog.selectedFiles();
        if (selected.isEmpty()) return;

        auto path = selected.first().toStdString();
        import_path(path.c_str());
        this->copyQueueCount++;
        this->_updateCopyStatus();
    });

    connect(ui->restoreBtn, &QPushButton::clicked, [&]() {
        initial_load();
    });

    connect(ui->fileInventoryBtn, &QPushButton::clicked, [&]() {
        file_inventory();
    });

    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    appModel->resetDrag();
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }

    QPoint pos = event->pos();
    if (!appModel->isDragActive()) {
        appModel->setInitialDragData(
            pos.x(),
            ui->disksTree->width(),
            ui->showsTree->width()
        );
    } else {
        appModel->setDragCurrentX(pos.x());
    }

    auto offset = appModel->getCurrentDragXOffset();
    if (offset != 0) {
        auto treesMask = appModel->getTreesMask();
        if (treesMask & 1) {
            qDebug() << "Setting showsTree to" << appModel->getCurrentShowsTreeWidth();
            ui->showsTree->setMaximumWidth(appModel->getCurrentShowsTreeWidth());
            ui->filmsTree->setMaximumWidth(appModel->getCurrentShowsTreeWidth());
        }
        if (treesMask & 2) {
            qDebug() << "Setting disksTree to" << appModel->getCurrentDisksTreeWidth();
            ui->disksTree->setMaximumWidth(appModel->getCurrentDisksTreeWidth());
        }
    }

    QWidget::mouseMoveEvent(event);
}

// Pulls a "key=value" field (whitespace-terminated) out of a status line,
// e.g. extracting "73.2%" from "... progress=73.2% eta=00:12:34".
static std::optional<std::string> _extractStatusField(const std::string& line, const std::string& key) {
    auto pat = key + "=";
    auto idx = line.find(pat);
    if (idx == std::string::npos) return std::nullopt;
    auto start = idx + pat.size();
    auto end = line.find_first_of(" \t", start);
    if (end == std::string::npos) end = line.size();
    if (end == start) return std::nullopt;
    return line.substr(start, end - start);
}

namespace {
struct ProgressInfo {
    std::string display;
    bool hasProgress = false;
    double percent = 0.0;
};
}

// Parses an "H:MM:SS" (or "HH:MM:SS") duration string, as emitted by both the
// ffmpeg and rsync progress augmenters, into a second count.
static std::optional<qint64> _parseDurationSeconds(const std::string& s) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        auto pos = s.find(':', start);
        parts.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    if (parts.size() != 3) return std::nullopt;
    bool ok1 = false, ok2 = false, ok3 = false;
    qint64 h = QString::fromStdString(parts[0]).toLongLong(&ok1);
    qint64 m = QString::fromStdString(parts[1]).toLongLong(&ok2);
    qint64 sec = QString::fromStdString(parts[2]).toLongLong(&ok3);
    if (!ok1 || !ok2 || !ok3) return std::nullopt;
    return h * 3600 + m * 60 + sec;
}

// Strips the machine-readable "progress=" / "eta=" tokens (appended by the
// worker to ffmpeg and rsync status lines) out of the raw line, replacing
// "eta=" (a remaining-time duration) with the human-readable clock time it
// resolves to, e.g. "(ETA 08:16)", and reports the percent so the caller can
// drive a QProgressBar.
static ProgressInfo _parseProgressLine(const std::string& rawOutput) {
    ProgressInfo info;
    info.display = rawOutput;

    if (auto progressStr = _extractStatusField(rawOutput, "progress")) {
        auto idx = info.display.find(" progress=");
        if (idx != std::string::npos) info.display.erase(idx);
        bool ok = false;
        double pct = QString::fromStdString(*progressStr).chopped(1).toDouble(&ok);
        if (ok) {
            info.hasProgress = true;
            info.percent = pct;
        }
    }
    if (auto etaStr = _extractStatusField(rawOutput, "eta")) {
        if (auto secs = _parseDurationSeconds(*etaStr)) {
            auto etaTime = QDateTime::currentDateTime().addSecs(*secs);
            info.display += std::format(" (ETA {})", etaTime.toString("HH:mm").toStdString());
        }
    }
    return info;
}

// Points a progress bar at the given percentage and hides the adjacent text
// spinner while it's visible (they'd otherwise be redundant motion).
static void _applyProgress(const ProgressInfo& info, QProgressBar* bar, QLabel* spinnerLabel) {
    if (info.hasProgress) {
        bar->setRange(0, 1000);
        bar->setValue(static_cast<int>(info.percent * 10.0));
        bar->show();
        spinnerLabel->hide();
    } else {
        bar->hide();
        spinnerLabel->show();
    }
}

void MainWindow::_updateFfmpegStatus() {
    qDebug() << "_updateFfmpegStatus: active=" << ffmpegActiveCount << "queue=" << ffmpegQueueCount << "output=" << q(lastFfmpegOutput);
    if (ffmpegActiveCount > 0 || ffmpegQueueCount > 0 || lastFfmpegOutput.starts_with("Error:")) {
        ui->ffmpegStatusWidget->show();
        if (ffmpegActiveCount > 0) {
            spinnerTimer->start(250);
            std::string prefix = "Encoding";
            if (lastFfmpegOutput.find("Match Scan") != std::string::npos) {
                prefix = "Scanning";
            }

            auto info = _parseProgressLine(lastFfmpegOutput);
            auto status = info.display.empty()
                ? std::format("{}: {} ({} in queue)", prefix, currentEncodingFile, ffmpegQueueCount)
                : std::format("{}: {} | {} ({} in queue)", prefix, currentEncodingFile, info.display, ffmpegQueueCount);
            ui->statusLabel->setText(q(status));
            _applyProgress(info, ui->ffmpegProgressBar, ui->spinnerLabel);
        } else if (ffmpegQueueCount > 0) {
            if (rsyncActiveCount == 0 && copyActiveCount == 0) spinnerTimer->stop();
            ui->spinnerLabel->setText("-");
            ui->spinnerLabel->show();
            ui->statusLabel->setText(q(std::format("Waiting: {} jobs in queue", ffmpegQueueCount)));
            ui->ffmpegProgressBar->hide();
        } else {
            if (rsyncActiveCount == 0 && copyActiveCount == 0) spinnerTimer->stop();
            ui->spinnerLabel->setText("!");
            ui->spinnerLabel->show();
            ui->statusLabel->setText(q(lastFfmpegOutput));
            ui->ffmpegProgressBar->hide();
        }
    } else {
        ui->ffmpegStatusWidget->hide();
        ui->ffmpegProgressBar->hide();
        if (rsyncActiveCount == 0 && copyActiveCount == 0) spinnerTimer->stop();
    }
}

void MainWindow::_updateRsyncStatus() {
    qDebug() << "_updateRsyncStatus: active=" << rsyncActiveCount << "queue=" << rsyncQueueCount << "output=" << q(lastRsyncOutput);
    if (rsyncActiveCount > 0 || rsyncQueueCount > 0 || lastRsyncOutput.starts_with("Error:")) {
        ui->rsyncStatusWidget->show();
        if (rsyncActiveCount > 0) {
            spinnerTimer->start(250);
            auto info = _parseProgressLine(lastRsyncOutput);
            auto status = info.display.empty()
                ? std::format("rsync: {} ({} in queue)", currentRsyncFile, rsyncQueueCount)
                : std::format("rsync: {} | {} ({} in queue)", currentRsyncFile, info.display, rsyncQueueCount);
            ui->rsyncStatusLabel->setText(q(status));
            _applyProgress(info, ui->rsyncProgressBar, ui->rsyncSpinnerLabel);
        } else if (rsyncQueueCount > 0) {
            if (ffmpegActiveCount == 0 && copyActiveCount == 0) spinnerTimer->stop();
            ui->rsyncSpinnerLabel->setText("-");
            ui->rsyncSpinnerLabel->show();
            ui->rsyncStatusLabel->setText(q(std::format("Waiting: {} rsync jobs in queue", rsyncQueueCount)));
            ui->rsyncProgressBar->hide();
        } else {
            if (ffmpegActiveCount == 0 && copyActiveCount == 0) spinnerTimer->stop();
            ui->rsyncSpinnerLabel->setText("!");
            ui->rsyncSpinnerLabel->show();
            ui->rsyncStatusLabel->setText(q(lastRsyncOutput));
            ui->rsyncProgressBar->hide();
        }
    } else {
        ui->rsyncStatusWidget->hide();
        ui->rsyncProgressBar->hide();
        if (ffmpegActiveCount == 0 && copyActiveCount == 0) spinnerTimer->stop();
    }
}

void MainWindow::_updateCopyStatus() {
    qDebug() << "_updateCopyStatus: active=" << copyActiveCount << "queue=" << copyQueueCount << "output=" << q(lastCopyOutput);
    ui->copyUsbBtn->setEnabled(_mUsbPresent && copyActiveCount == 0 && copyQueueCount == 0);
    ui->importBtn->setEnabled(copyActiveCount == 0 && copyQueueCount == 0);
    if (copyActiveCount > 0 || copyQueueCount > 0 || lastCopyOutput.starts_with("Error:")) {
        ui->copyStatusWidget->show();
        if (copyActiveCount > 0) {
            spinnerTimer->start(250);
            // "Copying ...", "Deleting ...", and "Importing ..." are already
            // self-describing; only prefix the generic ones (progress-less
            // status lines, errors) with "Copy:".
            bool selfDescribing = lastCopyOutput.starts_with("Copying") || lastCopyOutput.starts_with("Deleting") || lastCopyOutput.starts_with("Importing");
            auto status = lastCopyOutput.empty()
                ? std::string("Copying from USB...")
                : (selfDescribing ? lastCopyOutput : std::format("Copy: {}", lastCopyOutput));
            ui->copyStatusLabel->setText(q(status));
        } else if (copyQueueCount > 0) {
            if (ffmpegActiveCount == 0 && rsyncActiveCount == 0) spinnerTimer->stop();
            ui->copySpinnerLabel->setText("-");
            ui->copyStatusLabel->setText(q(std::format("Waiting: {} copy jobs in queue", copyQueueCount)));
        } else {
            if (ffmpegActiveCount == 0 && rsyncActiveCount == 0) spinnerTimer->stop();
            ui->copySpinnerLabel->setText("!");
            ui->copyStatusLabel->setText(q(lastCopyOutput));
        }
    } else {
        ui->copyStatusWidget->hide();
        if (ffmpegActiveCount == 0 && rsyncActiveCount == 0) spinnerTimer->stop();
    }
}

// Reads ~/.config/rkworkbench/home_assistant.json (same config-dir convention
// the worker uses for tmdb.key). Absent or unparsable config just leaves the
// feature disabled rather than erroring, since it's opt-in.
void MainWindow::_loadHomeAssistantConfig() {
    auto path = QDir::homePath().toStdString() + "/.config/rkworkbench/home_assistant.json";
    std::ifstream file(path);
    if (!file.is_open()) {
        qDebug() << "Home Assistant config not found at" << q(path) << "- sensor push disabled";
        return;
    }

    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    try {
        auto j = json::parse(contents);
        _haBaseUrl = q(j.value("base_url", ""));
        _haToken = q(j.value("token", ""));
    } catch (const std::exception& e) {
        qDebug() << "Failed to parse Home Assistant config:" << e.what();
        return;
    }

    if (_haBaseUrl.endsWith('/')) _haBaseUrl.chop(1);
    _haConfigured = !_haBaseUrl.isEmpty() && !_haToken.isEmpty();
    if (!_haConfigured) {
        qDebug() << "Home Assistant config missing base_url/token - sensor push disabled";
    }
}

// Sets a single HA sensor's state via the REST API (POST /api/states/<entity_id>).
// Fire-and-forget: errors are logged, not surfaced, since this is a best-effort
// monitoring side channel and shouldn't interrupt encoding/rsync on a flaky network.
void MainWindow::_postHomeAssistantState(const QString &entityId, double state, const QString &unit, const QString &friendlyName) {
    if (!_haConfigured) return;

    QNetworkRequest request(QUrl(_haBaseUrl + "/api/states/" + entityId));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + _haToken).toUtf8());

    json body;
    body["state"] = state;
    body["attributes"]["unit_of_measurement"] = unit.toStdString();
    body["attributes"]["friendly_name"] = friendlyName.toStdString();

    auto* reply = haNetworkManager->post(request, QByteArray::fromStdString(body.dump()));
    connect(reply, &QNetworkReply::finished, reply, [reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Home Assistant push failed:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

// Pushed on a 30s timer regardless of activity, so idle/zero states reach HA
// too - that's what lets an HA automation notice the queues drained and fire
// a "job's done" notification. Queue depth includes the in-flight job (not
// just what's waiting) so it reads 0 only once everything has finished.
void MainWindow::_pushHomeAssistantSensors() {
    if (!_haConfigured) return;

    double encodeProgress = 0.0;
    if (ffmpegActiveCount > 0) {
        auto info = _parseProgressLine(lastFfmpegOutput);
        if (info.hasProgress) encodeProgress = info.percent;
    }

    double rsyncProgress = 0.0;
    if (rsyncActiveCount > 0) {
        auto info = _parseProgressLine(lastRsyncOutput);
        if (info.hasProgress) rsyncProgress = info.percent;
    }

    _postHomeAssistantState("sensor.rkwb_encode_progress", encodeProgress, "%", "rkwb Encode Progress");
    _postHomeAssistantState("sensor.rkwb_encode_queue", ffmpegQueueCount + (ffmpegActiveCount > 0 ? 1 : 0), "jobs", "rkwb Encode Queue Depth");
    _postHomeAssistantState("sensor.rkwb_rsync_progress", rsyncProgress, "%", "rkwb Rsync Progress");
    _postHomeAssistantState("sensor.rkwb_rsync_queue", rsyncQueueCount + (rsyncActiveCount > 0 ? 1 : 0), "jobs", "rkwb Rsync Queue Depth");
}

// Tears down any existing push timer and reloads from disk - called at
// startup and again whenever SettingsDialog closes, so a change takes effect
// immediately without restarting the app.
void MainWindow::reinitHomeAssistant() {
    if (haPushTimer) {
        haPushTimer->stop();
        haPushTimer->deleteLater();
        haPushTimer = nullptr;
    }
    _haConfigured = false;
    _haBaseUrl.clear();
    _haToken.clear();

    _loadHomeAssistantConfig();
    if (_haConfigured) {
        haPushTimer = new QTimer(this);
        connect(haPushTimer, &QTimer::timeout, this, [this]() {
            _pushHomeAssistantSensors();
        });
        haPushTimer->start(30000);
        _pushHomeAssistantSensors();
    }
}

// Reads ~/.config/rkworkbench/emby.json. Absent config falls back to the
// defaults this app has always shipped with, so existing users see no
// behavior change until they open Settings and save their own values.
void MainWindow::_loadEmbySettings() {
    _embyTvLocation = "root@10.4.6.2:/mnt/user/emby/tv";
    _embyMovieLocation = "root@10.4.6.2:/mnt/user/emby/movies";
    _rsyncAfterEncode = false;

    auto path = QDir::homePath().toStdString() + "/.config/rkworkbench/emby.json";
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    try {
        auto j = json::parse(contents);
        _embyTvLocation = q(j.value("tv_location", _embyTvLocation.toStdString()));
        _embyMovieLocation = q(j.value("movie_location", _embyMovieLocation.toStdString()));
        _rsyncAfterEncode = j.value("rsync_after_encode", false);
    } catch (const std::exception& e) {
        qDebug() << "Failed to parse Emby config:" << e.what();
    }
}

// Reads ~/.config/rkworkbench/usb.json, written by SettingsDialog.
void MainWindow::_loadUsbCopySettings() {
    _deleteUsbAfterCopy = false;

    auto path = QDir::homePath().toStdString() + "/.config/rkworkbench/usb.json";
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    try {
        auto j = json::parse(contents);
        _deleteUsbAfterCopy = j.value("delete_after_copy", false);
    } catch (const std::exception& e) {
        qDebug() << "Failed to parse USB copy config:" << e.what();
    }
}

// Reads ~/.config/rkworkbench/encode.json, written by SettingsDialog. Absent
// config falls back to the NVENC HEVC preset this app has always defaulted to.
void MainWindow::_loadEncodeSettings() {
    _encodeCommand = "ffmpeg -hwaccel cuda -i ${in} -map 0 -c:v hevc_nvenc -preset p7 -rc vbr -cq 18 -pix_fmt p010le -c:a copy -c:s copy -c:d copy ${out}";
    _encodePresetLabel = "NVENC HEVC";

    auto path = QDir::homePath().toStdString() + "/.config/rkworkbench/encode.json";
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    try {
        auto j = json::parse(contents);
        _encodeCommand = q(j.value("command", _encodeCommand.toStdString()));
        _encodePresetLabel = q(j.value("template_label", _encodePresetLabel.toStdString()));
    } catch (const std::exception& e) {
        qDebug() << "Failed to parse encode config:" << e.what();
    }
}

void MainWindow::_updateEncodeBtnText() {
    ui->encodeSettingsBtn->setText(q(std::format("Encode Preset\n({})", _encodePresetLabel.toStdString())));
}

bool MainWindow::hasActiveJobs() const {
    return ffmpegActiveCount > 0 || ffmpegQueueCount > 0
        || rsyncActiveCount > 0 || rsyncQueueCount > 0
        || copyActiveCount > 0 || copyQueueCount > 0;
}

std::string MainWindow::workingDirPath() const {
    return appModel->workingDirPath();
}

void MainWindow::_updateUsbStatus() {
    // Don't poll while a copy is in flight; the source files are being read.
    if (copyActiveCount > 0 || copyQueueCount > 0) {
        return;
    }

    auto raw = usb_status();
    if (!raw) {
        return;
    }
    std::string statusJson(raw);
    free_string(raw);

    bool present = false;
    std::string label;
    int titleCount = 0;
    double gb = 0.0;

    try {
        auto status = json::parse(statusJson);
        present = status.value("present", false);
        label = status.value("label", std::string());
        titleCount = status.value("title_count", 0);
        auto totalBytes = status.value("total_bytes", 0ULL);
        gb = static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0);
    } catch (const json::exception& e) {
        qDebug() << "Failed to parse usb_status response:" << q(statusJson) << q(e.what());
        return;
    }

    _mUsbPresent = present;
    ui->copyUsbBtn->setEnabled(present && titleCount > 0);

    if (!present) {
        ui->copyUsbBtn->setText(q("Copy from\nUSB"));
    } else if (titleCount == 0) {
        ui->copyUsbBtn->setText(q(std::format("Copy from\nUSB\n({})\nno titles", label)));
    } else {
        ui->copyUsbBtn->setText(q(std::format("Copy from\nUSB\n({})\n{}x {:.1f}G", label, titleCount, gb)));
    }
}

void MainWindow::_findVlc() {
    if (_vlcFound) return;

    // Try standard vlc
    QProcess which;
    which.start("which", {"vlc"});
    which.waitForFinished();
    if (which.exitCode() == 0) {
        _vlcProgram = "vlc";
        _vlcArgs = {};
        _vlcFound = true;
        qDebug() << "Found vlc in PATH";
        return;
    }

    // Try flatpak
    which.start("which", {"flatpak"});
    which.waitForFinished();
    if (which.exitCode() == 0) {
        QProcess fpList;
        fpList.start("flatpak", {"list", "--columns=application"});
        fpList.waitForFinished();
        QString output = fpList.readAllStandardOutput();
        if (output.contains("org.videolan.VLC")) {
            _vlcProgram = "flatpak";
            // We use the user's recommended flags, but omit branch/arch for portability.
            // We include --file-forwarding and @@u as they are key for flatpak file access.
            _vlcArgs = {"run", "--file-forwarding", "org.videolan.VLC", "--started-from-file", "@@u"};
            _vlcFound = true;
            qDebug() << "Found org.videolan.VLC in flatpak";
            return;
        }
    }

    qDebug() << "VLC not found (neither 'vlc' in PATH nor 'org.videolan.VLC' in flatpak)";
}

void MainWindow::_clearMetadataPanel() {
    ui->metadataStill->clear();
    ui->metadataTitle->clear();
    ui->metadataDate->clear();
    ui->metadataLanguage->clear();
    ui->metadataRuntime->clear();
    ui->metadataPortable->clear();
    ui->metadataOverview->clear();
    ui->tracksGroup->hide();
    
    QLayoutItem *item;
    while ((item = ui->tracksLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }
}

// Dolby TrueHD/Atmos (and other lossless/stateful codecs) can only be decoded
// cleanly starting at a "major sync" frame. Blu-ray rips default to these
// tracks, so scrubbing to an arbitrary position lands the decoder mid-stream,
// producing corrupted "pip" audio and confusing the player's A/V clock into
// racing video ahead to catch up. DVD rips only ever carry AC3/MPEG audio,
// which decodes cleanly from any frame, so they never hit this. Prefer a
// non-lossless track (MakeMKV always also muxes an AC3 core) so previewing
// stays reliable; this only affects the in-app preview, not the archival
// ffmpeg encode, which still copies the original lossless track untouched.
void MainWindow::_selectSeekableAudioTrack() {
    auto tracks = player->audioTracks();
    if (tracks.isEmpty()) return;

    int active = player->activeAudioTrack();
    if (active < 0 || active >= tracks.size()) return;

    auto isUnseekable = [](const QMediaMetaData &meta) {
        auto codec = meta.value(QMediaMetaData::AudioCodec);
        if (!codec.isValid()) return false;
        auto audioCodec = codec.value<QMediaFormat::AudioCodec>();
        return audioCodec == QMediaFormat::AudioCodec::DolbyTrueHD
            || audioCodec == QMediaFormat::AudioCodec::FLAC
            || audioCodec == QMediaFormat::AudioCodec::ALAC;
    };

    if (!isUnseekable(tracks[active])) return;

    QString activeLanguage = tracks[active].stringValue(QMediaMetaData::Language);
    int fallback = -1;
    for (int i = 0; i < tracks.size(); i++) {
        if (i == active || isUnseekable(tracks[i])) continue;
        if (fallback < 0) fallback = i;
        if (tracks[i].stringValue(QMediaMetaData::Language) == activeLanguage) {
            fallback = i;
            break;
        }
    }

    if (fallback >= 0) {
        qDebug() << "Switching away from lossless audio track" << active << "to seek-friendly track" << fallback;
        player->setActiveAudioTrack(fallback);
    }
}

void MainWindow::_loadInPlayer(QString path) {
    player->stop();
    player->setSource(QUrl::fromLocalFile(path));
    player->setPlaybackRate(1.0);
    player->play();
    player->pause();
}


void MainWindow::processMessage(std::string message) {
    qDebug() << "[ cpp] incoming message:" << message;
    if (message == "\"WorkerReady\"") {
        initial_load();
    } else if (message == "\"ClearTrees\"") {
        _clearTrees();
    }

    // The message is (probably) JSON
    json m;
    try {
        m = json::parse(message);
    } catch (...) {
        return;
    }

    try {
        if (m.contains("CommandStarted")) {
            lastFfmpegOutput = "";
            auto req = m["CommandStarted"];
            if (req.contains("ReencodeRequest")) {
                std::string id = req["ReencodeRequest"].is_array() ? req["ReencodeRequest"][0].get<std::string>() : req["ReencodeRequest"].get<std::string>();
                auto fileName = get_filename_for_tv_episode_id(id.c_str());
                if (fileName) {
                    currentEncodingFile = fileName;
                    free_string(fileName);
                }
                
                ffmpegQueueCount = std::max(0, ffmpegQueueCount - 1);
                ffmpegActiveCount++;
                _updateFfmpegStatus();
            }
            if (req.contains("RsyncRequest") || req.contains("RsyncFromNasRequest")) {
                lastRsyncOutput = "";
                std::string key = req.contains("RsyncRequest") ? "RsyncRequest" : "RsyncFromNasRequest";
                auto args = req[key];
                std::string id = args[0].get<std::string>();
                currentRsyncFile = id;
                rsyncQueueCount = std::max(0, rsyncQueueCount - 1);
                rsyncActiveCount++;
                _updateRsyncStatus();
            }
            if (req.contains("ReencodeFilmRequest")) {
                std::string id = req["ReencodeFilmRequest"].is_array() ? req["ReencodeFilmRequest"][0].get<std::string>() : req["ReencodeFilmRequest"].get<std::string>();
                auto fileName = get_filename_for_film_video_id(id.c_str());
                if (fileName) {
                    currentEncodingFile = fileName;
                    free_string(fileName);
                }

                ffmpegQueueCount = std::max(0, ffmpegQueueCount - 1);
                ffmpegActiveCount++;
                _updateFfmpegStatus();
            }
            if (req.contains("PortableEncodeRequest")) {
                std::string id = req["PortableEncodeRequest"].is_array() ? req["PortableEncodeRequest"][0].get<std::string>() : req["PortableEncodeRequest"].get<std::string>();
                auto fileName = get_filename_for_tv_episode_id(id.c_str());
                if (!fileName) {
                    fileName = get_filename_for_film_video_id(id.c_str());
                }

                if (fileName) {
                    currentEncodingFile = std::string(fileName) + " (Portable)";
                    free_string(fileName);
                }

                ffmpegQueueCount = std::max(0, ffmpegQueueCount - 1);
                ffmpegActiveCount++;
                _updateFfmpegStatus();
            }
            if (req.contains("MatchScan")) {
                std::string id = req["MatchScan"].is_array() ? req["MatchScan"][0].get<std::string>() : req["MatchScan"].get<std::string>();
                auto fileName = get_filename_for_title_id(id.c_str());
                if (fileName) {
                    currentEncodingFile = fileName;
                    free_string(fileName);
                }

                ffmpegActiveCount++;
                _updateFfmpegStatus();
            }
            if (req == "PerformStitch") {
                currentEncodingFile = "Stitch Operation";
                ffmpegQueueCount = std::max(0, ffmpegQueueCount - 1);
                ffmpegActiveCount++;
                _updateFfmpegStatus();
            }
            if (req.contains("CopyFromUsb") || req.contains("ImportPath")) {
                lastCopyOutput = "";
                copyQueueCount = std::max(0, copyQueueCount - 1);
                copyActiveCount++;
                _updateCopyStatus();
            }
        }
    } catch (...) {}

    try {
        if (m.contains("CommandCompleted")) {
            auto req = m["CommandCompleted"];
            if (req.contains("ReencodeRequest") || req.contains("ReencodeFilmRequest") || req.contains("MatchScan") || req == "PerformStitch" || req.contains("PortableEncodeRequest")) {
                ffmpegActiveCount = std::max(0, ffmpegActiveCount - 1);
                bool encodeFailed = lastFfmpegOutput.starts_with("Error:");
                if (ffmpegActiveCount == 0) {
                    currentEncodingFile = "";
                    if (!encodeFailed) {
                        lastFfmpegOutput = "";
                    }
                }
                _updateFfmpegStatus();

                if (req.contains("ReencodeRequest") || req.contains("ReencodeFilmRequest") || req.contains("PortableEncodeRequest")) {
                    std::string key = req.contains("ReencodeRequest") ? "ReencodeRequest"
                        : req.contains("ReencodeFilmRequest") ? "ReencodeFilmRequest" : "PortableEncodeRequest";
                    auto args = req[key];
                    std::string id = args.is_array() ? args[0].get<std::string>() : args.get<std::string>();

                    auto it = _encodeUploadTargets.find(id);
                    if (it != _encodeUploadTargets.end()) {
                        std::string uploadId = it->second;
                        _encodeUploadTargets.erase(it);
                        if (_rsyncAfterEncode && !uploadId.empty() && !encodeFailed) {
                            auto tvLoc = _embyTvLocation.toStdString();
                            auto movieLoc = _embyMovieLocation.toStdString();
                            rsync_show(uploadId.c_str(), tvLoc.c_str(), movieLoc.c_str());
                            rsyncQueueCount++;
                            _updateRsyncStatus();
                        }
                    }
                }
            }
            if (req.contains("RsyncRequest") || req.contains("RsyncFromNasRequest")) {
                rsyncActiveCount = std::max(0, rsyncActiveCount - 1);
                if (rsyncActiveCount == 0) {
                    currentRsyncFile = "";
                    if (!lastRsyncOutput.starts_with("Error:")) {
                        lastRsyncOutput = "";
                    }
                }
                _updateRsyncStatus();
            }
            if (req.contains("CopyFromUsb") || req.contains("ImportPath")) {
                copyActiveCount = std::max(0, copyActiveCount - 1);
                if (copyActiveCount == 0) {
                    if (!lastCopyOutput.starts_with("Error:")) {
                        lastCopyOutput = "";
                    }
                }
                _updateCopyStatus();
            }
        }
    } catch (...) {}

    try {
        if (m.contains("RsyncOutput")) {
            lastRsyncOutput = m["RsyncOutput"].get<std::string>();
            _updateRsyncStatus();
        }
        if (m.contains("CopyOutput")) {
            lastCopyOutput = m["CopyOutput"].get<std::string>();
            _updateCopyStatus();
        }
    } catch (...) {}

    try {
        if (m.contains("MatchResults")) {
            auto treeName = m["MatchResults"]["tree"].get<std::string>();
            auto results = m["MatchResults"]["results"];

            auto tree = ui->showsTree;
            if (treeName == "Films") {
                tree = ui->filmsTree;
            }

            auto* model = dynamic_cast<QStandardItemModel *>(tree->model());
            for (int i = 0; i < model->rowCount(); ++i) {
                auto parentItem = model->item(i);
                bool parentVisible = false;
                for (int j = 0; j < parentItem->rowCount(); ++j) {
                    auto childItem = parentItem->child(j);
                    std::string id = childItem->data(Qt::UserRole).toString().toStdString();
                    std::string originalText = childItem->data(Qt::UserRole + 2).toString().toStdString();
                    if (originalText.empty()) originalText = childItem->data(Qt::DisplayRole).toString().toStdString();

                    if (results.contains(id)) {
                        auto result = results[id];
                        uint32_t diff = result["diff"].get<uint32_t>();
                        uint64_t ts = result["position_ms"].get<uint64_t>();

                        childItem->setText(q(std::format("{} [diff={} ts={}]", originalText, diff, ts)));
                        tree->setRowHidden(j, model->indexFromItem(parentItem), false);
                        parentVisible = true;
                    } else {
                        // Keep hidden if no result (usually means no still image available)
                        tree->setRowHidden(j, model->indexFromItem(parentItem), true);
                    }
                }
                tree->setRowHidden(i, QModelIndex(), !parentVisible);
            }
        }
    } catch (...) {}

    try {
        if (m.contains("SetTmdbStill")) {
            auto path = m["SetTmdbStill"]["path"].get<std::string>();
            QPixmap pixmap(q(path));
            if (!pixmap.isNull()) {
                if (pixmap.width() > 0) {
                    ui->metadataStill->setPixmap(pixmap.scaled(ui->metadataStill->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                }
            }
        }
    } catch (...) {}

    try {
        if (m.contains("SetMetadata")) {
            auto metadata = m["SetMetadata"]["metadata"];
            ui->metadataTitle->setText(q(metadata["title"].get<std::string>()));
            ui->metadataOverview->setText(q(metadata["overview"].get<std::string>()));
            
            std::string lang = metadata["language"].get<std::string>();
            ui->metadataLanguage->setText(lang.empty() ? "" : QString("Language: %1").arg(q(lang)));
            
            std::string date = metadata["release_date"].get<std::string>();
            ui->metadataDate->setText(date.empty() ? "" : QString("Release Date: %1").arg(q(date)));
            
            std::string runtime = metadata["runtime"].get<std::string>();
            ui->metadataRuntime->setText(runtime.empty() ? "" : QString("Runtime: %1").arg(q(runtime)));
        }
    } catch (...) {}

    try {
        if (m.contains("SetMkvTracks")) {
            auto tracks = m["SetMkvTracks"]["tracks"];
            ui->tracksGroup->setVisible(!tracks.empty());

            // Clear existing tracks first
            QLayoutItem *item;
            while ((item = ui->tracksLayout->takeAt(0)) != nullptr) {
                if (item->widget()) delete item->widget();
                delete item;
            }

            for (const auto& track : tracks) {
                int id = track["id"].get<int>();
                std::string type = track["type_"].get<std::string>();
                std::string codec = track["codec"].get<std::string>();
                std::string lang = track["language"].get<std::string>();
                std::string name = track["name"].is_null() ? "" : track["name"].get<std::string>();
                std::string profile = track["profile"].is_null() ? "" : track["profile"].get<std::string>();
                std::string bitrate = track["bitrate"].is_null() ? "" : track["bitrate"].get<std::string>();

                QString text = QString("[%1] %2 (%3) - %4")
                    .arg(id)
                    .arg(q(type))
                    .arg(q(codec))
                    .arg(q(lang));

                if (!profile.empty()) {
                    text += QString(" (%1)").arg(q(profile));
                }

                if (!bitrate.empty()) {
                    text += QString(" @ %1").arg(q(bitrate));
                }

                if (!name.empty()) {
                    text += QString(" - %1").arg(q(name));
                }

                QStringList flags;
                if (track["is_default"].get<bool>()) flags << "default";
                if (track["is_forced"].get<bool>()) flags << "forced";
                if (track["is_hearing_impaired"].get<bool>()) flags << "HI";
                if (track["is_commentary"].get<bool>()) flags << "commentary";

                if (!flags.isEmpty()) {
                    text += QString(" [%1]").arg(flags.join(", "));
                }

                QLabel *label = new QLabel(text);
                label->setWordWrap(true);
                ui->tracksLayout->addWidget(label);
            }
        }
    } catch (...) {}

    try {
        auto tree = m["AddTreeItem"]["tree"].get<std::string>();
        auto id = m["AddTreeItem"]["item"]["id"].get<std::string>();
        auto parentId = m["AddTreeItem"]["item"]["parent_id"].is_null() ? "" : m["AddTreeItem"]["item"].value("parent_id", "");
        auto parentText = m["AddTreeItem"]["item"]["parent_text"].get<std::string>();
        auto text = m["AddTreeItem"]["item"]["text"].get<std::string>();
        auto color = m["AddTreeItem"]["item"]["color"].get<std::string>();
        std::string after = m["AddTreeItem"]["after"].is_null() ? "" : m["AddTreeItem"].value("after", "");

        _addTreeItem(tree, id, parentId, parentText, text, color, after);
    } catch (...) {}

    try {
        auto tree = m["ChangeTreeItem"]["tree"].get<std::string>();
        auto id = m["ChangeTreeItem"]["id"].get<std::string>();
        auto color = m["ChangeTreeItem"]["change"]["ChangeColor"].get<std::string>();

        _changeTreeItemColor(tree, id, color);
    } catch (...) {}

    try {
        auto tree = m["ChangeTreeItem"]["tree"].get<std::string>();
        auto id = m["ChangeTreeItem"]["id"].get<std::string>();
        auto text = m["ChangeTreeItem"]["change"]["ChangeText"].get<std::string>();

        _changeTreeItemText(tree, id, text);
    } catch (...) {}

    try {
        auto tree = m["SelectTreeItem"]["tree"].get<std::string>();
        auto id = m["SelectTreeItem"]["id"].get<std::string>();

        _selectTreeItem(tree, id);
    } catch (...) {}

    try {
        if (m.contains("SeekPlayer")) {
            auto pos = m["SeekPlayer"]["position_ms"].get<int>();
            _mRequestedPlayerPosition = pos;
            player->setPosition(pos);
        }
    } catch (...) {}

    try {
        auto tree = m["RemoveTreeItemById"]["tree"].get<std::string>();
        auto id = m["RemoveTreeItemById"]["id"].get<std::string>();

        _removeTreeItemById(tree, id);
    } catch (...) {}

    try {
        if (m.contains("SetStitchList")) {
            auto files = m["SetStitchList"]["files"].get<std::vector<std::string>>();
            ui->stitchList->blockSignals(true);
            ui->stitchList->clear();
            for (const auto& file : files) {
                ui->stitchList->addItem(q(file));
            }
            ui->stitchList->blockSignals(false);
            ui->stitchGroup->setVisible(!files.empty());
        }
    } catch (...) {}


    try {
        auto garbageSize = m["ChangeGarbageSize"]["size"].get<std::uint64_t>();
        _changeGarbageSize(garbageSize);
    } catch (...) {}

    try {
        if (m.contains("FfmpegOutput")) {
            lastFfmpegOutput = m["FfmpegOutput"].get<std::string>();
            _updateFfmpegStatus();
        }
    } catch (...) {}
}

void callback_wrapper(void* ptr, const char* message) {
    std::string msg(message);
    if (auto* client = static_cast<MainWindow*>(ptr)) {
        QMetaObject::invokeMethod(client, "processMessage", Qt::QueuedConnection, Q_ARG(std::string, msg));
    }
}
