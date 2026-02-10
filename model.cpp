#include "model.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <QDebug>
#include <nlohmann/json.hpp>
#include <regex>

namespace fs = std::filesystem;
using json = nlohmann::json;

AppModel::AppModel()
{
    // Defaults
    _mTasks = { };
    _mShows = { };
    _mTitles = { };
    _mEpisodes = { };
    _mTmdbMode = TmdbMode::TV;
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
	_scanLocalTitles();
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
    _mTmdbApiKey = apiKey;
    _writeApiKey();
}

void AppModel::toggleTmdbMode()
{
    _mTmdbMode = _mTmdbMode == TmdbMode::TV
        ? TmdbMode::FILM
        : TmdbMode::TV;
}

std::string AppModel::tmdbMode()
{
    return _mTmdbMode == TmdbMode::TV ? "TV" : "Film";
}

fs::path AppModel::tvDirectory()
{
    return _mConfigDirPath / "tv";
}

fs::path AppModel::filmDirectory()
{
    return _mConfigDirPath / "films";
}

void AppModel::_createDirectories()
{
   	fs::create_directories(tvDirectory());
	fs::create_directories(filmDirectory());
	fs::create_directories(_mWorkingDirPath / "output");
}

void AppModel::_readApiKey()
{
    std::ifstream t(_mConfigDirPath / "tmdb.key");
    std::stringstream buffer;
    buffer << t.rdbuf();
    _mTmdbApiKey = buffer.str();
}

void AppModel::_writeApiKey()
{
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
    return !stem.empty() && std::all_of(stem.begin(), stem.end(), ::isdigit);
}

/**
 * This method was written by an LLM
 */
bool detectSeasonJson(const fs::path& path) {
    // Get the filename as a string
    std::string filename = path.filename().string();

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
    _mShows = { };
    _mEpisodes = { };

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

            Show* show = new Show(
                parsed.id,
                std::format("{} ({}) [tmdb={}]", parsed.name, parsed.first_air_date.substr(0, 4), parsed.id)
            );

            for (auto& sejson : jf["seasons"]) {
                show->pushSeason(sejson["season_number"].get<int>());
            }

            _mShows.push_back(*show);
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

                Episode* episode = new Episode(
                    parsed.id,
                    parsed.season_number,
                    parsed.episode_number,
                    parsed.show_id,
                    parsed.name
                );

                _mEpisodes.push_back(*episode);
            }
        }
    }
}

/**
 * Scans the local filesystem for titles.
 */
void AppModel::_scanLocalTitles()
{
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

            auto title = _newRippedTitle(
                entry.path(),
                size,
                diskName,
                entry.path().filename().string()
            );

            _mTitles.push_back(*title);
        }
    }
}

Show::Show(int _id, std::string _title)
{
    id = _id;
    seasons = { };
    title = _title;
}

void Show::pushSeason(int season)
{
    seasons.push_back(season);
}

Episode::Episode(int _id, int _season, int _number, int _showId, std::string _title)
{
    id = _id;
    season = _season;
    number = _number;
    showId = _showId;
    title = _title;
}

std::vector<std::string> *AppModel::tasks()
{
    return &_mTasks;
}

std::vector<Show> *AppModel::shows()
{
    return &_mShows;
}

std::vector<Episode> *AppModel::episodes()
{
    return &_mEpisodes;
}

std::vector<RippedTitle> *AppModel::titles()
{
    return &_mTitles;
}

std::string Episode::seasonKey()
{
    return std::format("S{:02}E{:02}", season, number);
}

std::string Episode::friendlyTitle()
{
    return std::format("{} - {}", seasonKey(), title);
}

void AppModel::setPreprocessorCommand(std::string cmd)
{
    _mPreprocessorCommand = cmd;
}

void AppModel::identifyEpisode(std::string titleId, std::string showId)
{
    _mIdentifiedEpisodes[titleId] = showId;
}

bool AppModel::isIdentified(std::string item)
{
    // either a key or a value in _mIdentifiedEpisodes
    for (const auto& pair : _mIdentifiedEpisodes) {
        if (pair.first == item || pair.second == item) {
            return true;
        }
    }
    return false;
}

RippedTitle* AppModel::_newRippedTitle(fs::path path, std::uintmax_t size, std::string diskName, std::string titleName)
{
    return new RippedTitle(_mIdSequence.fetch_add(1), path, size, diskName, titleName);
}

RippedTitle::RippedTitle(std::uint64_t _id, fs::path path, std::uintmax_t size, std::string diskName, std::string titleName)
{
    id = _id;
    _mPath = path;
    _mSize = size;
    _mDiskName = diskName;
    _mTitleName = titleName;
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
 */
void AppModel::enqueueAllJobs()
{
    auto countTasks = _mTasks.size();
    for (const auto& pair : _mIdentifiedEpisodes) {
        auto diskId = pair.first;
        auto episodeId = pair.second;
        for (auto& title : _mTitles) {
            if (std::format("{}", title.id) == diskId) {
                for (auto& episode : _mEpisodes) {
                    if (std::format("{}", episode.id) == episodeId) {
                        auto showId = episode.showId;
                        for (auto& show : _mShows) {
                            if (show.id == showId) {
                                auto outDir = std::format("output/{}", show.title);

                                // TODO: If outdir exists, don't mkdir it
                                auto cmd = std::format("mkdir -p \"{}\"", outDir);
                                _mTasks.push_back(cmd);

                                auto cmd2 = std::format("mv \"{}\" \"{}/{}.mkv\"", title.path().string(), outDir, episode.seasonKey());
                                _mTasks.push_back(cmd2);
                                goto broke;
                            }
                        }
                    }
                }
            }
        }
        broke:
        qDebug() << "test";
    }
    if (_mTasks.size() > countTasks) {
        _mTasks.push_back("_reflowAll");
    }
}

fs::path RippedTitle::path()
{
    return _mPath;
}

int AppModel::queuedAndPendingJobs()
{
    return _mQueuedAndPendingJobs;
}
