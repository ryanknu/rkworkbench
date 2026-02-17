#include "commandworker.h"
#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QDebug>
#include <QThread>
#include <QMenu>
#include <QAction>
#include <fstream>
#include <string>
#include <format>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <QMouseEvent>

namespace fs = std::filesystem;

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

/**
 * Re-renders the disks tree from app model.
 */
void MainWindow::_reflowDisksTree() const {
    auto* disksModel = dynamic_cast<QStandardItemModel *>(ui->disksTree->model());
    disksModel->removeRows(0, disksModel->rowCount());

    std::unordered_map<std::string, QStandardItem*> disks;

    auto titles = appModel->titles();
    std::ranges::sort(titles,
        [](RippedTitle* a, RippedTitle* b) {
            return *a < *b;
        }
    );

    for (auto& title : titles) {
        auto titleItem = new QStandardItem(q(title->friendlyTitle()));
        titleItem->setData(q(title->id), Qt::UserRole);
        if (title->isDeleted()) {
            titleItem->setForeground(QBrush(QColor("red")));
        }
        else if (appModel->isIdentified(title->id)) {
            titleItem->setForeground(QBrush(QColor("orange")));
        }

        if (!disks.contains(title->diskName())) {
            auto diskItem = new QStandardItem(q(title->diskName()));
            diskItem->setSelectable(false);
            disksModel->invisibleRootItem()
                ->appendRow(diskItem);

            diskItem->appendRow(titleItem);
            disks[title->diskName()] = diskItem;
        } else {
            auto diskItem = disks[title->diskName()];
            diskItem->appendRow(titleItem);
        }
    }

    ui->disksTree->expandAll();
}

/**
 * Re-renders the shows tree from app model.
 */
void MainWindow::_reflowShowsTree() const {
    auto* showsModel = dynamic_cast<QStandardItemModel *>(ui->showsTree->model());
    showsModel->removeRows(0, showsModel->rowCount());

    std::unordered_map<std::string, QStandardItem*> showItems;

    // Buffer for episodes for sorting
    auto episodes = appModel->episodes();
    std::ranges::sort(episodes,
        [](Episode* a, Episode* b) {
            return *a < *b;
        }
    );

    for (auto& episode : episodes) {
        if (!appModel->hasShow(episode->showId)) continue;
        auto show = appModel->showById(episode->showId);

        auto episodeItem = new QStandardItem(q(episode->friendlyTitle()));
        episodeItem->setData(q(episode->id), Qt::UserRole);
        if (appModel->isConfirmedPlays(episode->id)) {
            episodeItem->setForeground(QBrush(QColor("cyan")));
        }
        else if (appModel->showHasLocalFile(show.title, episode->seasonKey())) {
            episodeItem->setForeground(QBrush(QColor("green")));
        }
        else if (appModel->isIdentified(std::format("{}", episode->id))) {
            episodeItem->setForeground(QBrush(QColor("orange")));
        }

        if (!showItems.contains(show.id)) {
            auto showItem = new QStandardItem(q(show.title));
            showItem->setSelectable(false);
            showsModel->invisibleRootItem()
                ->appendRow(showItem);

            showItem->appendRow(episodeItem);
            showItems.emplace(show.id, showItem);
        } else {
            auto showItem = showItems.at(show.id);
            showItem->appendRow(episodeItem);
        }
    }

    ui->showsTree->expandAll();
}

