#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <atomic>

class Episode;
class RippedTitle;
class Show;

enum TmdbMode {
    TV,
    FILM,
};

class AppModel {
    public:
        AppModel();
        std::filesystem::path configDirPath();
        std::filesystem::path workingDirPath();
        std::string tmdbMode();
        std::string tmdbApiKey();
        void setTmdbApiKey(std::string apiKey);
        void toggleTmdbMode();
        std::vector<std::string> *tasks();
        void pushTask(std::string task);
        void popTask();
        std::vector<Show> *shows();
        std::vector<Episode> *episodes();
        std::vector<RippedTitle> *titles();
        void setPreprocessorCommand(std::string cmd);
        void identifyEpisode(std::string titleId, std::string showId);
        bool isIdentified(std::string item);
        void enqueueAllJobs();
        int queuedAndPendingJobs();
        void scanLocalTmdbData();

        // I need to expose these because the UI generates cURL commands...
        // Maybe a better pattern is to have appModel generate them.
        std::filesystem::path tvDirectory();
        std::filesystem::path filmDirectory();

    private:
        // Media data
        TmdbMode _mTmdbMode;
        std::vector<Show> _mShows;
        std::vector<Episode> _mEpisodes;
        std::vector<RippedTitle> _mTitles;
        std::unordered_map<std::string, std::string> _mIdentifiedEpisodes;

        // Configurations
        std::filesystem::path _mConfigDirPath;
        std::filesystem::path _mWorkingDirPath;
        std::string _mTmdbApiKey;
        std::string _mPreprocessorCommand;

        // Task worker
        std::vector<std::string> _mTasks;
        int _mQueuedAndPendingJobs = 0;

        // Sequences
        std::atomic<std::uint64_t> _mIdSequence{1};

        void _createDirectories();
        RippedTitle* _newRippedTitle(std::filesystem::path path, std::uintmax_t size, std::string diskName, std::string titleName);
        void _readApiKey();
        void _writeApiKey();
        void _scanLocalTitles();
};

class RippedTitle
{
    public:
        RippedTitle(std::uint64_t _id, std::filesystem::path path, std::uintmax_t size, std::string diskName, std::string titleName);
        std::uint64_t id;
        std::string diskName();
        std::string friendlyTitle();
        std::filesystem::path path();

    private:
        std::filesystem::path _mPath;
        std::uintmax_t _mSize;
        std::string _mDiskName;
        std::string _mTitleName;
};

class Show
{
    public:
        Show(int _id, std::string title);
        int id;
        std::vector<int> seasons;
        std::string title;

        void pushSeason(int season);
};

class Episode
{
    public:
        Episode(int _id, int _season, int _number, int _showId, std::string _title);
        int id;
        int season;
        int number;
        int showId;

        std::string title;
        std::string seasonKey();
        std::string friendlyTitle();
        // int duration
};

    // class Entry {
    //     private:
    //         std::string id;
    //         Entry* _mParentEntry;
    //         std::string text;
    //         std::filesystem::path file;
    // };
