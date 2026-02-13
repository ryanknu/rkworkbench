#include "commandworker.h"

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
            std::unique_lock<std::mutex> lock(queueMutex);
            condVar.wait(lock, [this] { return !commandQueue.empty() || !running; });
            if (!running && commandQueue.empty()) break;
            cmd = commandQueue.front();
            commandQueue.pop();
        }

        if (cmd == "_reflowAll") {
            emit reflowAll();
        } else if (cmd == "_scanLocalTitles") {
            emit scanLocalTitles();
        } else if (cmd.starts_with("_scanFsForShow ")) {
            int showId = std::stoi(cmd.substr(15));
            emit scanFilesystemForShow(showId);
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
