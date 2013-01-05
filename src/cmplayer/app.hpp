#ifndef APP_HPP
#define APP_HPP

#include "stdafx.hpp"
#include "qtsingleapplication/qtsingleapplication.h"

class QUrl;		class Mrl;
class MainWindow;	class QMenuBar;

class App : public QtSolution::QtSingleApplication {
	Q_OBJECT
public:
	App(int &argc, char **argv);
	~App();
	static QIcon defaultIcon();
	static Mrl getMrlFromCommandLine();

	void setMainWindow(MainWindow *mw);
	MainWindow *mainWindow() const;
	QStringList devices() const;
	QString styleName() const;
	bool isUnique() const;
	QStringList availableStyleNames() const;
#ifdef Q_WS_MAC
	QMenuBar *globalMenuBar() const;
#endif
	void setStyleName(const QString &name);
	void setAlwaysOnTop(QWidget *widget, bool onTop);
	void setScreensaverDisabled(bool disabled);
	void setUnique(bool unique);
	bool shutdown();
public slots:
	void xSync() {syncX();}
private slots:
	void open(const QString &url);
	void onMessageReceived(const QString &message);
private:
	App(const App&) = delete;
	App &operator = (const App&) = delete;
	static void messageHandler(QtMsgType type, const char *msg);
	bool event(QEvent *event);
	struct Data;
	Data *d = nullptr;
};

static inline App *app() {return static_cast<App*>(qApp);}

#endif // APPLICATION_HPP
