#include "mainwindow.h"
#include "model.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    // Initialize app model.
    // We create the model before the UI to allow model construction to be a little slow.
    auto* gModel = new AppModel();

    // Initialize Qt
	QApplication a(argc, argv);
	MainWindow w;
	w.setAppModel(gModel);
	w.show();
	return a.exec();
}
