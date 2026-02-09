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
        std::vector<std::string> tasks();
        void pushTask(std::string task);
        std::string popTask();
        std::vector<Show> *shows();
        std::vector<Episode> *episodes();
        std::vector<RippedTitle> *titles();
        void setPreprocessorCommand(std::string cmd);
        void identifyEpisode(std::string diskItem, std::string tmdbItem);
        bool isIdentified(std::string item);

    private:
        TmdbMode _mTmdbMode;
        std::vector<Show> _mShows;
        std::vector<Episode> _mEpisodes;
        std::vector<RippedTitle> _mTitles;
        std::vector<std::string> _mTasks;
        std::unordered_map<std::string, std::string> _mIdentifiedEpisodes;

        std::filesystem::path _mConfigDirPath;
        std::filesystem::path _mWorkingDirPath;
        std::string _mTmdbApiKey;
        std::string _mPreprocessorCommand;

        std::atomic<std::uint64_t> _mIdSequence{1};

        void _createDirectories();
        RippedTitle* _newRippedTitle(std::filesystem::path path, std::uintmax_t size, std::string diskName, std::string titleName);
        void _readApiKey();
        void _writeApiKey();
        std::filesystem::path _tvDirectory();
        std::filesystem::path _filmDirectory();
        void _scanLocalTmdbData();
        void _scanLocalTitles();
};

class RippedTitle
{
    public:
        RippedTitle(std::uint64_t _id, std::filesystem::path path, std::uintmax_t size, std::string diskName, std::string titleName);
        std::uint64_t id;
        std::string diskName();
        std::string friendlyTitle();

    private:
        std::filesystem::path _mPath;
        std::uintmax_t _mSize;
        std::string _mDiskName;
        std::string _mTitleName;
};

class Show
{
    public:
        Show(int _id, int _seasons, std::string title);
        int id;
        int seasons;

        std::string title;
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
