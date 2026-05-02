#pragma once
#include <QObject>
#include <QThread>
#include <QProcess>
#include <QDebug>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>

class CommandWorker : public QObject
{
    Q_OBJECT

public:
    void addCommand(const std::string& cmd);
    int queuedAndPendingJobCount();
    CommandWorker();

public slots:
    void processQueue();
    void stop();

signals:
    void commandCompleted();
    void reflowAll();
    void reflowDisksTree();
    void reflowShowsTree();
    void reflowGcButton();
    void removeLocalSeason(std::string showId, int seasonNumber);
    void removeLocalShow(std::string showId);
    void scanFilesystemForShow(int showId);
    void scanLocalTitles();
    void scanLocalEpisodes();
    void clearTrees();
    void scanLocalTmdbData(std::string filter);

private:
    std::queue<std::string> commandQueue;
    std::mutex queueMutex;
    std::condition_variable condVar;
    bool running = true;
    int runningACommand = 0;
};
