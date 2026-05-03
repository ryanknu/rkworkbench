#include "commandworker.h"
#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QDebug>
#include <QThread>
#include <QMenu>
#include <QAction>
#include <QAudioOutput>
#include <fstream>
#include <string>
#include <format>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <QMouseEvent>
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

    // TODO: If the item is selected, we should unselect it so you can see the new color.
    tree->clearSelection();

    if (!items.empty() && items.at(0).isValid()) {
        auto item = model->itemFromIndex(items.at(0));
        item->setForeground(QBrush(QColor(color.c_str())));
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

void MainWindow::_changeGarbageSize(std::uint64_t size) {
    double gb = static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0);
    ui->gcBtn->setText(q(std::format("Collect Garbage ({:.1f}G)", gb)));
    ui->gcBtn->setDisabled(size < 1);
}

void MainWindow::_hideTmdbApiKeyInput() {
    // TODO: Add button or something to bring it back.
    ui->tmdbApiKey->hide();
}

/**
 * Re-renders the disks tree from app model.
 */
void MainWindow::_reflowDisksTree() const {
    // auto* disksModel = dynamic_cast<QStandardItemModel *>(ui->disksTree->model());
    // disksModel->removeRows(0, disksModel->rowCount());
    //
    // std::unordered_map<std::string, QStandardItem*> disks;
    //
    // auto titles = appModel->titles();
    // std::ranges::sort(titles,
    //     [](RippedTitle* a, RippedTitle* b) {
    //         return *a < *b;
    //     }
    // );
    //
    // for (auto& title : titles) {
    //     auto titleItem = new QStandardItem(q(title->friendlyTitle()));
    //     titleItem->setData(q(title->id), Qt::UserRole);
    //     if (title->isDeleted()) {
    //         titleItem->setForeground(QBrush(QColor("red")));
    //     }
    //     else if (appModel->isIdentified(title->id)) {
    //         titleItem->setForeground(QBrush(QColor("orange")));
    //     }
    //
    //     if (!disks.contains(title->diskName())) {
    //         auto diskItem = new QStandardItem(q(title->diskName()));
    //         diskItem->setSelectable(false);
    //         disksModel->invisibleRootItem()
    //             ->appendRow(diskItem);
    //
    //         diskItem->appendRow(titleItem);
    //         disks[title->diskName()] = diskItem;
    //     } else {
    //         auto diskItem = disks[title->diskName()];
    //         diskItem->appendRow(titleItem);
    //     }
    // }
    //
    // ui->disksTree->expandAll();
}

/**
 * Re-renders the shows tree from app model.
 */
void MainWindow::_reflowShowsTree() const {
    // auto* showsModel = dynamic_cast<QStandardItemModel *>(ui->showsTree->model());
    // showsModel->removeRows(0, showsModel->rowCount());
    //
    // std::unordered_map<std::string, QStandardItem*> showItems;
    //
    // // Buffer for episodes for sorting
    // auto episodes = appModel->episodes();
    // std::ranges::sort(episodes,
    //     [](Episode* a, Episode* b) {
    //         return *a < *b;
    //     }
    // );
    //
    // for (auto& episode : episodes) {
    //     if (!appModel->hasShow(episode->showId)) continue;
    //     auto show = appModel->showById(episode->showId);
    //
    //     auto episodeItem = new QStandardItem(q(episode->friendlyTitle()));
    //     episodeItem->setData(q(episode->id), Qt::UserRole);
    //     if (appModel->isConfirmedPlays(episode->id)) {
    //         episodeItem->setForeground(QBrush(QColor("cyan")));
    //     }
    //     else if (appModel->showHasLocalFile(show.title, episode->seasonKey())) {
    //         episodeItem->setForeground(QBrush(QColor("green")));
    //     }
    //     else if (appModel->isIdentified(std::format("{}", episode->id))) {
    //         episodeItem->setForeground(QBrush(QColor("orange")));
    //     }
    //
    //     if (!showItems.contains(show.id)) {
    //         auto showItem = new QStandardItem(q(show.title));
    //         showItem->setSelectable(false);
    //         showsModel->invisibleRootItem()
    //             ->appendRow(showItem);
    //
    //         showItem->appendRow(episodeItem);
    //         showItems.emplace(show.id, showItem);
    //     } else {
    //         auto showItem = showItems.at(show.id);
    //         showItem->appendRow(episodeItem);
    //     }
    // }
    //
    // ui->showsTree->expandAll();
}

void MainWindow::_reflowGcButton() const {
    // ui->gcBtn->setText(q(std::format("Collect Garbage ({})", appModel->getGarbageCollectableBytes())));
    // ui->gcBtn->setDisabled(!appModel->canGarbageCollect());
}

