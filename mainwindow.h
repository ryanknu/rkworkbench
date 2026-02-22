#include <filesystem>
#include <QMainWindow>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QPushButton>
#include <QTreeView>
#include <QAction>
#include <QMediaPlayer>
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

	void _addTreeItem(std::string tree, std::string id, std::string parentText, std::string text, std::string color, std::string after);
	void _changeTreeItemColor(std::string tree, std::string id, std::string color);
	void _changeGarbageSize(std::uint64_t size);
	void _hideTmdbApiKeyInput();

private:
	Ui::MainWindow *ui;
	std::filesystem::path configPath;
	AppModel *appModel;
	CommandWorker *worker;
	QMediaPlayer *player;

	void _reflowDisksTree() const;
	void _reflowShowsTree() const;
	void _reflowGcButton() const;
	void _reflowTaskList();
	std::string _getIdForSelectedItemInTree(QTreeView *&tree);
	void _queueTask(std::string cmd);
	void _queueTasks(std::vector<std::string> cmds);
	int _getRequestedPosition();
};

void callback_wrapper(void* ptr, const char* message);

extern "C" {
	typedef void (*message_callback_t)(void* ptr, const char* message);
	void start_rust_processing(void* ptr, const char* media_dir, message_callback_t callback);
	// void uc_echo(const char* message);
	void initial_load();
	void map_media(const char* from, const char* to);
}
