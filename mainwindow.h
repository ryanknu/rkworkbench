#include <filesystem>
#include <QMainWindow>
#include <QStandardItemModel>
#include <QPushButton>
#include <QTreeView>
#include "phonon/mediasource.h"
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
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();
	void setAppModel(AppModel *theModel);

private:
	Ui::MainWindow *ui;
	Phonon::MediaSource *video;
	std::filesystem::path configPath;
	AppModel *appModel;

	void _reflowTrees();
	std::string _getIdForSelectedItemInTree(QTreeView *&tree);
};