void MainWindow::_reflowTaskList()
{
    // Get the model
    auto* model = dynamic_cast<QStringListModel *>(ui->tasksList->model());
    auto stringList = new QStringList();
    int i = 0;
    int t = appModel->queuedAndPendingJobs();
    int c = appModel->tasks()->size();

    for (auto& task : *appModel->tasks()) {
        i++;
        auto text = i < (c - t)
            ? std::format("[DONE] {}", task)
            : task;

        stringList->append(q(text));
    }

    model->setStringList(*stringList);

    if (appModel->tasks()->empty()) {
        ui->tasksList->setMaximumHeight(0);
    } else {
        ui->tasksList->setMaximumHeight(200);
        ui->tasksList->scrollToBottom();
    }
}

void MainWindow::_queueTask(std::string cmd)
{
    appModel->pushTask(cmd);
    worker->addCommand(cmd);
    _reflowTaskList();
}

void MainWindow::_queueTasks(std::vector<std::string> cmds)
{
    for (auto& cmd : cmds) {
        _queueTask(cmd);
    }
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

    // Spin up background thread (2)
    start_rust_processing(this, mediaDir.c_str(), callback_wrapper);

    // Initial population of UI from `theModel`.
    // ui->tmdbApiKey->setText(q(appModel->tmdbApiKey()));
    ui->tmdbModeBtn->setText(q(appModel->tmdbMode()));

    // Initialize task list model
    ui->tasksList->setModel(new QStringListModel());

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

    _reflowDisksTree();
    _reflowShowsTree();
    _reflowTaskList();
    _reflowGcButton();

    ui->disksTree->setRootIsDecorated(false);
    ui->disksTree->setItemsExpandable(false);

    // When selecting an entry on the disks tree, load item in player.
    connect(ui->disksTree->selectionModel(), &QItemSelectionModel::selectionChanged, [&](const QItemSelection &, const QItemSelection &) {
        auto titleId = _getIdForSelectedItemInTree(ui->disksTree);
        auto fileName = get_filename_for_title_id(titleId.c_str());
        std::string path(fileName);
        free_string(fileName);

        // Read position from LineEdit
        try {
            _mRequestedPlayerPosition = std::stoi(ui->seekPos->text().toStdString());
        } catch (...) {}

        player->stop();
        player->setSource(QUrl::fromLocalFile(q(path)));
        player->setPlaybackRate(1.0);
        player->play();
        player->pause();
});
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->ffmpegStatusWidget->hide();
    setMouseTrackingRecursive(this, true);

    ui->filmsTree->hide();

    // Spinner timer
    spinnerTimer = new QTimer(this);
    connect(spinnerTimer, &QTimer::timeout, this, [&]() {
        const QString frames[] = {"|", "/", "-", "\\"};
        spinnerIndex = (spinnerIndex + 1) % 4;
        ui->spinnerLabel->setText(frames[spinnerIndex]);
    });

    // Spin up background thread
    worker = new CommandWorker();
    auto *thread = new QThread();
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &CommandWorker::processQueue);
    thread->start();

    // Connect media player
    audioOutput = new QAudioOutput;
    player = new QMediaPlayer;
    player->setVideoOutput(ui->videoWidget);
    player->setAudioOutput(audioOutput);
    connect(player, &QMediaPlayer::durationChanged, [&](int v) {
        ui->videoSeek->setMaximum(v);
    });
    connect(player, &QMediaPlayer::positionChanged, [&](int v) {
        ui->videoSeek->setValue(v);
        ui->seekPos->setText(q(std::format("{}", v)));
    });
    connect(ui->videoSeek, &QSlider::sliderMoved, [&](int v) {
        player->setPosition(v);
        ui->seekPos->setText(q(std::format("{}", v)));
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
        if (status == QMediaPlayer::BufferedMedia) {
            // Seek video to requested position. Do not allow seeking within 10 seconds of the end
            // of the title, because QMediaPlayer will unload the video upon reaching the end and
            // that can make the experience feel bizarre.
            auto maximum = ui->videoSeek->maximum();
            auto requested = _mRequestedPlayerPosition;
            if (requested > maximum - 10000) {
                requested = maximum - 10000;
            }
            player->setPosition(requested);
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

        QMenu menu;
        QAction * deleteAction = menu.addAction(q("Delete Title"));
        QAction * unDeleteAction = menu.addAction(q("Undelete Title"));

        connect(deleteAction, &QAction::triggered, [&]() {
            // Stop the media player, if we remove the file it's accessing we'll segfault.
            player->stop();
            player->setSource(QUrl());

            auto titleId = _getIdForSelectedItemInTree(ui->disksTree);
            auto cmds = appModel->getCommandsToDeleteFileForTitle(titleId);
            _queueTasks(cmds);
        });

        connect(unDeleteAction, &QAction::triggered, [&]() {
            auto titleId = _getIdForSelectedItemInTree(ui->disksTree);
            auto cmds = appModel->getCommandsToUnDeleteFileForTitle(titleId);
            _queueTasks(cmds);
        });

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
        QAction * uploadAction = menu.addAction(q("Upload Show (rsync)"));

        if (episodeId.empty()) {
            auto deleteMenu = menu.addMenu(q("Remove Metadata"));
            auto deleteShow = deleteMenu->addAction(q("Remove Show"));
            connect(deleteShow, &QAction::triggered, [this, showId]() {
                delete_tv_show(showId.c_str());
                std::string appModelId = showId;
                if (!appModelId.starts_with("show.")) appModelId = "show." + appModelId;
                appModel->removeLocalShow(appModelId);
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

            auto deleteMenu = menu.addMenu(q("Remove Metadata"));
            auto deleteShow = deleteMenu->addAction(q("Remove Show"));
            auto deleteSeason = deleteMenu->addAction(q(std::format("Remove {}", seasonText.toStdString())));

            connect(deleteShow, &QAction::triggered, [this, showId]() {
                delete_tv_show(showId.c_str());
                std::string appModelId = showId;
                if (!appModelId.starts_with("show.")) appModelId = "show." + appModelId;
                appModel->removeLocalShow(appModelId);
            });

            connect(deleteSeason, &QAction::triggered, [this, episodeId]() {
                std::string appModelId = episodeId;
                if (!appModelId.starts_with("ep.")) appModelId = "ep." + appModelId;
                if (!appModel->hasEpisode(appModelId)) return;
                auto episode = appModel->episodeById(appModelId);

                delete_tv_season(episode.showId.c_str(), episode.season);
                appModel->removeLocalSeason(episode.showId, episode.season);
            });

            connect(confirmAction, &QAction::triggered, [this, episodeId]() {
                std::string appModelId = episodeId;
                if (!appModelId.starts_with("ep.")) appModelId = "ep." + appModelId;
                appModel->confirmPlays(appModelId);
                confirm_tv_episode_plays(episodeId.c_str());
                _reflowGcButton();
            });

            connect(unidentifyAction, &QAction::triggered, [episodeId]() {
                unidentify_tv_episode(episodeId.c_str());
            });

            connect(reencodeAction, &QAction::triggered, [this, episodeId]() {
                reencode_tv_episode(episodeId.c_str());
                this->ffmpegQueueCount++;
                this->_updateFfmpegStatus();
            });
        }

        connect(uploadAction, &QAction::triggered, [showId]() {
            rsync_show(showId.c_str());
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
        QAction * uploadAction = menu.addAction(q("Upload Film (rsync)"));

        std::string filmVideoId;
        if (index.parent().isValid()) {
            filmVideoId = id;
        }

        if (!filmVideoId.empty()) {
            auto confirmAction = menu.addAction(q("Confirm Plays"));
            connect(confirmAction, &QAction::triggered, [filmVideoId]() {
                confirm_film_video_plays(filmVideoId.c_str());
            });
        }

        connect(uploadAction, &QAction::triggered, [filmId]() {
            rsync_show(filmId.c_str());
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
        auto idEdit = ui->tmdbId->text().toStdString();
        auto apiKeyEdit = ui->tmdbApiKey->text().toStdString();
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

    connect(ui->execBtn, &QPushButton::clicked, [&]() {
        rename_identified();
    });

    connect(ui->gcBtn, &QPushButton::clicked, [&]() {
        _queueTasks(appModel->getCommandsToCollectGarbage());
    });

    connect(worker, &CommandWorker::commandCompleted, this, [&]() {
        appModel->popTask();
        _reflowTaskList();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::reflowAll, this, [&]() {
        _reflowDisksTree();
        _reflowShowsTree();
        _reflowGcButton();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::reflowDisksTree, this, [&]() {
        _reflowDisksTree();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::reflowShowsTree, this, [&]() {
        _reflowShowsTree();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::reflowGcButton, this, [&]() {
        _reflowGcButton();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::scanLocalTitles, this, [&]() {
        appModel->scanLocalTitles();
        _reflowDisksTree();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::scanLocalEpisodes, this, [&]() {
        appModel->scanLocalEpisodes();
        _reflowShowsTree();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::clearTrees, this, [&]() {
        ui->disksTree->model()->removeRows(0, ui->disksTree->model()->rowCount());
        ui->showsTree->model()->removeRows(0, ui->showsTree->model()->rowCount());
        ui->filmsTree->model()->removeRows(0, ui->filmsTree->model()->rowCount());
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::scanLocalTmdbData, this, [&]() {
        appModel->scanLocalTmdbData("*");
        _reflowShowsTree();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::removeLocalSeason, this, [&](std::string showId, int seasonNumber) {
        appModel->removeLocalSeason(showId, seasonNumber);
        _reflowShowsTree();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::removeLocalShow, this, [&](std::string showId) {
        appModel->removeLocalShow(showId);
        _reflowShowsTree();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::scanFilesystemForShow, this, [&](int showId) {
        appModel->scanLocalTmdbData(std::format("tv/{}.json", showId));
        _reflowShowsTree();
        if (showId == 0) {
            return;
        }

        auto id = std::format("show.{}", showId);
        if (!appModel->hasShow(id)) {
            qDebug() << "Show" << id << "not found, no seasons to pull";
            return;
        };
        auto show = appModel->showById(id);

        for (auto seasonNr : show.seasons) {
            auto cmd = std::format(
                "curl https://api.themoviedb.org/3/tv/{}/season/{}.json --header \"Authorization: bearer {}\" -o {}/{}-S{:02}.json",
                showId,
                seasonNr,
                ui->tmdbApiKey->text().toStdString(), // It would be nice to save this when the user starts fetching so they can't mess it up.
                appModel->tvDirectory().string(),
                showId,
                seasonNr
            );

            _queueTask(cmd);
            _queueTask("_scanFsForShow 0"); // TODO: make scanFsForAll or something
        }
    }, Qt::QueuedConnection);
}

MainWindow::~MainWindow()
{
    delete ui;
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
    if (appModel->getCurrentDragXOffset() == 0) {
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

void MainWindow::_updateFfmpegStatus() {
    if (ffmpegActiveCount > 0 || ffmpegQueueCount > 0) {
        ui->ffmpegStatusWidget->show();
        if (ffmpegActiveCount > 0) {
            spinnerTimer->start(250);
            ui->statusLabel->setText(q(std::format("Encoding: {} ({} in queue)", currentEncodingFile, ffmpegQueueCount)));
        } else {
            spinnerTimer->stop();
            ui->spinnerLabel->setText("-");
            ui->statusLabel->setText(q(std::format("Waiting: {} jobs in queue", ffmpegQueueCount)));
        }
    } else {
        ui->ffmpegStatusWidget->hide();
        spinnerTimer->stop();
    }
}

void MainWindow::processMessage(std::string message) {
    qDebug() << "[ cpp] incoming message:" << message;
    if (message == "\"WorkerReady\"") {
        initial_load();
    } else if (message == "\"RecalledConfirmedTmdbApiKey\"") {
        _hideTmdbApiKeyInput();
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
            auto req = m["CommandStarted"];
            if (req.contains("ReencodeRequest")) {
                std::string id = req["ReencodeRequest"][0].get<std::string>();
                auto fileName = get_filename_for_tv_episode_id(id.c_str());
                if (fileName) {
                    currentEncodingFile = fileName;
                    free_string(fileName);
                }
                
                ffmpegQueueCount = std::max(0, ffmpegQueueCount - 1);
                ffmpegActiveCount++;
                _updateFfmpegStatus();
            }
        }
    } catch (...) {}

    try {
        if (m.contains("CommandCompleted")) {
            auto req = m["CommandCompleted"];
            if (req.contains("ReencodeRequest")) {
                ffmpegActiveCount = std::max(0, ffmpegActiveCount - 1);
                if (ffmpegActiveCount == 0) {
                    currentEncodingFile = "";
                }
                _updateFfmpegStatus();
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
        auto tree = m["RemoveTreeItemById"]["tree"].get<std::string>();
        auto id = m["RemoveTreeItemById"]["id"].get<std::string>();

        _removeTreeItemById(tree, id);
    } catch (...) {}

    try {
        auto garbageSize = m["ChangeGarbageSize"]["size"].get<std::uint64_t>();
        _changeGarbageSize(garbageSize);
    } catch (...) {}
}

void callback_wrapper(void* ptr, const char* message) {
    std::string msg(message);
    if (auto* client = static_cast<MainWindow*>(ptr)) {
        QMetaObject::invokeMethod(client, "processMessage", Qt::QueuedConnection, Q_ARG(std::string, msg));
    }
}
