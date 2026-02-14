#include "model.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <QDebug>
#include <nlohmann/json.hpp>
#include <regex>
#include <utility>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::atomic<std::uint64_t> RippedTitle::_mIdSequence = {1};

AppModel::AppModel()
{
    // Defaults
    _mTmdbMode = TV;
    _mPreprocessorCommand = "";
    _mTmdbApiKey = "";

    // Set up working directory
    _mWorkingDirPath = fs::current_path();

    // Set up config directory
    auto home = getenv("HOME");
	_mConfigDirPath = fs::path(home) / ".config" / "rkworkbench";

	// Initialize
	_createDirectories();
	_readApiKey();
	scanLocalTmdbData();
	scanLocalTitles();
}

fs::path AppModel::configDirPath()
{
    return _mConfigDirPath;
}

fs::path AppModel::workingDirPath()
{
    return _mWorkingDirPath;
}

std::string AppModel::tmdbApiKey()
{
    return _mTmdbApiKey;
}

void AppModel::setTmdbApiKey(std::string apiKey)
{
    _mTmdbApiKey = std::move(apiKey);
    _writeApiKey();
}

void AppModel::toggleTmdbMode()
{
    _mTmdbMode = _mTmdbMode == TV ? FILM : TV;
}

std::string AppModel::tmdbMode() const {
    return _mTmdbMode == TV ? "TV" : "Film";
}

fs::path AppModel::tvDirectory() const {
    return _mConfigDirPath / "tv";
}

fs::path AppModel::filmDirectory() const {
    return _mConfigDirPath / "films";
}

fs::path AppModel::outputDirectory() const {
    return _mWorkingDirPath / "output";
}

void AppModel::_createDirectories() const
{
   	fs::create_directories(tvDirectory());
	fs::create_directories(filmDirectory());
	fs::create_directories(outputDirectory());
}

void AppModel::_readApiKey()
{
    std::ifstream t(_mConfigDirPath / "tmdb.key");
    std::stringstream buffer;
    buffer << t.rdbuf();
    _mTmdbApiKey = buffer.str();
}

void AppModel::_writeApiKey() const {
    fs::path of = _mConfigDirPath / "tmdb.key";
    std::ofstream output_file;
    output_file.open(of);
    if (output_file.is_open()) {
        output_file << _mTmdbApiKey;
        output_file.close();
    }
}

/**
 * This method was written by an LLM
 */
bool detectShowJson(const fs::path& p) {
    // 1. Check extension is .json
    if (p.extension() != ".json") return false;

    // 2. Get filename without extension
    std::string stem = p.stem().string();

    // 3. Check if stem is not empty and consists only of digits
    return !stem.empty() && std::ranges::all_of(stem, ::isdigit);
}

/**
 * This method was written by an LLM
 */
bool detectSeasonJson(const fs::path& path) {
    // Get the filename as a string
    const std::string filename = path.filename().string();

    // Regex breakdown:
    // ^        : Start of string
    // \d+      : One or more digits (first integer)
    // -S       : Literal hyphen followed by capital S
    // \d+      : One or more digits (second integer)
    // \.json   : Literal dot followed by "json"
    // $        : End of string
    static const std::regex pattern(R"(^\d+-S\d+\.json$)");

    return std::regex_match(filename, pattern);
}

/**
 * Scans the local filesystem for cached TMBD data.
 */
void AppModel::scanLocalTmdbData()
{
    _mShows.clear();
    _mEpisodes.clear();

    struct TvShowData {
        int id;
        std::string first_air_date;
        std::string name;
    };

    struct TvEpisodeData {
        int id;
        int episode_number;
        int season_number;
        int show_id;
        std::string name;
    };

    for (const auto& entry : fs::directory_iterator(tvDirectory())) {
        if (detectShowJson(entry.path())) {
            std::ifstream ifs(entry.path());
            json jf = json::parse(ifs);

            TvShowData parsed {
                jf["id"].get<int>(),
                jf["first_air_date"].get<std::string>(),
                jf["name"].get<std::string>()
            };

            // WARNING: Show title is used for remote naming convention. Do not change it without changing
            //          all pathing operations.
            auto title = std::format("{} ({}) [tmdb={}]", parsed.name, parsed.first_air_date.substr(0, 4), parsed.id);
            auto show = std::make_unique<Show>(std::format("show.{}", parsed.id), title);

            for (auto& season : jf["seasons"]) {
                show->pushSeason(season["season_number"].get<int>());
            }

            _mShows.emplace(show->id, std::move(show));
        }

        if (detectSeasonJson(entry.path())) {
            std::ifstream ifs(entry.path());
            json jf = json::parse(ifs);

            for (auto& epjson : jf["episodes"]) {
                TvEpisodeData parsed {
                    epjson["id"].get<int>(),
                    epjson["episode_number"].get<int>(),
                    epjson["season_number"].get<int>(),
                    epjson["show_id"].get<int>(),
                    epjson["name"].get<std::string>()
                };

                auto episode = std::make_unique<Episode>(
                    std::format("ep.{}", parsed.id),
                    parsed.season_number,
                    parsed.episode_number,
                    std::format("show.{}", parsed.show_id),
                    parsed.name
                );

                _mEpisodes.emplace(episode->id, std::move(episode));
            }
        }
    }
}

