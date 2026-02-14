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

namespace fs = std::filesystem;

QString q(const std::string& str)
{
    return QString::fromStdString(str);
}

/**
 * Redraws the UI for the two trees from data in appModel
 * I'm aware this pattern has flaws.
 */
void MainWindow::_reflowTrees() const {
    // Get the tree models
    auto* showsModel = dynamic_cast<QStandardItemModel *>(ui->showsTree->model());
    auto* disksModel = dynamic_cast<QStandardItemModel *>(ui->disksTree->model());

    // Empty them out.
    showsModel->removeRows(0, showsModel->rowCount());
    disksModel->removeRows(0, disksModel->rowCount());

    // Populate show listing
    for (const auto& show : *appModel->shows()) {
        const auto showId = show.id;
        auto showName = show.title;
        const auto showItem = new QStandardItem(q(showName));

        showsModel->invisibleRootItem()
            ->appendRow(showItem);

        for (auto& episode : *appModel->episodes()) {
            if (showId == episode.showId) {
                auto episodeItem = new QStandardItem(q(episode.friendlyTitle()));
                if (appModel->showHasLocalFile(showName, episode.seasonKey())) {
                    episodeItem->setForeground(QBrush(QColor("green")));
                }
                else if (appModel->isIdentified(std::format("{}", episode.id))) {
                    episodeItem->setForeground(QBrush(QColor("orange")));
                }
                episodeItem->setData(q(std::format("{}", episode.id)), Qt::UserRole);
                showItem->appendRow(episodeItem);
            }
        }
    }

    // Populate disk listing
    std::unordered_map<std::string, QStandardItem*> disks;
    for (auto& title : *appModel->titles()) {
        auto titleItem = new QStandardItem(q(title.friendlyTitle()));
        titleItem->setData(q(std::format("{}", title.id)), Qt::UserRole);
        if (title.path().string().ends_with(".d")) {
            titleItem->setForeground(QBrush(QColor("red")));
        }
        else if (appModel->isIdentified(std::format("{}", title.id))) {
            titleItem->setForeground(QBrush(QColor("orange")));
        }

        if (!disks.contains(title.diskName())) {
            auto diskItem = new QStandardItem(q(title.diskName()));
            diskItem->setSelectable(false);
            disksModel->invisibleRootItem()
                ->appendRow(diskItem);

            diskItem->appendRow(titleItem);
            disks[title.diskName()] = diskItem;
        } else {
            auto diskItem = disks[title.diskName()];
            diskItem->appendRow(titleItem);
        }
    }

    ui->showsTree->expandAll();
    ui->disksTree->expandAll();
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

    _reflowTrees();
    _reflowTaskList();

    ui->disksTree->setRootIsDecorated(false);
    ui->disksTree->setItemsExpandable(false);

    // When selecting an entry on the disks tree, load item in player.
    connect(ui->disksTree->selectionModel(), &QItemSelectionModel::selectionChanged, [&](const QItemSelection &, const QItemSelection &) {
        auto titleId = _getIdForSelectedItemInTree(ui->disksTree);
        for (auto& title : *appModel->titles()) {
            if (std::format("{}", title.id) == titleId) {
                if (_getRequestedPosition() != 0) {
                    appModel->setRequestedPosition(_getRequestedPosition());
                }

                player->setSource(QUrl::fromLocalFile(q(title.path().string())));
                player->play();
                player->pause();
            }
        }
    });
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

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
            player->setPosition(appModel->requestedPosition());
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
        // TODO: See if this works with the selectedItem helper fn
        auto index = ui->showsTree->indexAt(pos);
        if (!index.isValid()) {
            return;
        }

        QMenu menu;
        QAction * uploadAction = menu.addAction(q("Upload Show (rsync)"));
        menu.addAction(q("Confirm Plays (not implemented)"));

        // TODO: Upload Episode, disabled if not green
        //       Make Delete show and season work.
        auto deleteMenu = menu.addMenu(q("Delete Stuff"));
        deleteMenu->addAction(q("Delete Show (not implemented)"));
        deleteMenu->addAction(q("Delete Season (not implemented)"));

        connect(uploadAction, &QAction::triggered, [&]() {
            auto episodeId = _getIdForSelectedItemInTree(ui->showsTree);
            for (auto& episode : *appModel->episodes()) {
                if (std::format("{}", episode.id) != episodeId) continue;
                auto showId = episode.showId;
                for (auto& show : *appModel->shows()) {
                    if (show.id != showId) continue;
                    bool isTv = ui->tmdbModeBtn->text() == "TV";
                    auto embyDir = isTv ? "tv" : "movies";
                    auto showDir = appModel->outputDirectory() / show.title;
                    auto cmd = std::format(
                        "rsync -a \"{}/\" \"root@10.4.6.2:/mnt/user/emby/{}/{}/\"",
                        showDir.string(),
                        embyDir,
                        show.title
                    );
                    _queueTask(cmd);
                }
            }
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

        _reflowTrees();
    });

    connect(ui->execBtn, &QPushButton::clicked, [&]() {
        auto jobs = appModel->generateJobsFromState();
        _queueTasks(jobs);
    });

    connect(worker, &CommandWorker::commandCompleted, [&]() {
        appModel->popTask();
        _reflowTaskList();
    });

    connect(worker, &CommandWorker::reflowAll, this, [&]() {
        _reflowTrees();
        _reflowTaskList();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::scanLocalTitles, this, [&]() {
        appModel->scanLocalTitles();
        _reflowTrees();
    }, Qt::QueuedConnection);

    connect(worker, &CommandWorker::scanFilesystemForShow, [&](int showId) {
        appModel->scanLocalTmdbData();
        _reflowTrees();
        if (showId == 0) {
            return;
        }

        for (auto& show : *appModel->shows()) {
            if (show.id == showId) {
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
                break;
            }
        }
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}
