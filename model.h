#pragma once
#include <string>
#include <filesystem>

enum TmdbMode {
    TV,
    FILM,
};

class AppModel {
public:
    AppModel(std::string outDir);
    std::string tmdbMode() const;
    void toggleTmdbMode();
    std::string workingDirPath() const;

    // Splitter for tree widths
    void resetDrag();
    void setInitialDragData(int x, int disksTreeWidth, int showsTreeWidth);
    void setDragCurrentX(int x);
    bool isDragActive() const;
    int getCurrentDragXOffset();
    int getCurrentDisksTreeWidth();
    int getCurrentShowsTreeWidth();
    int getTreesMask();

private:
    // Media data
    TmdbMode _mTmdbMode;

    // Configurations
    std::filesystem::path _mHomeDirPath;
    std::filesystem::path _mConfigDirPath;
    std::filesystem::path _mWorkingDirPath;

    // Splitter for tree widths
    int _mDragInitialX = 0;
    int _mDragCurrentX = 0;
    int _mInitialDisksTreeWidth = 200;
    int _mInitialShowsTreeWidth = 200;
    int _mWhichTree = 0;
    bool _mDragActive = false;

    void _readWorkingDir(std::string outDir);
    void _writeWorkingDir() const;
};
