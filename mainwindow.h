#include <filesystem>
#include <cstdint>
#include <unordered_map>
#include <QMainWindow>
#include <QStandardItemModel>
#include <QPushButton>
#include <QTreeView>
#include <QAction>
#include <QMediaPlayer>
#include <QTimer>
#include <QCloseEvent>
#include <QNetworkAccessManager>
#include "model.h"

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
	void closeEvent(QCloseEvent *event) override;

	Q_INVOKABLE
	void processMessage(std::string message);

	void _addTreeItem(std::string tree, std::string id, std::string parentId, std::string parentText, std::string text, std::string color, std::string after);
	void _removeTreeItemById(std::string tree, std::string id);
	void _changeTreeItemColor(std::string tree, std::string id, std::string color);
	void _changeTreeItemText(std::string tree, std::string id, std::string text);
	void _selectTreeItem(std::string tree, std::string id);
	void _changeGarbageSize(std::uint64_t size);
	void _clearTrees();

	// Used by SettingsDialog to read current state and push through changes.
	std::string workingDirPath() const;
	QString tmdbApiKey() const { return _tmdbApiKey; }
	QString embyTvLocation() const { return _embyTvLocation; }
	QString embyMovieLocation() const { return _embyMovieLocation; }
	QString haBaseUrl() const { return _haBaseUrl; }
	bool isHomeAssistantConfigured() const { return _haConfigured; }
	bool hasActiveJobs() const;
	void reinitHomeAssistant();

private:
	Ui::MainWindow *ui;
	std::filesystem::path configPath;
	AppModel *appModel;
	QAudioOutput *audioOutput;
	QMediaPlayer *player;

	// UI State
	qint64 _mRequestedPlayerPosition = 0;
	bool _mSeekPending = false;
	int ffmpegQueueCount = 0;
	int ffmpegActiveCount = 0;
	int rsyncQueueCount = 0;
	int rsyncActiveCount = 0;
	int copyQueueCount = 0;
	int copyActiveCount = 0;
	bool _mCutModeActive = false;
	QTimer *spinnerTimer = nullptr;
	int spinnerIndex = 0;
	QTimer *usbStatusTimer = nullptr;
	bool _mUsbPresent = false;

	// Home Assistant sensor push
	QNetworkAccessManager *haNetworkManager = nullptr;
	QTimer *haPushTimer = nullptr;
	QString _haBaseUrl;
	QString _haToken;
	bool _haConfigured = false;

	// Settings cached from ~/.config/rkworkbench/ (see SettingsDialog)
	QString _tmdbApiKey;
	QString _embyTvLocation;
	QString _embyMovieLocation;
	bool _deleteUsbAfterCopy = false;
	bool _rsyncAfterEncode = false;
	QString _encodeCommand;
	QString _encodePresetLabel;
	bool _mkvmergeRemuxEnabled = true;
	QString _mkvmergeCommand;

	// Maps an in-flight ffmpeg job's id (episode/film-video id) to the
	// show/film id to rsync once it completes, when "Rsync after encode" is
	// enabled. Populated when the job is queued, consumed on CommandCompleted.
	std::unordered_map<std::string, std::string> _encodeUploadTargets;

	std::string _mediaDir;
	std::string currentEncodingFile;
	std::string currentRsyncFile;
	std::string lastFfmpegOutput;
	std::string lastRsyncOutput;
	std::string lastCopyOutput;

	void _updateFfmpegStatus();
	void _updateRsyncStatus();
	void _updateCopyStatus();
	void _updateUsbStatus();
	void _loadHomeAssistantConfig();
	void _pushHomeAssistantSensors();
	void _postHomeAssistantState(const QString &entityId, double state, const QString &unit, const QString &friendlyName);
	void _loadEmbySettings();
	void _loadUsbCopySettings();
	void _loadEncodeSettings();
	void _updateEncodeBtnText();
	std::string _getIdForSelectedItemInTree(QTreeView *&tree);
	void _findVlc();
	void _clearMetadataPanel();
	void _loadInPlayer(QString path);
	void _selectSeekableAudioTrack();
	void _populateAudioTrackCombo();
	bool _mPopulatingAudioTrackCombo = false;
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
	void file_inventory();
	void lookup_film(const char* id, const char* api_key, bool skip_special_features);
	void lookup_tv(const char* id, const char* api_key, bool skip_season_0);
	void rsync_show(const char* show_id, const char* tv_location, const char* movie_location);
	void rsync_from_nas(const char* show_id, const char* tv_location, const char* movie_location);
	void portable_encode(const char* id);
	bool has_portable_for_tv_episode(const char* id);
	bool has_portable_for_film_video(const char* id);
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
	void reencode_tv_episode(const char* id, const char* command, bool mkvmerge_enabled, const char* mkvmerge_command);
	void reencode_film_video(const char* id, const char* command, bool mkvmerge_enabled, const char* mkvmerge_command);
	void delete_title(const char* id);
	void undelete_title(const char* id);
	void collect_garbage();
	void copy_from_usb(bool delete_source);
	void import_path(const char* path);
	const char* usb_status();
	void add_to_stitch(const char* path);
	void remove_from_stitch(size_t index);
	void reorder_stitch(size_t from, size_t to);
	void clear_stitch();
	void perform_stitch();
	void start_cut(const char* title_id);
	void add_cut_point(std::uint64_t position_ms);
	void remove_cut_point(size_t index);
	void assign_cut_segment(size_t segment_index, const char* media_id, bool is_tv);
	void unassign_cut_segment(size_t segment_index);
	void cancel_cut();
	void process_cuts(const char* command);

	bool has_original_for_tv_episode(const char* id);
	bool has_original_for_film_video(const char* id);
	void restore_original_for_tv_episode(const char* id);
	void restore_original_for_film_video(const char* id);
}