/**
 * Scans the local filesystem for titles.
 */
void AppModel::scanLocalTitles()
{
    _mTitles.clear();
    _mLocalEpisodes.clear();

    for (const auto& entry : fs::directory_iterator(outputDirectory())) {
        if (!entry.is_directory()) {
            continue;
        }

        auto showName = entry.path().filename().string();
        for (const auto& entry : fs::directory_iterator(entry.path())) {
            auto seasonKey = entry.path().filename().string().substr(0, 6);
            _mLocalEpisodes[std::format("{} {}", showName, seasonKey)] = entry.path();
        }
    }

    for (const auto& entry : fs::directory_iterator(_mWorkingDirPath)) {
        if (!entry.is_directory()) {
            continue;
        }

        // Skip the output directory.
        if (entry.path().filename().string() == "output") {
            continue;
        }

        auto diskName = entry.path().filename().string();

        for (const auto& entry : fs::directory_iterator(entry.path())) {
            std::uintmax_t size = fs::file_size(entry.path());

            auto title = std::make_unique<RippedTitle>(
                entry.path(),
                size,
                diskName,
                entry.path().filename().string()
            );

            _mTitles.emplace(title->id, std::move(title));
        }
    }
}

Show::Show(std::string _id, std::string _title)
{
    id = std::move(_id);
    seasons = { };
    title = std::move(_title);
}

void Show::pushSeason(int season)
{
    seasons.push_back(season);
}

Episode::Episode(std::string _id, int _season, int _number, std::string _showId, std::string _title)
{
    id = std::move(_id);
    season = _season;
    number = _number;
    showId = std::move(_showId);
    title = std::move(_title);
}

std::vector<std::string> *AppModel::tasks()
{
    return &_mTasks;
}

std::vector<Show*> AppModel::shows()
{
    std::vector<Show*> ret;
    for (const auto &val: _mShows | std::views::values) {
        ret.push_back(val.get());
    }
    return ret;
}

std::vector<Episode*> AppModel::episodes()
{
    std::vector<Episode*> ret;
    for (const auto &val: _mEpisodes | std::views::values) {
        ret.push_back(val.get());
    }
    return ret;
}

std::vector<RippedTitle*> AppModel::titles()
{
    std::vector<RippedTitle*> ret;
    for (const auto &val: _mTitles | std::views::values) {
        ret.push_back(val.get());
    }
    return ret;
}

std::string Episode::seasonKey()
{
    return std::format("S{:02}E{:02}", season, number);
}

std::string Episode::friendlyTitle()
{
    return std::format("{} - {}", seasonKey(), title);
}

bool Episode::operator<(const Episode& other) const {
    // TODO: Sort by show name first.
    if (other.season == season) {
        if (other.number == number) {
            return std::strcoll(other.title.c_str(), title.c_str()) < 0;
        }
        return number < other.number;
    }
    return season < other.season;
}

bool RippedTitle::operator<(const RippedTitle& other) const {
    if (_mDiskName == other._mDiskName) {
        return std::strcoll(_mTitleName.c_str(), other._mTitleName.c_str()) < 0;
    }
    return std::strcoll(_mDiskName.c_str(), other._mDiskName.c_str()) < 0;
}

void AppModel::setPreprocessorCommand(std::string cmd)
{
    _mPreprocessorCommand = std::move(cmd);
}

void AppModel::identifyEpisode(const std::string& titleId, std::string showId)
{
    _mIdentifiedEpisodes[titleId] = std::move(showId);
}

bool AppModel::isIdentified(const std::string& item) const {
    return std::ranges::any_of(_mIdentifiedEpisodes, [&](const auto& pair) {
        return pair.first == item || pair.second == item;
    });
}

RippedTitle::RippedTitle(fs::path path, std::uintmax_t size, std::string diskName, std::string titleName)
{
    id = std::format("title.{}", _mIdSequence.fetch_add(1));
    _mPath = std::move(path);
    _mSize = size;
    _mDiskName = std::move(diskName);
    _mTitleName = std::move(titleName);
}

