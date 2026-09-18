#include "mainmenu.h"
#include "commandmanager.h"
#include <QApplication>
#include <QMessageBox>
#include "mainwindow.h"

MainMenu::MainMenu(MainWindow *mainWindow)
    : QObject{mainWindow}, p_mainWindow(mainWindow), p_menuBar(mainWindow->menuBar())
{

    initFileMenu(p_menuBar);
    initEditMenu(p_menuBar);
    initViewMenu(p_menuBar);
    initPlcMenu(p_menuBar);
    initSettingsMenu(p_menuBar);
    initHelpMenu(p_menuBar);

    connect(CommandManager::instance(), &CommandManager::activeConfDockWidgetChanged,
            this, &MainMenu::updateMenuState);
}

void MainMenu::updateMenuState(ConfigDockWidget *activeWidget)
{
    if (!activeWidget){
        saveAction->setEnabled(false);
        saveAsAction->setEnabled(false);
        saveAction->setText(tr("&Сохранить"));
        saveAsAction->setText(tr("Сохранить &как..."));
        return;
    }
    QString plcName = activeWidget->getPlcName();
    saveAction->setEnabled(true);
    saveAction->setText(QString("&Сохранить %1").arg(plcName));
    saveAsAction->setEnabled(true);
    saveAsAction->setText(QString("Сохранить %1 &как...").arg(plcName));
}

