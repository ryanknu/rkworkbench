#pragma once
#include <QDialog>
#include <QString>
#include <string>

class MainWindow;
class QNetworkAccessManager;

QT_BEGIN_NAMESPACE
namespace Ui {
class SettingsDialog;
}
QT_END_NAMESPACE

class SettingsDialog : public QDialog
{
	Q_OBJECT

public:
	// Category indices, for callers that want to jump straight to a page
	// (e.g. the toolbar's Encode Preset button) instead of landing on the
	// first one.
	enum Category {
		WorkingDirectoryCategory = 0,
		TmdbCategory = 1,
		EmbyCategory = 2,
		HomeAssistantCategory = 3,
		UsbCopyCategory = 4,
		EncodeCategory = 5,
	};

	explicit SettingsDialog(MainWindow *parent, int initialCategory = WorkingDirectoryCategory);
	~SettingsDialog() override;

	// Only meaningful if the user actually typed something this session -
	// the field itself is never prefilled with a saved secret.
	QString enteredTmdbApiKey() const;
	QString embyTvLocation() const;
	QString embyMovieLocation() const;

private:
	Ui::SettingsDialog *ui;
	MainWindow *_mainWindow;
	QNetworkAccessManager *_testNetworkManager = nullptr;
	std::string _originalWorkingDir;

	void _loadCurrentValues();
	void _browseWorkingDir();
	void _testHomeAssistantConnection();
	void _onSaveClicked();

	// Resolves the token to use for save/test: the typed value, or (if left
	// blank) whatever is already on disk, so re-saving other fields doesn't
	// require retyping a secret that's just sitting there unchanged.
	std::string _resolveHaToken() const;

	void _saveWorkingDirectory();
	void _saveTmdb();
	void _saveEmby();
	void _saveHomeAssistant();
	void _saveUsbCopy();
	void _saveEncode();
};