void MainWindow::_reflowGcButton() const {
    ui->gcBtn->setText(q(std::format("Collect Garbage ({})", appModel->getGarbageCollectableBytes())));
    ui->gcBtn->setDisabled(!appModel->canGarbageCollect());
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

int MainWindow::_getRequestedPosition() {
    int pos;
    try {
        pos = std::stoi(ui->seekPos->text().toStdString());
    } catch (...) {
        pos = 0;
    }

    return pos;
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

void MainWindow::setAppModel(AppModel *theModel) {
    appModel = theModel;

    // Initial population of UI from `theModel`.
    ui->tmdbApiKey->setText(q(appModel->tmdbApiKey()));
    ui->tmdbModeBtn->setText(q(appModel->tmdbMode()));

    // Initialize task list model
    ui->tasksList->setModel(new QStringListModel());

    // Initialize tree models
    auto* disksModel = new QStandardItemModel(this);
    auto* showsModel = new QStandardItemModel(this);
    disksModel->setHorizontalHeaderLabels({ "Disks" });
    showsModel->setHorizontalHeaderLabels({ "Shows" });
    ui->disksTree->setModel(disksModel);
    ui->showsTree->setModel(showsModel);

    _reflowDisksTree();
    _reflowShowsTree();
    _reflowTaskList();
    _reflowGcButton();

    ui->disksTree->setRootIsDecorated(false);
    ui->disksTree->setItemsExpandable(false);

    // When selecting an entry on the disks tree, load item in player.
    connect(ui->disksTree->selectionModel(), &QItemSelectionModel::selectionChanged, [&](const QItemSelection &, const QItemSelection &) {
        auto titleId = _getIdForSelectedItemInTree(ui->disksTree);

        if (!appModel->hasTitle(titleId)) return;
        auto title = appModel->titleById(titleId);

        if (_getRequestedPosition() != 0) {
            appModel->setRequestedPosition(_getRequestedPosition());
        }

        player->setSource(QUrl::fromLocalFile(q(title.path().string())));
        player->play();
        player->pause();
});
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setMouseTrackingRecursive(this, true);

    // Spin up background thread
    worker = new CommandWorker();
    auto *thread = new QThread();
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &CommandWorker::processQueue);
    thread->start();

    // Connect media player
    player = new QMediaPlayer;
    player->setVideoOutput(ui->videoWidget);
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
            auto requested = appModel->requestedPosition();
            if (requested > maximum - 10000) {
                requested = maximum - 10000;
            }
            player->setPosition(requested);
        }
    });

    // Set context menu on titles
    ui->disksTree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->disksTree, &QWidget::customContextMenuRequested, [&](const QPoint &pos) {
        // TODO: See if this works with the selectedItem helper fn
        auto index = ui->disksTree->indexAt(pos);
        if (!index.isValid()) {
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
        auto episodeId = _getIdForSelectedItemInTree(ui->showsTree);
        if (!appModel->hasEpisode(episodeId)) return;
        auto episode = appModel->episodeById(episodeId);
        auto episodeText = episode.season == 0 ? "specials" : std::format("season {:02}", episode.season);

        QMenu menu;
        QAction * uploadAction = menu.addAction(q("Upload Show (rsync)"));
        auto confirmAction = menu.addAction(q("Confirm Plays (not implemented)"));

        // TODO: Upload Episode, disabled if not green
        //       Make Delete show and season work.
        auto deleteMenu = menu.addMenu(q("Delete Stuff"));
        deleteMenu->addAction(q("Delete Show (not implemented)"));
        auto deleteSeason = deleteMenu->addAction(q(std::format("Delete {}", episodeText)));

        connect(deleteSeason, &QAction::triggered, [&]() {
            auto episodeId = _getIdForSelectedItemInTree(ui->showsTree);
            if (!appModel->hasEpisode(episodeId)) return;
            auto episode = appModel->episodeById(episodeId);

            _queueTasks(appModel->getCommandsToDeleteSeason(episode.showId, episode.season));
        });

        connect(uploadAction, &QAction::triggered, [&]() {
            auto episodeId = _getIdForSelectedItemInTree(ui->showsTree);
            if (!appModel->hasEpisode(episodeId)) return;
            auto episode = appModel->episodeById(episodeId);

            _queueTasks(
                appModel->getCommandsToUploadEntireShow(episode.showId)
            );
        });

        connect(confirmAction, &QAction::triggered, [&]() {
            auto episodeId = _getIdForSelectedItemInTree(ui->showsTree);
            appModel->confirmPlays(episodeId);
            _reflowShowsTree();
        });

        menu.exec(ui->showsTree->viewport()->mapToGlobal(pos));
    });

    // Button handlers
    connect(ui->playBtn, &QPushButton::clicked, [&]() {
        player->play();
    });

    connect(ui->pauseBtn, &QPushButton::clicked, [&]() {
        player->pause();
    });

    connect(ui->tmdbFetchBtn, &QPushButton::clicked, [&]() {
        // Check if response is already on disk.
        auto showId = ui->tmdbId->text().toStdString();
        bool isTv = ui->tmdbModeBtn->text() == "TV";
        auto subdir = isTv ? appModel->tvDirectory() : appModel->filmDirectory();
        std::ifstream t(subdir / (ui->tmdbId->text().toStdString() + ".json"));
        std::stringstream buffer;
        buffer << t.rdbuf();
        auto json = buffer.str();
        if (json.length() > 0) {
            qDebug() << "Data from file: " << json;
            return;
        }

        // Save the API key
        appModel->setTmdbApiKey(ui->tmdbApiKey->text().toStdString());

        // Make the cmd
        auto cmd = std::format(
            "curl https://api.themoviedb.org/3/{}/{} --header \"Authorization: bearer {}\" -o {}/{}.json",
            isTv ? "tv" : "movie",
            ui->tmdbId->text().toStdString(),
            ui->tmdbApiKey->text().toStdString(),
            subdir.string(),
            ui->tmdbId->text().toStdString()
        );

        _queueTask(cmd);
        if (isTv) {
            _queueTask(format("_scanFsForShow {}", showId));
        }
    });

    connect(ui->tmdbModeBtn, &QPushButton::clicked, [&]() {
        appModel->toggleTmdbMode();
        ui->tmdbModeBtn->setText(q(appModel->tmdbMode()));
    });

    connect(ui->identifyBtn, &QPushButton::clicked, [&]() {
        auto showId = _getIdForSelectedItemInTree(ui->showsTree);
        auto titleId = _getIdForSelectedItemInTree(ui->disksTree);

        appModel->identifyEpisode(titleId, showId);

        _reflowDisksTree();
        _reflowShowsTree();
    });

    connect(ui->execBtn, &QPushButton::clicked, [&]() {
        auto jobs = appModel->generateJobsFromState();
        _queueTasks(jobs);
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

    connect(worker, &CommandWorker::scanLocalTmdbData, this, [&]() {
        appModel->scanLocalTmdbData("*");
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
        }
        if (treesMask & 2) {
            qDebug() << "Setting disksTree to" << appModel->getCurrentDisksTreeWidth();
            ui->disksTree->setMaximumWidth(appModel->getCurrentDisksTreeWidth());
        }
    }

    QWidget::mouseMoveEvent(event);
}