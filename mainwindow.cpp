#include "mainwindow.h"
#include <QDockWidget>
#include <QStatusBar>
#include <QLabel>
#include <QFileDialog>
#include <QApplication>
#include <QHelpEvent>
#include <QToolTip>
#include <QHBoxLayout>
#include <QMessageBox>
#include "commandmanager.h"
#include "firmwarepackage.h"



MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(1280, 720);

    QWidget* dummy = new QWidget(this);
    setCentralWidget(dummy);
    dummy->hide(); // Скрываем, чтобы доки сомкнулись в центре

    lbplc = plcManager::instanse();
    createTreeDockWidget();
    createDiscoverDockWidget();
    setDockNestingEnabled(true);
    connect(this, &QMainWindow::tabifiedDockWidgetActivated,
            CommandManager::instance(), &CommandManager::checkDockWidget);
    createLogDockWidget();

    // 1. Создаем главное меню
    menu = new MainMenu(this);

    connect(menu, &MainMenu::openFileRequested, this, [this](){
        QString filePath = QFileDialog::getOpenFileName(this, "Открыть конфигурацию", "", "YAML Files (*.yaml *.yml);;All Files (*)");
        if (!filePath.isEmpty()) {
            QString fileName = QFileInfo(filePath).fileName();
            ConfigDockWidget* dock = CreateConfDockWidget(filePath, fileName);

            if (!dock->openFile(filePath)) {
                QMessageBox::critical(this, "Ошибка", "Не удалось открыть файл");
                delete dock;
                return;
            }
            dock->show();
            dock->raise();
            dock->setFocus();
        }
    });

    connect(menu, &MainMenu::newConfigurationRequested, this, [this]{
        ConfigDockWidget* dock = CreateConfDockWidget(QUuid::createUuid().toString(), "noname");
        dock->show();
        dock->raise();
        dock->setFocus();
    });
    // Создаем строку состояния (Status Bar)
    QStatusBar *statusBar = this->statusBar();

    // Временное сообщение (исчезнет через 5000 миллисекунд / 5 секунд)
    statusBar->showMessage(tr("Программа готова к работе"), 5000);


    QLabel *watchStatusLabel = new QLabel(this);
    watchStatusLabel->setAlignment(Qt::AlignCenter);
    watchStatusLabel->setStyleSheet("QLabel { padding: 2px 8px; border-radius: 3px; }");
    watchStatusLabel->setFixedHeight(20);
    statusBar->addPermanentWidget(watchStatusLabel);
    connect(lbplc, &plcManager::activeWatchChanged, this, [watchStatusLabel](const QStringList &keys) {
        int count = keys.size();
        if (count > 0) {
            watchStatusLabel->setText(QString("Connected: %1").arg(count));
            watchStatusLabel->setToolTip(QString("%1").arg(keys.join("\n")));
            watchStatusLabel->setStyleSheet(
                "QLabel {"
                "  background-color: #D4EDDA;"
                "  color: #155724;"
                "  border: 1px solid #C3E6CB;"
                "  padding: 0px 6px;"          // Сузили вертикальный отступ до 0px
                "  border-radius: 3px;"
                "  font-weight: bold;"
                "  font-size: 11px;"           // Слегка уменьшили шрифт, чтобы рамка не поджимала текст
                "}"
                );
        }else{
            watchStatusLabel->setText("No Connections");
            watchStatusLabel->setToolTip("No active watch lists");
            watchStatusLabel->setStyleSheet("QLabel { padding: 2px 8px; border-radius: 3px; color: #6c757d; }");
        }
    });
    emit lbplc->activeWatchChanged(lbplc->activeWatchKeys());

    fwWidget = new FirmwareWidget(this);
    statusBar->addPermanentWidget(fwWidget);


    connect(lbplc, &plcManager::firmwareStarted, this, [this]
            (const plcManager::CommandContext &ctx, const QString &message){
                debugApp()<<"plcManager::firmwareStarted"<<ctx.target.endpoint.displayString()<<ctx.target.name;
                this->statusBar()->showMessage(message);
                fwWidget->showStatus();
            });
    connect(lbplc, &plcManager::firmwareProgressChanged,
            fwWidget, &FirmwareWidget::setProgress);

    connect(lbplc, &plcManager::firmwareFinished,
            fwWidget, &FirmwareWidget::resetAndHide);

    connect(lbplc, &plcManager::errorOccurred, this, [this](const QString &msg){
        this->statusBar()->showMessage(msg);
    });
    connect(lbplc, &plcManager::eventOccurred, this, [this](const QString &msg){
        this->statusBar()->showMessage(msg, 5000);
    });
    connect(lbplc, &plcManager::configReceived,
            this, &MainWindow::CreateConfig);

    connect(fwWidget, &FirmwareWidget::stopButtonPressed, this, [this](){
        lbplc->stopFirmware();
    });

    connect(lbplc, &plcManager::logStarted, this, &MainWindow::createLogDockWidget);

}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event); // Обязательно вызываем базу
    // Теперь размеры окна уже реальные (800x600)
    int totalWidth = this->width();

    // Задаем пропорции 1/3 и 2/3
    resizeDocks({treeDock, discoverDock}, {totalWidth/3, 2*totalWidth/3}, Qt::Horizontal);

    int totalHeight = this->height();
    if (discoverDock && logDock) {
        resizeDocks({discoverDock.get(), logDock.get()}, {totalHeight - 150, 150}, Qt::Vertical);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Проверяем, что событие происходит на панели вкладок
    QTabBar *tabBar = qobject_cast<QTabBar*>(watched);
    if (tabBar && event->type() == QEvent::ToolTip) {
        QHelpEvent *helpEvent = static_cast<QHelpEvent*>(event);
        // Определяем индекс вкладки, на которую указывает курсор
        int index = tabBar->tabAt(helpEvent->pos());
        if (index != -1) {
            QString tabText = tabBar->tabText(index);
            // Ищем документ, соответствующий этой вкладке
            for (ConfigDockWidget *dock : configDocks.values()) {
                if (dock && dock->windowTitle() == tabText) {
                    QString filePath = dock->getCurrentFilePath();
                    if (!filePath.isEmpty()) {
                        // Выводим подсказку на экран в глобальных координатах курсора
                        QToolTip::showText(helpEvent->globalPos(), filePath, tabBar);
                    } else {
                        // Если пути нет, принудительно скрываем подсказку
                        QToolTip::hideText();
                    }
                    return true; // Сообщаем Qt, что событие полностью обработано
                }
            }
        }
        // Если это вкладка "Discover" или любой другой не наш док, скрываем старый текст
        QToolTip::hideText();
    }
    return QMainWindow::eventFilter(watched, event);
}

