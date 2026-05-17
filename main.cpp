#include "mainwindow.h"
#include "model.h"

#include <QApplication>
#include <clocale>

int main(int argc, char *argv[])
{
	std::setlocale(LC_COLLATE, nullptr);

	// Read in requested output directory from launch args.
	std::string mediaDir = argc == 2 ? argv[1] : "";

    // Initialize app model.
    // We create the model before the UI to allow model construction to be a little slow.
    auto* gModel = new AppModel(mediaDir);

    // Initialize Qt
	QApplication a(argc, argv);
	a.setOrganizationName("ryan");
	a.setApplicationName("rkworkbench");
	MainWindow w;
	w.setAppModel(gModel, mediaDir);
	w.show();
	return a.exec();
}
