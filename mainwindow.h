#include <filesystem>
#include <QMainWindow>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QPushButton>
#include <QTreeView>
#include <QAction>
#include <QMediaPlayer>
#include <QTimer>
#include "model.h"
#include "commandworker.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow() override;
	void setAppModel(AppModel *theModel, std::string mediaDir);
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

	Q_INVOKABLE
	void processMessage(std::string message);

	void _addTreeItem(std::string tree, std::string id, std::string parentId, std::string parentText, std::string text, std::string color, std::string after);
	void _removeTreeItemById(std::string tree, std::string id);
	void _changeTreeItemColor(std::string tree, std::string id, std::string color);
	void _changeTreeItemText(std::string tree, std::string id, std::string text);
	void _selectTreeItem(std::string tree, std::string id);
	void _changeGarbageSize(std::uint64_t size);
	void _clearTrees();
	void _hideTmdbApiKeyInput();

private:
	Ui::MainWindow *ui;
	std::filesystem::path configPath;
	AppModel *appModel;
	CommandWorker *worker;
	QAudioOutput *audioOutput;
	QMediaPlayer *player;

	// UI State
	int _mRequestedPlayerPosition = 0;
	int ffmpegQueueCount = 0;
	int ffmpegActiveCount = 0;
	int rsyncQueueCount = 0;
	int rsyncActiveCount = 0;
	QTimer *spinnerTimer = nullptr;
	int spinnerIndex = 0;
	std::string _mediaDir;
	std::string currentEncodingFile;
	std::string currentRsyncFile;
	std::string lastFfmpegOutput;
	std::string lastRsyncOutput;

	void _reflowDisksTree() const;
	void _reflowShowsTree() const;
	void _reflowGcButton() const;
	void _reflowTaskList();
	void _updateFfmpegStatus();
	void _updateRsyncStatus();
	std::string _getIdForSelectedItemInTree(QTreeView *&tree);
	void _queueTask(std::string cmd);
	void _queueTasks(std::vector<std::string> cmds);
	void _findVlc();
	void _clearMetadataPanel();
	void _loadInPlayer(QString path);
	QString _vlcProgram;
	QStringList _vlcArgs;
	bool _vlcFound = false;
};

void callback_wrapper(void* ptr, const char* message);

extern "C" {
	typedef void (*message_callback_t)(void* ptr, const char* message);
	void start_rust_processing(void* ptr, const char* media_dir, message_callback_t callback);

	// Fast requests to the backend
	const char* get_filename_for_title_id(const char* id);
	const char* get_filename_for_tv_episode_id(const char* id);
	const char* get_filename_for_film_video_id(const char* id);
	void free_string(const char* str);

	// Commands that the worker thread can work.
	void initial_load();
	void lookup_film(const char* id, const char* api_key);
	void lookup_tv(const char* id, const char* api_key);
	void rename_identified();
	void rsync_show(const char* show_id, const char* tv_location, const char* movie_location);
	void delete_tv_show(const char* show_id);
	void delete_tv_season(const char* show_id, size_t season_number);
	void delete_film(const char* film_id);
	void delete_film_video(const char* video_id);
	void map_tv_episode(const char* from, const char* to);
	void map_film_video(const char* from, const char* to);
	void confirm_tv_episode_plays(const char* id);
	void confirm_film_video_plays(const char* id);
	void unidentify_tv_episode(const char* id);
	void unidentify_film_video(const char* id);
 void match_scan(const char* id, const char* command);
	void fetch_tmdb_still(const char* id, bool is_tv);
	void fetch_mkv_info(const char* path);
	void reencode_tv_episode(const char* id, const char* command);
	void reencode_film_video(const char* id, const char* command);
	void delete_title(const char* id);
	void undelete_title(const char* id);
	void collect_garbage();

	bool has_original_for_tv_episode(const char* id);
	bool has_original_for_film_video(const char* id);
	void restore_original_for_tv_episode(const char* id);
	void restore_original_for_film_video(const char* id);
}