MainWindow::~MainWindow() {}

QPointer<DeviceTreeDockWidget> MainWindow::getTreeDock() const
{
    return treeDock.get();
}

QPointer<DiscoverDockWidget> MainWindow::getDiscoverDock() const
{
    return discoverDock.get();
}

ConfigDockWidget *MainWindow::CreateConfDockWidget(const QString &key, const QString &name)
{
    ConfigDockWidget* dock = nullptr;
    if (configDocks.contains(key)) {
        dock = configDocks[key];
    } else {
        dock = new ConfigDockWidget(name, this);
        dock->setAttribute(Qt::WA_DeleteOnClose);

        configDocks.insert(key, dock);
        tabifyDockWidgetTo(dock, Qt::RightDockWidgetArea);
        for (QTabBar *tabBar : this->findChildren<QTabBar *>()) {
            tabBar->installEventFilter(this);
        }

        connect(dock, &QObject::destroyed, this, [this, key]() {
            debugApp() << "destroy ConfDockWidget: "<<key;
            configDocks.remove(key);
            CommandManager::instance()->resetActiveConfDockWidget();
        });
        connect(lbplc, &plcManager::confCompleted, this, &MainWindow::checkTreeAndStartScan,
                Qt::UniqueConnection);
    }
    return dock;
}