void MainMenu::initFileMenu(QMenuBar *menuBar)
{
    QMenu *fileMenu = menuBar->addMenu("&Файл");

    QAction *newAction = fileMenu->addAction("&Новая конфигурация");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainMenu::newConfigurationRequested);

    QAction *openAction = fileMenu->addAction("&Открыть...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainMenu::openFileRequested);

    saveAction = fileMenu->addAction("&Сохранить");
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setEnabled(false);
    connect(saveAction, &QAction::triggered, [](){
        if (!CommandManager::instance()->isNullActiveConfDockWidget())
            CommandManager::instance()->
                getActiveConfDockWidget()->saveFile();
    });
    CommandManager::instance()->setSaveAction(saveAction);

    saveAsAction = fileMenu->addAction(tr("Сохранить &как..."));
    saveAsAction->setEnabled(false);
    connect(saveAsAction, &QAction::triggered, [](){
        if (!CommandManager::instance()->isNullActiveConfDockWidget())
            CommandManager::instance()->
                getActiveConfDockWidget()->saveFileAs();
    });
    CommandManager::instance()->setSaveAsAction(saveAsAction);

    fileMenu->addSeparator();

    QAction *exitAction = fileMenu->addAction(tr("&Выход"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
}

void MainMenu::initEditMenu(QMenuBar *menuBar)
{
    editMenu = menuBar->addMenu("&Правка");
    connect(editMenu, &QMenu::aboutToShow, this, &MainMenu::onEditMenuAboutToShow);
}

void MainMenu::initViewMenu(QMenuBar *menuBar)
{
    viewMenu = menuBar->addMenu("&Вид");

    treeAct = viewMenu->addAction(tr("Дерево устройств"));
    treeAct->setCheckable(true);
    connect(treeAct, &QAction::triggered, this, [this]() {
        if (p_mainWindow->getTreeDock() && p_mainWindow->getTreeDock()->isVisible()) {
            p_mainWindow->getTreeDock()->close(); // Закрываем (и уничтожаем благодаря WA_DeleteOnClose)
        } else {
            auto* dock = p_mainWindow->createTreeDockWidget(); // Создаем заново или открываем
            dock->show();
            dock->raise();
        }
    });

    discAct = viewMenu->addAction(tr("Поиск устройств"));
    discAct->setCheckable(true);
    connect(discAct, &QAction::triggered, this, [this]() {
        if (p_mainWindow->getDiscoverDock() && p_mainWindow->getDiscoverDock()->isVisible()) {
            p_mainWindow->getDiscoverDock()->close();
        } else {
            auto* dock = p_mainWindow->createDiscoverDockWidget();
            dock->show();
            dock->raise();
        }
    });

    logAct = viewMenu->addAction("Логи");
    logAct->setCheckable(true);
    connect(logAct, &QAction::triggered, this, [this](){
        if (p_mainWindow->getLogDock() && p_mainWindow->getLogDock()->isVisible())
            p_mainWindow->getLogDock()->close();
        else{
            auto *dock = p_mainWindow->createLogDockWidget();
            dock->show();
            dock->raise();
        }
    });

    viewMenu->addSeparator();

    watchMenu = new QMenu("Watch", viewMenu);
    viewMenu->addMenu(watchMenu);

    createWatch = new QAction("Создать новый", this);
    createWatch->setShortcut(QKeySequence("Ctrl+W"));
    watchMenu->addAction(createWatch);
    connect(createWatch, &QAction::triggered, this, [this](){
        static QAtomicInt counter(0);
        LogicBoxTarget target;
        target.name = QString("new %1").arg(counter.fetchAndAddRelaxed(1) + 1);
        WatchDockWidget* watch = p_mainWindow->createWatchDockWidget(target);
        watch->show();
        watch->raise();
        watch->setFocus();
    });

    confMenu = new QMenu("Открытые конфигурации", viewMenu);
    confMenu->setEnabled(false);
    viewMenu->addMenu(confMenu);

    connect(viewMenu, &QMenu::aboutToShow, this, &MainMenu::onViewMenuAboutToShow);
}

void MainMenu::initPlcMenu(QMenuBar *menuBar)
{
    plcMenu = menuBar->addMenu("&ПЛК");
    connect(plcMenu, &QMenu::aboutToShow, this, &MainMenu::onPlcMenuAboutToShow);
}

void MainMenu::initSettingsMenu(QMenuBar *menuBar)
{
    QMenu *settingsMenu = menuBar->addMenu("&Настройки");

    QAction *firmwareRepositoryAction = settingsMenu->addAction("Репозиторий прошивок...");
    firmwareRepositoryAction->setStatusTip(
        "Изменить корневой каталог репозитория LogicBox с папкой firmware");
    connect(firmwareRepositoryAction, &QAction::triggered, this, [this]() {
        p_mainWindow->editFirmwareRepositorySettings();
    });
}

void MainMenu::initHelpMenu(QMenuBar *menuBar)
{
QMenu *helpMenu = menuBar->addMenu("&Справка");

    QAction *aboutAct = helpMenu->addAction(tr("&О программе..."));
    aboutAct->setStatusTip(tr("Показать информацию о приложении"));

    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::about(p_mainWindow,
                           tr("О программе lbcfg"),
                           tr("<h3>Конфигуратор ПЛК Logic Box</h3>"
                              "<p>Версия 1.0.0</p>"
                              "<p>Программа предназначена для сканирования устройств, "
                              "редактирования файлов конфигурации YAML/YML и безопасной "
                              "загрузки прошивок в ПЛК.</p>"
                              "<p>Данное программное обеспечение использует библиотеку Qt, "
                              "распространяемую на условиях лицензии GNU Lesser General Public License (LGPL) версии 3. "
                              "Вы имеете право пересобирать приложение с измененной версией библиотеки Qt в соответствии с условиями LGPLv3.</p>"
                              "<p>Подробную информацию о лицензии Qt можно найти в меню 'О библиотеке Qt'.</p>")
                           );
    });

    QAction *aboutQtAct = helpMenu->addAction("О библиотеке &Qt...");

    connect(aboutQtAct, &QAction::triggered, this, [this]() {
        QMessageBox::aboutQt(p_mainWindow, "О библиотеке Qt");
    });
}

void MainMenu::onEditMenuAboutToShow()
{
    editMenu->clear();
    QList<QAction*> actions = activeTextActions();

    if (actions.isEmpty()) {
        QAction *emptyAct = editMenu->addAction("Нет активного редактора");
        emptyAct->setEnabled(false);
        return;
    }

    for (QAction *act : actions) {
        editMenu->addAction(act);
    }
}

