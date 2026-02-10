#include "commandworker.h"
#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QDebug>
#include <QThread>
#include <QMenu>
#include <fstream>
#include <string>
#include <format>
#include <unordered_map>

namespace fs = std::filesystem;

QString q(std::string str)
{
    return QString::fromStdString(str);
}

/**
 * Redraws the UI for the two trees from data in appModel
 * I'm aware this pattern has flaws.
 */
void MainWindow::_reflowTrees()
{
    // Get the tree models
    auto* showsModel = dynamic_cast<QStandardItemModel *>(ui->showsTree->model());
    auto* disksModel = dynamic_cast<QStandardItemModel *>(ui->disksTree->model());

    // Empty them out.
    showsModel->removeRows(0, showsModel->rowCount());
    disksModel->removeRows(0, disksModel->rowCount());

    // Populate show listing
    for (auto& entry : *appModel->shows()) {
        auto showId = entry.id;
        auto showItem = new QStandardItem(q(entry.title));

        showsModel->invisibleRootItem()
            ->appendRow(showItem);

        for (auto& entry : *appModel->episodes()) {
            if (showId == entry.showId) {
                auto episodeItem = new QStandardItem(q(entry.friendlyTitle()));
                if (appModel->isIdentified(std::format("{}", entry.id))) {
                    episodeItem->setForeground(QBrush(QColor("orange")));
                }
                episodeItem->setData(q(std::format("{}", entry.id)), Qt::UserRole);
                showItem->appendRow(episodeItem);
            }
        }
    }

    // Populate disk listing
    std::unordered_map<std::string, QStandardItem*> disks;
    for (auto& title : *appModel->titles()) {
        auto titleItem = new QStandardItem(q(title.friendlyTitle()));
        titleItem->setData(q(std::format("{}", title.id)), Qt::UserRole);
        if (appModel->isIdentified(std::format("{}", title.id))) {
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
}

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent), ui(new Ui::MainWindow)
{
	ui->setupUi(this);

	// Spin up background thread
	worker = new CommandWorker();
	QThread *thread = new QThread();
	worker->moveToThread(thread);
	connect(thread, &QThread::started, worker, &CommandWorker::processQueue);
	thread->start();

	// Set context menu
	ui->showsTree->setContextMenuPolicy(Qt::CustomContextMenu);

	QObject::connect(ui->showsTree, &QWidget::customContextMenuRequested, [&](const QPoint &pos) {
	    auto index = ui->showsTree->indexAt(pos);
		if (!index.isValid()) {
		    return;
		}

		QMenu menu;
		menu.addAction(q("Upload")); // processed (green)
		auto deleteMenu = menu.addMenu(q("Delete Stuff"));
		deleteMenu->addAction(q("Delete Show"));
		deleteMenu->addAction(q("Delete Season"));

		menu.exec(ui->showsTree->viewport()->mapToGlobal(pos));
	});

	// Button handlers
	QObject::connect(ui->playBtn, &QPushButton::clicked, [&]() {
        QModelIndex index = ui->disksTree->currentIndex();
        if (index.isValid()) {
            QVariant data = index.model()->data(index, Qt::UserRole);
            QString text = data.toString();

            video = new Phonon::MediaSource(text);

            ui->videoPlayer->play(*video);
        }
    });

	QObject::connect(ui->tmdbFetchBtn, &QPushButton::clicked, [&]() {
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

    QObject::connect(ui->tmdbModeBtn, &QPushButton::clicked, [&]() {
        appModel->toggleTmdbMode();
        ui->tmdbModeBtn->setText(q(appModel->tmdbMode()));
    });

    QObject::connect(ui->identifyBtn, &QPushButton::clicked, [&]() {
        auto showId = _getIdForSelectedItemInTree(ui->showsTree);
        auto titleId = _getIdForSelectedItemInTree(ui->disksTree);

        appModel->identifyEpisode(titleId, showId);

        _reflowTrees();
    });

    QObject::connect(ui->execBtn, &QPushButton::clicked, [&]() {
        appModel->enqueueAllJobs();
        _reflowTaskList();
    });

    QObject::connect(worker, &CommandWorker::commandCompleted, [&]() {
        appModel->popTask();
        _reflowTaskList();
    });

    QObject::connect(worker, &CommandWorker::reflowAll, [&]() {
        _reflowTrees();
        _reflowTaskList();
    });

    QObject::connect(worker, &CommandWorker::scanFilesystemForShow, [&](int showId) {
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