DeviceTreeDockWidget *MainWindow::createTreeDockWidget()
{
    if (treeDock)
        return treeDock.get();
    treeDock = new DeviceTreeDockWidget(this);
    treeDock->setAttribute(Qt::WA_DeleteOnClose); // Чтобы док уничтожался при нажатии на крестик
    treeDock->setWindowTitle("Device Tree");
    treeDock->setAllowedAreas(Qt::AllDockWidgetAreas);

    connect(CommandManager::instance(), &CommandManager::requestFlash, this, [this]
            (const plcManager::CommandContext &ctx){
                QString filePath = QFileDialog::getOpenFileName(
                    this,
                    "Загрузить прошивку ...",
                    "",
                    "LogicBox Firmware (*.bin *.bin.xz *.xz);;BIN Files (*.bin);;XZ compressed (*.xz);;All Files (*)");

                if (filePath.isEmpty())
                    return;

                const FirmwarePackage::Result firmware = FirmwarePackage::prepare(filePath);

                if (!firmware.isOk()) {
                    QMessageBox::critical(
                        this,
                        "Ошибка подготовки прошивки",
                        firmware.error);
                    return;
                }

                QMetaObject::Connection cleanupConnection;
                if (firmware.temporary) {
                    statusBar()->showMessage(
                        QString("XZ распакован: %1")
                            .arg(QFileInfo(firmware.path).fileName()),
                        5000);

                    // Install cleanup before starting: an address/configuration
                    // error can complete synchronously inside startFirmware().
                    cleanupConnection = connect(
                        lbplc,
                        &plcManager::firmwareFinished,
                        this,
                        [firmware]() { FirmwarePackage::cleanup(firmware); },
                        Qt::SingleShotConnection);
                }

                const bool accepted = lbplc->startFirmware(
                    ctx,
                    firmware.path,
                    "Загрузка уже выполняется, дождитесь окончания",
                    QString("Загрузка прошивки в %1 ...").arg(ctx.displayName()));

                // If another OTA was already active, no firmwareFinished signal
                // belongs to this package. Disconnect the cleanup handler and
                // remove the temporary BIN immediately instead of deleting it
                // on somebody else's future OTA completion.
                if (!accepted && firmware.temporary) {
                    QObject::disconnect(cleanupConnection);
                    FirmwarePackage::cleanup(firmware);
                }
            });
    connect(treeDock, &DeviceTreeDockWidget::requestFlashAll, this, [this]
            (const plcManager::CommandContext &ctx){
                QString filePath = QFileDialog::getExistingDirectory(this, "Выберите директорию для прошивки ...", "", QFileDialog::DontResolveSymlinks);
                if (!filePath.isEmpty()) {
                    lbplc->startFirmwareAll(ctx, filePath,
                                            "Загрузка уже выполняется, дождитесь окончания",
                                            QString("Загрузка прошивки в %1 ...").arg(ctx.displayName()));
                }
            });
    connect(treeDock, &DeviceTreeDockWidget::requestFboot, this, [this]
            (const plcManager::CommandContext &ctx){
                QString filePath = QFileDialog::getOpenFileName(this, "Загрузить fboot ...", "", "Fboot Files (*.fboot);;All Files (*)");                if (!filePath.isEmpty()) {
                    lbplc->startFirmware(ctx, filePath,
                                         "Загрузка уже выполняется, дождитесь окончания",
                                         QString("Загрузка fboot в %1 ...").arg(ctx.displayName()),
                                         "fboot");
                }
            });
    connect(treeDock, &DeviceTreeDockWidget::requestUpdate,
            this, [this](const LogicBoxTarget &target){ lbplc->scanDevice(target); });
    connect(treeDock, &DeviceTreeDockWidget::requestConfig,
            this, [this](const LogicBoxTarget &target){ lbplc->requestConfig(target); });

    connect(lbplc, &plcManager::scanCompleted, this, [this]
            (const LogicBoxTarget &target, const QMap<qsizetype, lbprocess::scaninfo> &scanData){
        if (!treeDock)
            createTreeDockWidget();
        treeDock->updateDevice(target, scanData);
        treeDock->show();
        treeDock->raise();
        treeDock->setFocus();
    });

    tabifyDockWidgetTo(treeDock, Qt::LeftDockWidgetArea);

    return treeDock.get();
}

