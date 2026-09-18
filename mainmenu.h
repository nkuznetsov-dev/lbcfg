#ifndef MAINMENU_H
#define MAINMENU_H

#include <QObject>
#include <QMenuBar>
#include "configdockwidget.h"
// #include "mainwindow.h"

class MainWindow;
class MainMenu : public QObject
{
    Q_OBJECT
public:
    explicit MainMenu(MainWindow *mainWindow = nullptr);
    virtual ~MainMenu() = default;

    void updateMenuState(ConfigDockWidget *activeWidget);

signals:
    void openFileRequested();
    void newConfigurationRequested();

private:
    MainWindow *p_mainWindow = nullptr;
    QMenuBar   *p_menuBar    = nullptr;

    void initFileMenu(QMenuBar *menuBar);
    void initEditMenu(QMenuBar *menuBar);
    void initViewMenu(QMenuBar *menuBar);
    void initPlcMenu(QMenuBar *menuBar);
    void initSettingsMenu(QMenuBar *menuBar);
    void initHelpMenu(QMenuBar *menuBar);

    QAction *saveAction = nullptr;
    QAction *saveAsAction = nullptr;

    QMenu *editMenu = nullptr;
    QMenu *viewMenu = nullptr;
    QMenu *plcMenu = nullptr;
    QMenu *logMenu = nullptr;
    QMenu *connectMenu = nullptr;
    void onEditMenuAboutToShow();
    void onViewMenuAboutToShow();
    void onPlcMenuAboutToShow();
    QList<QAction*> activeTextActions() const;

    QAction *treeAct = nullptr;
    QAction *discAct = nullptr;
    QAction *logAct = nullptr;
    QAction *createWatch = nullptr;
    QMenu *watchMenu = nullptr;
    QMenu *confMenu = nullptr;

};

#endif // MAINMENU_H
