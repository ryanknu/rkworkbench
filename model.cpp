#include "model.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

AppModel::AppModel(std::string outDir)
{
    // Defaults
    _mTmdbMode = TV;

    // Set up config directory
    auto home = getenv("HOME");
    _mHomeDirPath = fs::path(home);
	_mConfigDirPath = _mHomeDirPath / ".config" / "rkworkbench";
    _readWorkingDir(outDir);
}

void AppModel::toggleTmdbMode()
{
    _mTmdbMode = _mTmdbMode == TV ? FILM : TV;
}

std::string AppModel::tmdbMode() const {
    return _mTmdbMode == TV ? "TV" : "Film";
}

std::string AppModel::workingDirPath() const {
    return _mWorkingDirPath.string();
}

/**
 * Retrieves the working dir for the application.
 * Priority Order:
 * 1. outDir, and SAVE
 * 2. previous saved dir
 * 3. working dir, and SAVE
 */
void AppModel::_readWorkingDir(std::string outDir)
{
    // Handle tilde expansion.
    if (outDir.substr(0, 2) == "~/") {
        outDir = std::format("{}{}", _mHomeDirPath.string(), outDir.substr(1));
    }

    if (!outDir.empty()) {
        fs::path absDir(outDir);
        fs::path dir = outDir.front() == '/' ? absDir : fs::canonical(outDir);

        if (fs::exists(dir)) {
            _mWorkingDirPath = dir;
            _writeWorkingDir();
        }
    }

    std::ifstream t(_mConfigDirPath / "wd.txt");
    std::stringstream buffer;
    buffer << t.rdbuf();
    fs::path workingDir = fs::path(buffer.str());
    _mWorkingDirPath = workingDir;

    if (_mWorkingDirPath.empty()) {
        _mWorkingDirPath = fs::current_path();
        _writeWorkingDir();
    }
}

void AppModel::_writeWorkingDir() const {
    fs::path of = _mConfigDirPath / "wd.txt";
    std::ofstream output_file;
    output_file.open(of);
    if (output_file.is_open()) {
        output_file << _mWorkingDirPath.string();
        output_file.close();
    }
}

void AppModel::resetDrag() {
    _mDragInitialX = 0;
    _mDragCurrentX = 0;
    _mDragActive = false;
}

void AppModel::setInitialDragData(int x, int disksTreeWidth, int showsTreeWidth) {
    _mDragInitialX = x;
    _mDragCurrentX = x;
    _mInitialDisksTreeWidth = disksTreeWidth;
    _mInitialShowsTreeWidth = showsTreeWidth;
    _mWhichTree = x > (disksTreeWidth + 20) ? 1 : 2;
    _mDragActive = true;
}

void AppModel::setDragCurrentX(int x) {
    _mDragCurrentX = x;
}

bool AppModel::isDragActive() const {
    return _mDragActive;
}

int AppModel::getCurrentDragXOffset() {
    return _mDragCurrentX - _mDragInitialX;
}

int AppModel::getCurrentDisksTreeWidth() {
    return std::max(100, _mInitialDisksTreeWidth + getCurrentDragXOffset());
}

int AppModel::getCurrentShowsTreeWidth() {
    return std::max(100, _mInitialShowsTreeWidth + getCurrentDragXOffset());
}

int AppModel::getTreesMask() {
    return _mWhichTree;
}