void MainMenu::onViewMenuAboutToShow()
{
    treeAct->setChecked(p_mainWindow->getTreeDock() && p_mainWindow->getTreeDock()->isVisible());
    discAct->setChecked(p_mainWindow->getDiscoverDock() && p_mainWindow->getDiscoverDock()->isVisible());
    logAct->setChecked(p_mainWindow->getLogDock() && p_mainWindow->getLogDock()->isVisible());

    for (QAction *action : watchMenu->actions()) {
        if (action != createWatch) {
            watchMenu->removeAction(action);
            action->deleteLater(); // Безопасное удаление из памяти Qt
        }
    }

    confMenu->clear();
    auto openWatch = p_mainWindow->getWatchDocks();

    if (!openWatch.isEmpty()){

        for (auto *watch : openWatch){
            QAction *watchAct = watchMenu->addAction(watch->getPlcName());

            if (CommandManager::instance()->getActiveWatchDockWidget() == watch){
                watchAct->setCheckable(true);
                watchAct->setChecked(true);
            }

            connect(watchAct, &QAction::triggered, this, [watch](){
                watch->show();
                watch->raise();
                watch->setFocus();
            });
        }
    }

    QList<ConfigDockWidget*> openConf = p_mainWindow->getConfigDocks();

    if (!openConf.isEmpty()) {
        confMenu->setEnabled(true);

        for (ConfigDockWidget *dock : openConf) {
            QAction *docAct = confMenu->addAction(dock->getPlcName());

            // Если этот файл сейчас редактируется (активен) — ставим галочку
            if (CommandManager::instance()->getActiveConfDockWidget() == dock) {
                docAct->setCheckable(true);
                docAct->setChecked(true);
            }

            // По клику переключаемся на этот файл
            connect(docAct, &QAction::triggered, this, [dock]() {
                dock->show();
                dock->raise();
                dock->setFocus();
            });
        }
    }else{
        confMenu->setEnabled(false);
    }
}

void MainMenu::onPlcMenuAboutToShow()
{
    plcMenu->clear();

    if (logMenu)
        logMenu->deleteLater();
    logMenu = new QMenu("Запросить лог у...", plcMenu);
    if (connectMenu)
        connectMenu->deleteLater();
    connectMenu = new QMenu("Подключиться к...", plcMenu);


    QAction *discoverAction = plcMenu->addAction("Сканироавть");
    connect(discoverAction, &QAction::triggered, [this](){
        if (!p_mainWindow->getDiscoverDock().get()){
            auto dock = p_mainWindow->createDiscoverDockWidget();
            dock->show();
            dock->raise();
            dock->setFocus();
        }
        plcManager::instanse()->startDiscover();
    });

    QAction *confAction = CommandManager::instance()->getConfAction();
    ConfigDockWidget *active = CommandManager::instance()->getActiveConfDockWidget();
    if (confAction && active){

        // confAction->setText(QString("Сконфигурировать %1")
        //                         .arg(active->getPlcName()));
        plcMenu->addAction(confAction);
    }
    else{
        confAction = plcMenu->addAction("Сконфигурировать");
        confAction->setEnabled(false);
    }

    plcMenu->addMenu(logMenu);
    if (p_mainWindow->getDiscoverDock().get())
    {
        QMap<QString, discover::lbinfo> ldmap = p_mainWindow->getDiscoverDock()->getLdmap();
        if (!ldmap.isEmpty())
        {
            logMenu->setEnabled(true);
            for (auto it = ldmap.begin(); it != ldmap.end(); ++it){
                plcManager::CommandContext ctx;
                ctx.target.name = it.value().name;
                ctx.target.mac = it.value().mac;
                ctx.target.endpoint = it.value().endpoint;
                CommandManager::instance()->getLogMenu(ctx, logMenu, it.value().name);
            }
        }else
            logMenu->setEnabled(false);
    }else{
        logMenu->setEnabled(false);
    }

    plcMenu->addMenu(connectMenu);
    auto watchDocks = p_mainWindow->getWatchDocks();
    if (!watchDocks.isEmpty()){
        connectMenu->setEnabled(true);
        QList<WatchDockWidget*> connectedWatch;
        for (auto w : watchDocks) {
            QAction *wA = connectMenu->addAction(w->getPlcName());
            if (w->isConnected()){
                wA->setCheckable(true);
                wA->setChecked(true);
                connectedWatch.append(w);
            }
            connect(wA, &QAction::triggered, this, [w](){
                w->toggleConnection();
            });
        }
        if (!connectedWatch.isEmpty()){
            auto disconnectAll = connectMenu->addAction("Отключить все");
            connect(disconnectAll, &QAction::triggered, this, [connectedWatch](){
                for (auto w : connectedWatch)
                    w->toggleConnection();
            });
        }
    }else{
        connectMenu->setEnabled(false);
    }
}

QList<QAction *> MainMenu::activeTextActions() const
{
    // Запрашиваем у Медиатора просто активный виджет
    ConfigDockWidget *active = CommandManager::instance()->getActiveConfDockWidget();
    if (!active) return QList<QAction*>();

    return active->activeTextActions();
}
