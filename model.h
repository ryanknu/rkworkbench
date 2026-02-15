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
    AppModel(std::string outDir);
    std::filesystem::path configDirPath();
    std::filesystem::path workingDirPath();
    std::string tmdbMode() const;
    std::string tmdbApiKey();
    void setTmdbApiKey(std::string apiKey);
    void toggleTmdbMode();
    std::vector<std::string> *tasks();
    void pushTask(std::string task);
    void popTask();
    std::vector<Show*> shows(); // Probably not needed, use showById instead. It also shouldn't know how
    // show and episode are linked, we should do episodesByShow(Show) and showByEpisode(Episode).
    std::vector<Episode*> episodes(); // These are likely to be immediately sorted, so, letting the caller
    // own the reference is ideal.
    std::vector<RippedTitle*> titles();
    bool hasShow(const std::string& id);
    bool hasEpisode(const std::string& id);
    bool hasTitle(const std::string& id);
    Show& showById(const std::string& id);
    Episode& episodeById(const std::string& id);
    RippedTitle& titleById(const std::string& id);
    void setPreprocessorCommand(std::string cmd);
    void identifyEpisode(const std::string& titleId, std::string showId);
    bool isIdentified(const std::string& item) const;
    void confirmPlays(std::string& episodeId);
    bool isConfirmedPlays(const std::string& episodeId) const;
    std::vector<std::string> generateJobsFromState();
    int queuedAndPendingJobs();
    void scanLocalTmdbData();
    void scanLocalTitles();
    void scanLocalEpisodes();
    bool showHasLocalFile(std::string showName, std::string seasonKey);
    std::vector<std::string> getCommandsToDeleteFileForTitle(const std::string& titleId);
    std::vector<std::string> getCommandsToDeleteSeason(const std::string& showId, int seasonNumber);
    std::vector<std::string> getCommandsToUnDeleteFileForTitle(const std::string& titleId);
    std::vector<std::string> getCommandsToUploadEntireShow(const std::string& showId);
    std::vector<std::string> getCommandsToCollectGarbage();
    int requestedPosition();
    void setRequestedPosition(int position);
    bool canGarbageCollect();
    std::string getGarbageCollectableBytes();

    // I need to expose these because the UI generates cURL commands...
    // Maybe a better pattern is to have appModel generate them.
    std::filesystem::path tvDirectory() const;
    std::filesystem::path filmDirectory() const;
    std::filesystem::path outputDirectory() const;

private:
    // Media data
    TmdbMode _mTmdbMode;
    std::unordered_map<std::string, std::unique_ptr<Show>> _mShows;
    std::unordered_map<std::string, std::unique_ptr<Episode>> _mEpisodes;
    std::unordered_map<std::string, std::unique_ptr<RippedTitle>> _mTitles;
    std::unordered_map<std::string, std::filesystem::path> _mLocalEpisodes;
    std::unordered_map<std::string, std::string> _mIdentifiedEpisodes;
    std::vector<std::string> _mConfirmedEpisodes;

    // Configurations
    std::filesystem::path _mHomeDirPath;
    std::filesystem::path _mConfigDirPath;
    std::filesystem::path _mWorkingDirPath;
    std::string _mTmdbApiKey;
    std::string _mPreprocessorCommand;

    // Task worker
    std::vector<std::string> _mTasks;
    int _mQueuedAndPendingJobs = 0;

    // Media player
    int _mRequestedPosition = 0;

    void _createDirectories() const;
    void _readApiKey();
    void _writeApiKey() const;
    void _readWorkingDir(std::string outDir);
    void _writeWorkingDir() const;
};

class RippedTitle
{
public:
    RippedTitle(std::filesystem::path path, std::uintmax_t size, std::string diskName, std::string titleName);
    std::string id;
    std::string diskName();
    std::string friendlyTitle();
    std::filesystem::path path();
    [[nodiscard]] std::uintmax_t size() const;
    bool isDeleted();
    bool operator<(const RippedTitle& other) const;

private:
    std::filesystem::path _mPath;
    std::uintmax_t _mSize;
    std::string _mDiskName;
    std::string _mTitleName;
    static std::atomic<std::uint64_t> _mIdSequence;
};

class Show
{
    public:
        Show(std::string _id, int _number, std::string title);
        std::string id;
        int number;
        std::vector<int> seasons;
        std::string title;

        void pushSeason(int season);
};

class Episode
{
    public:
        Episode(std::string _id, int _season, int _number, std::string _showId, std::string _title);
        std::string id;
        int season;
        int number;
        std::string showId;

        std::string title;
        std::string seasonKey();
        std::string friendlyTitle();
        bool operator<(const Episode& other) const;
        // int duration
        // video enc
        // resolution
};
