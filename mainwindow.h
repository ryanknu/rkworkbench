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
	void setAppModel(AppModel *theModel);

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