std::string RippedTitle::diskName()
{
    return _mDiskName;
}

std::string RippedTitle::friendlyTitle()
{
    double gb = static_cast<double>(_mSize) / (1024.0 * 1024.0 * 1024.0);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << gb << "G";

    return std::format("{} {}", ss.str(), _mTitleName);
}

void AppModel::pushTask(std::string task)
{
    _mTasks.push_back(task);
    _mQueuedAndPendingJobs ++;
}

void AppModel::popTask()
{
    _mQueuedAndPendingJobs --;
}

/**
 * Creates all jobs from the current UI state.
 * *DOES NOT* append them to _mTasks, that is the UI's job.
 */
std::vector<std::string> AppModel::generateJobsFromState()
{
    std::vector<std::string> jobs;
    for (const auto& pair : _mIdentifiedEpisodes) {
        auto titleId = pair.first;
        auto episodeId = pair.second;

        if (!hasTitle(titleId) || !hasEpisode(episodeId)) continue;

        auto title = titleById(titleId);
        auto episode = episodeById(episodeId);

        if (!hasShow(episode.showId)) continue;

        auto show = showById(episode.showId);

        // RK: I don't like this being located here. The show's directory should be something like Show::outDir()
        auto outDir = outputDirectory() / show.title;

        if (!fs::exists(outDir)) {
            auto cmd = std::format("_mkDir {}", outDir.string());
            jobs.push_back(cmd);
        }

        auto savePath = outDir / std::format("{}.mkv", episode.seasonKey());
        auto cmd2 = std::format("mv \"{}\" \"{}\"", title.path().string(), savePath.string());
        jobs.push_back(cmd2);
    }

    if (jobs.size() > 0) {
        jobs.push_back("_scanLocalTitles");
    }

    return jobs;
}

fs::path RippedTitle::path()
{
    return _mPath;
}

int AppModel::queuedAndPendingJobs()
{
    return _mQueuedAndPendingJobs;
}

bool AppModel::showHasLocalFile(std::string showName, std::string seasonKey)
{
    return _mLocalEpisodes.contains(std::format("{} {}", showName, seasonKey));
}

std::vector<std::string> AppModel::getCommandsToDeleteFileForTitle(const std::string& titleId)
{
    if (!hasTitle(titleId)) {
        return { };
    }

    auto title = titleById(titleId);
    if (!title.isDeleted()) {
        return { };
    }

    auto cmd = std::format(
    "mv \"{}\" \"{}.d\"",
        title.path().string(),
        title.path().string()
    );

    return {
        cmd,
        // And update the UI
        "_scanLocalTitles",
        "_reflowAll"
    };
}

std::vector<std::string> AppModel::getCommandsToUnDeleteFileForTitle(const std::string& titleId)
{
    if (!hasTitle(titleId)) {
        return { };
    }

    auto title = titleById(titleId);
    if (!title.isDeleted()) {
        return { };
    }

    auto cmd = std::format(
        "mv \"{}\" \"{}\"",
        title.path().string(),
        title.path().string().substr(0, title.path().string().length() - 2)
    );

    return {
        cmd,
        // And update the UI
        "_scanLocalTitles",
        "_reflowAll"
    };
}

std::vector<std::string> AppModel::getCommandsToUploadEntireShow(const std::string& showId)
{
    if (!hasShow(showId)) {
        return { };
    }

    auto show = showById(showId);
    const auto showDir = outputDirectory() / show.title;

    auto cmd = std::format(
        "rsync -a \"{}/\" \"root@10.4.6.2:/mnt/user/emby/tv/{}/\"",
        showDir.string(),
        show.title
    );

    return {
        cmd
    };
}

int AppModel::requestedPosition() {
    return _mRequestedPosition;
}

void AppModel::setRequestedPosition(int position)
{
    _mRequestedPosition = position;
}

bool RippedTitle::isDeleted() {
    return _mPath.string().ends_with(".d");
}

Show& AppModel::showById(const std::string& id) {
    return *_mShows.at(id);
}

Episode& AppModel::episodeById(const std::string& id) {
    return *_mEpisodes.at(id);
}

RippedTitle& AppModel::titleById(const std::string& id) {
    return *_mTitles.at(id);
}

bool AppModel::hasShow(const std::string& id) {
    return _mShows.contains(id);
}

bool AppModel::hasEpisode(const std::string& id) {
    return _mEpisodes.contains(id);
}

bool AppModel::hasTitle(const std::string& id) {
    return _mTitles.contains(id);
}


