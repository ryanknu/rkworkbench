#include <filesystem>
#include <QMainWindow>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QPushButton>
#include <QTreeView>
#include "phonon/mediasource.h"
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
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();
	void setAppModel(AppModel *theModel);

private:
	Ui::MainWindow *ui;
	Phonon::MediaSource *video;
	std::filesystem::path configPath;
	AppModel *appModel;
	CommandWorker *worker;

	void _reflowTrees();
	void _reflowTaskList();
	std::string _getIdForSelectedItemInTree(QTreeView *&tree);
	void _queueTask(std::string cmd);
};