DiscoverDockWidget *MainWindow::createDiscoverDockWidget()
{
    if (discoverDock)
        return discoverDock.get();
    discoverDock = new DiscoverDockWidget(this);
    discoverDock->setAttribute(Qt::WA_DeleteOnClose);
    discoverDock->setWindowTitle("Discover");
    discoverDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    connect(discoverDock, &DiscoverDockWidget::newConfig,
            this, [this] (const LogicBoxTarget &target){
                CreateConfig(target);
            });
    connect(discoverDock, &DiscoverDockWidget::deviceSelected,
            this, [this](const LogicBoxTarget &target){ lbplc->scanDevice(target); });
    connect(discoverDock, &DiscoverDockWidget::requestConfig,
            this, [this](const LogicBoxTarget &target){ lbplc->requestConfig(target); });

    tabifyDockWidgetTo(discoverDock, Qt::RightDockWidgetArea);
    return discoverDock.get();
}

LogDockWidget *MainWindow::createLogDockWidget()
{
    if (logDock) return logDock.get();

    logDock = new LogDockWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, logDock.get());

    if (getDocksInArea(Qt::RightDockWidgetArea).isEmpty()) {
        splitDockWidget(discoverDock.get(), logDock.get(), Qt::Vertical);
    }
    logDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    connect(lbplc, &plcManager::logStarted, logDock, &LogDockWidget::onLogStarted);
    connect(lbplc, &plcManager::logFinished, logDock, &LogDockWidget::onLogFinished);
    connect(logDock, &LogDockWidget::stopButtonPressed, lbplc, &plcManager::stopLog);
    connect(logDock, &QObject::destroyed, lbplc, [this](){
        disconnect(lbplc, &plcManager::logFinished, logDock, &LogDockWidget::onLogFinished);
        lbplc->stopLog();
    });

    return logDock.get();
}

WatchDockWidget *MainWindow::createWatchDockWidget(const LogicBoxTarget &target)
{
    const QString key = target.routeKey();
    WatchDockWidget* dock = nullptr;
    for (WatchDockWidget *existing : watchDocks) {
        if (existing && existing->getTarget().routeKey() == key) {
            dock = existing;
            break;
        }
    }
    if (dock) {
        dock->setTarget(target);
    } else {
        dock = new WatchDockWidget(target, this);
        dock->setAttribute(Qt::WA_DeleteOnClose);
        watchDocks.insert(key, dock);
        tabifyDockWidgetTo(dock, Qt::LeftDockWidgetArea);

        connect(dock, &QObject::destroyed, this, [this, key]() {
            debugApp() << "destroy WatchDockWidget:" << key;
            watchDocks.remove(key);
        });
    }
    return dock;
}

QList<ConfigDockWidget *> MainWindow::getConfigDocks() const
{
    return configDocks.values();
}

QList<WatchDockWidget *> MainWindow::getWatchDocks() const
{
    return watchDocks.values();
}

QPointer<LogDockWidget> MainWindow::getLogDock() const
{
    return logDock;
}

void MainWindow::CreateConfig(const LogicBoxTarget &target, const QString &content)
{
    const QString key = target.isValid() ? target.routeKey() : QUuid::createUuid().toString();
    ConfigDockWidget* dock = CreateConfDockWidget(key, target.displayName());
    dock->setBoundTarget(target);
    dock->setConfig(content);
    dock->show();
    dock->raise();
}

QList<QDockWidget *> MainWindow::getDocksInArea(Qt::DockWidgetArea area) const
{
    QList<QDockWidget*> result;

    // 1. Находим вообще все QDockWidget, принадлежащие главному окну
    QList<QDockWidget*> allDocks = findChildren<QDockWidget*>();

    // 2. Фильтруем их по текущей области
    for (QDockWidget *dock : allDocks) {
        if (dock && dockWidgetArea(dock) == area) {
            result.append(dock);
        }
    }

    return result;
}

void MainWindow::tabifyDockWidgetTo(QDockWidget *dock, Qt::DockWidgetArea area)
{
    QList<QDockWidget*> areaDocks = getDocksInArea(area);

    QDockWidget* targetForTab = nullptr;
    for (QDockWidget* d : areaDocks) {
        if (d->isVisible()) {
            targetForTab = d;
            break;
        }
    }

    if (targetForTab) {
        tabifyDockWidget(targetForTab, dock);
    } else {
        addDockWidget(area, dock);
    }
}

void MainWindow::checkTreeAndStartScan(const LogicBoxTarget &target)
{
    if (!treeDock || !treeDock->containsTarget(target))
        lbplc->scanDevice(target);
}
