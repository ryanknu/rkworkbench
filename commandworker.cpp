#include "commandworker.h"
#include <filesystem>
#include <sstream>

extern "C" void initial_load();
extern "C" void delete_tv_show(const char* show_id);
extern "C" void delete_tv_season(const char* show_id, size_t season_number);

namespace fs = std::filesystem;

CommandWorker::CommandWorker()
{

}

int CommandWorker::queuedAndPendingJobCount()
{
    return commandQueue.size() + runningACommand;
}

void CommandWorker::addCommand(const std::string& cmd)
{
    {
        auto copy = cmd;
        std::lock_guard<std::mutex> lock(queueMutex);
        commandQueue.push(copy);
    }
    condVar.notify_one();
}

void CommandWorker::processQueue()
{
    while (true) {
        std::string cmd;
        {
            std::unique_lock lock(queueMutex);
            condVar.wait(lock, [this] { return !commandQueue.empty() || !running; });
            if (!running && commandQueue.empty()) break;
            cmd = commandQueue.front();
            commandQueue.pop();
        }

        if (cmd == "_reflowAll") {
            emit reflowAll();
        } else if (cmd == "_reflowDisksTree") {
            emit reflowDisksTree();
        } else if (cmd == "_reflowShowsTree") {
            emit reflowShowsTree();
        } else if (cmd == "_reflowGcButton") {
            emit reflowGcButton();
        } else if (cmd.starts_with("_removeLocalSeason ")) {
            std::istringstream iss(cmd.substr(19));
            std::string showId;
            int seasonNumber;
            if (iss >> showId >> seasonNumber) {
                emit removeLocalSeason(showId, seasonNumber);
            } else {
                qDebug() << "Invalid arguments for" << cmd;
            }
        } else if (cmd.starts_with("_deleteTvShow ")) {
            delete_tv_show(cmd.substr(14).c_str());
        } else if (cmd.starts_with("_deleteTvSeason ")) {
            std::istringstream iss(cmd.substr(16));
            std::string showId;
            int seasonNumber;
            if (iss >> showId >> seasonNumber) {
                delete_tv_season(showId.c_str(), seasonNumber);
            } else {
                qDebug() << "Invalid arguments for" << cmd;
            }
        } else if (cmd == "_scanLocalTitles") {
            emit scanLocalTitles();
        } else if (cmd == "_scanLocalEpisodes") {
            emit scanLocalEpisodes();
        } else if (cmd == "_clearTrees") {
            emit clearTrees();
        } else if (cmd == "_initialLoad") {
            initial_load();
        } else if (cmd.starts_with("_mkDir ")) {
            fs::path path(cmd.substr(7));
            if (!fs::exists(path)) {
                fs::create_directories(path);
            }
        } else if (cmd.starts_with("_rm ")) {
            fs::path path(cmd.substr(4));
            if (fs::exists(path)) {
                fs::remove(path);
            }
        } else {
            QStringList parts = QProcess::splitCommand(QString::fromStdString(cmd));

            qDebug() << "(CommandWorker) Executing:" << parts.first() << "Arguments:" << parts.mid(1);

            runningACommand = 1;
            QProcess::execute(parts.first(), parts.mid(1));
            runningACommand = 0;
        }

        emit commandCompleted();
    }
}

void CommandWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        running = false;
    }
    condVar.notify_one();
}
