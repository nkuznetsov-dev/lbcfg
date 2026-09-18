#include "devicetreedockwidget.h"
#include <QVBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QBrush>
#include <QColor>
#include "logmanager.h"
#include "commandmanager.h"
#include "appsettings.h"
#include "firmwarerepositorydialog.h"

DeviceTreeDockWidget::DeviceTreeDockWidget(QWidget *parent)
    : QDockWidget("Tree View", parent), lbplc(plcManager::instanse())
{
    QWidget *content = new QWidget(this);
    setWidget(content);
    QVBoxLayout *layout = new QVBoxLayout(content);
    treeView = new QTreeView(this);
    treeModel = new QStandardItemModel(this);
    treeView->setModel(treeModel);
    treeView->setHeaderHidden(true);
    treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(treeView, &QTreeView::customContextMenuRequested,
            this, &DeviceTreeDockWidget::showContextMenu);
    connect(treeView, &QTreeView::expanded,
            this, &DeviceTreeDockWidget::onTreeExpanded);

    m_firmwareAnalyzer = new firmwareAnalyzer(this);

    layout->addWidget(treeView);
    connect(treeView, &QTreeView::doubleClicked, this,
            [this](const QModelIndex &index){
                if (!index.isValid()) return;
                if (!index.parent().isValid()) {
                    const LogicBoxTarget target = index.data(TargetRole).value<LogicBoxTarget>();
                    if (target.isValid())
                        emit requestConfig(target);
                }
            }
            );
    connect(lbplc, &plcManager::showMessage, this, [this](const QString &title, const QString &message){
        QMessageBox::information(this, title, message);
    });
    connect(lbplc, &plcManager::restartAllCompleted, this, [this]
            (const plcManager::CommandContext &ctx){
                QMessageBox::information(this, "Перезагрузить все",
                                         QString("Команда на перезагрузку всех модулей %1 отправлена").arg(ctx.target.name));
            });
}

void DeviceTreeDockWidget::updateDevice(const LogicBoxTarget &target,
                                        const QMap<qsizetype, lbprocess::scaninfo> &scan)
{
    QStandardItem* plcRoot = findPlcRoot(target);
    QModelIndex rootIndex;

    if (!plcRoot) {
        plcRoot = new QStandardItem(target.name);
        plcRoot->setData(QVariant::fromValue(target), TargetRole);
        QFont rootFont = plcRoot->font();
        rootFont.setBold(true);
        plcRoot->setFont(rootFont);
        treeModel->appendRow(plcRoot);
        rootIndex = treeModel->index(treeModel->rowCount() - 1, 0);
    } else {
        plcRoot->removeRows(0, plcRoot->rowCount());
        plcRoot->setText(target.name);
        plcRoot->setData(QVariant::fromValue(target), TargetRole);
        rootIndex = plcRoot->index();
    }

    for (auto it = scan.begin(); it != scan.end(); ++it) {
        const auto &info = it.value();
        QStandardItem *col1 = new QStandardItem(QString("Slot %1: %2").arg(it.key()).arg(info.devtype));
        col1->setData(it.key(), SlotRole);
        col1->setData(info.devtype, ModuleTypeRole);
        col1->setData(info.version, InstalledVersionRole);
        col1->setData(true, ModuleItemRole);
        QFont boldFont = col1->font();
        boldFont.setBold(true);
        col1->setFont(boldFont);
        if (info.master)
            col1->setText(col1->text() + " [MASTER]");
        plcRoot->appendRow(col1);
        col1->appendRow(new QStandardItem("MAC: " + info.mac));

        QStandardItem *versionItem = new QStandardItem("Version: " + info.version);
        versionItem->setData(true, VersionInfoRole);
        col1->appendRow(versionItem);
        col1->appendRow(new QStandardItem("Serial: " + info.data.value(0)));

        if (m_firmwareLoaded)
            updateFirmwareStatus(col1);
    }
    treeView->expand(rootIndex);
}

bool DeviceTreeDockWidget::containsTarget(const LogicBoxTarget &target) const
{
    return findPlcRoot(target) != nullptr;
}

void DeviceTreeDockWidget::showContextMenu(const QPoint &pos)
{
    // Получаем индекс элемента, на который кликнули
    QModelIndex index = treeView->indexAt(pos);
    if (!index.isValid()) return;

    const bool isRoot = !index.parent().isValid();
    const bool isModule = index.data(ModuleItemRole).toBool();

    // MAC/Version/Serial are informational rows, not command targets.
    if (!isRoot && !isModule)
        return;

    plcManager::CommandContext ctx;
    if (isRoot) {
        ctx.target = index.data(TargetRole).value<LogicBoxTarget>();
    } else {
        ctx.target = index.parent().data(TargetRole).value<LogicBoxTarget>();
        ctx.slot = index.data(SlotRole).toInt();
    }
    if (!ctx.target.isValid())
        return;

    QMenu menu(this);
    // --- Только для Устройства ---
    if (isRoot) {
        QAction *getConfigAction = menu.addAction(QString("Запросить конфигурацию у %1").arg(ctx.target.name));
        QFont font = getConfigAction->font();
        font.setBold(true);
        getConfigAction->setFont(font);
        connect(getConfigAction, &QAction::triggered, this, [this, ctx]() {
            emit requestConfig(ctx.target);
        });

        QAction *update = menu.addAction("Обновить");
        connect(update, &QAction::triggered, this, [this, ctx](){
            emit requestUpdate(ctx.target);
        });

        QAction *removeAction = menu.addAction(QString("Удалить %1").arg(ctx.target.name));
        connect(removeAction, &QAction::triggered, this, [this, index]() {
            treeModel->removeRow(index.row());
        });

        QAction *restartAll = menu.addAction("Перезагрузить все");
        connect(restartAll, &QAction::triggered, this,
                [this, ctx](){
                    debugApp()<<"Перезагрузить все команда отправлена";
                    lbplc->startRestartAll(ctx);
                });

        QAction *fsformat = menu.addAction("Сбросить к заводским");
        connect(fsformat, &QAction::triggered, this, [this, ctx](){
            auto reply = QMessageBox::question(this,
                                               "Подтверждение сброса",
                                               QString("Вы уверены, что хотите сбросить устройство %1 к заводским настройкам?").arg(ctx.target.name),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No); // Кнопка по умолчанию
            if (reply == QMessageBox::Yes)
                lbplc->lbc_executeCommand(ctx, {"fsformat"}, "Сброс к заводским", [ctx, this] (const QStringList& res){
                    return QString("%1 сброшено к заводским настройкам. Требуется перезагрузка.")
                        .arg(ctx.target.name);
                });
        });

        QAction *flashAll = menu.addAction(QString("Прошить все модули %1").arg(ctx.target.name));
        connect(flashAll, &QAction::triggered, this, [this, ctx](){
            emit requestFlashAll(ctx);
        });

        QAction *fboot = menu.addAction(QString("Загрузить fboot в %1").arg(ctx.target.name));
        connect(fboot, &QAction::triggered, this, [this, ctx](){
            emit requestFboot(ctx);
        });

        QAction *nofboot = menu.addAction(QString("Удалить fboot в %1").arg(ctx.target.name));
        connect(nofboot, &QAction::triggered, this, [this, ctx]() {
            lbplc->lbc_executeCommand(ctx, {"nofboot"}, "Удалить fboot", [ctx](const QStringList&) {
                return QString("Команда на удаление fboot %1 отправлена").arg(ctx.displayName());
            });
        });

        menu.addSeparator();

        QAction *repositoryAction = menu.addAction("Репозиторий прошивок...");
        connect(repositoryAction, &QAction::triggered, this, [this]() {
            editFirmwareRepository();
        });

        QAction *refreshRepositoryAction = menu.addAction("Обновить версии из репозитория");
        connect(refreshRepositoryAction, &QAction::triggered, this, [this]() {
            m_repositoryPromptDeclined = false;

            QString repositoryRoot = m_repositoryRoot;
            if (repositoryRoot.isEmpty())
                repositoryRoot = AppSettings::firmwareRepositoryRoot();

            if (repositoryRoot.isEmpty()) {
                editFirmwareRepository();
                return;
            }

            if (loadFirmwareRepository(repositoryRoot, true))
                updateAllFirmwareStatuses();
        });
    }
    // --- Общие действия ---
    // QAction *getUptime = menu.addAction("Время работы");
    // connect(getUptime, &QAction::triggered, this, [this, ctx]() {
    //     lbplc->lbc_executeCommand(ctx, {"get", "sys.uptime"}, "Время работы", [ctx, this](const QStringList& res) {
    //         QString uptime = res.isEmpty() ? toBold("none") : toBold(res.at(0));
    //         return QString("Время работы %1 %2 сек").arg(ctx.displayName(), uptime);
    //     });
    // });
    menu.addSeparator();
    CommandManager::instance()->getUptimeAction(ctx, &menu);
    CommandManager::instance()->getRestartAction(ctx, &menu);
    CommandManager::instance()->getFlashAction(ctx, &menu);
    menu.addSeparator();

    // QAction *restart = menu.addAction("Перезагрузить");
    // connect(restart, &QAction::triggered, this, [this, ctx]() {
    //     lbplc->lbc_executeCommand(ctx, {"set", "sys.restart=1"}, "Перезагрузка", [ctx](const QStringList&) {
    //         return QString("Команда на перезагрузку %1 отправлена").arg(ctx.displayName());
    //     });
    // });
    // QAction *flash = menu.addAction("Загрузить прошивку ...");
    // connect(flash, &QAction::triggered, this, [this, ctx](){
    //     emit requestFlash(ctx);
    // });

    CommandManager::instance()->getLogMenu(ctx, &menu);

    // QMenu *logMenu = menu.addMenu("Запросить лог");

    // QAction *logAll = logMenu->addAction("Запросить весь лог");
    // connect(logAll, &QAction::triggered, this, [this, ctx](){
    //     lbplc->startLog(ctx, "a");
    // });

    // QAction *logLast100 = logMenu->addAction("Запросить 100 сообщений");
    // connect(logLast100, &QAction::triggered, this, [this, ctx](){
    //     lbplc->startLog(ctx, "a100");
    // });

    // QAction *logLast100f = logMenu->addAction("Запросить 100 и следовать");
    // connect(logLast100f, &QAction::triggered, this, [this, ctx](){
    //     lbplc->startLog(ctx, "a100f");
    // });

    menu.exec(treeView->viewport()->mapToGlobal(pos));

}

void DeviceTreeDockWidget::onTreeExpanded(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    QStandardItem *moduleItem = treeModel->itemFromIndex(index);
    if (!moduleItem || !moduleItem->data(ModuleItemRole).toBool())
        return;

    const QString moduleType = moduleItem->data(ModuleTypeRole).toString().trimmed();

    // There is nothing useful to compare until the module identifies itself.
    if (moduleType.isEmpty() || moduleType.compare(QStringLiteral("unknown"), Qt::CaseInsensitive) == 0) {
        if (QStandardItem *versionItem = versionInfoItem(moduleItem)) {
            versionItem->setBackground(QBrush());
            versionItem->setToolTip(QStringLiteral("Тип модуля не определён, сравнение версии недоступно"));
        }
        return;
    }

    if (!ensureFirmwareRepository()) {
        if (QStandardItem *versionItem = versionInfoItem(moduleItem)) {
            versionItem->setBackground(QBrush());
            versionItem->setToolTip(QStringLiteral("Репозиторий прошивок не настроен"));
        }
        return;
    }

    updateFirmwareStatus(moduleItem);
}

QStandardItem *DeviceTreeDockWidget::versionInfoItem(QStandardItem *moduleItem) const
{
    if (!moduleItem)
        return nullptr;

    for (int row = 0; row < moduleItem->rowCount(); ++row) {
        QStandardItem *child = moduleItem->child(row);
        if (child && child->data(VersionInfoRole).toBool())
            return child;
    }

    return nullptr;
}

bool DeviceTreeDockWidget::ensureFirmwareRepository()
{
    if (m_firmwareLoaded)
        return true;

    QString repositoryRoot = m_repositoryRoot;
    if (repositoryRoot.isEmpty())
        repositoryRoot = AppSettings::firmwareRepositoryRoot();

    if (!repositoryRoot.isEmpty() && loadFirmwareRepository(repositoryRoot, false))
        return true;

    if (m_repositoryPromptDeclined)
        return false;

    // Expanding a module must never throw the user straight into Explorer.
    // First show an explicit settings dialog with the current path and a
    // separate "Browse" button. The folder picker is opened only by request.
    return chooseFirmwareRepository(false);
}

bool DeviceTreeDockWidget::chooseFirmwareRepository(bool allowClear)
{
    QString initialPath = m_repositoryRoot;
    if (initialPath.isEmpty())
        initialPath = AppSettings::firmwareRepositoryRoot();

    while (true) {
        FirmwareRepositoryDialog dialog(initialPath, allowClear, this);
        if (dialog.exec() != QDialog::Accepted) {
            // Do not ask again for every module expanded in the same session.
            m_repositoryPromptDeclined = true;
            return false;
        }

        const QString selected = dialog.repositoryPath();
        if (selected.isEmpty() && allowClear) {
            clearFirmwareRepository();
            m_repositoryPromptDeclined = false;
            return true;
        }

        if (loadFirmwareRepository(selected, true)) {
            m_repositoryPromptDeclined = false;
            return true;
        }

        // The basic directory structure was already checked by the dialog.
        // If the analyzer rejects the repository, keep the entered path and
        // return to the same dialog instead of forcing the user to start over.
        initialPath = selected;
    }
}

void DeviceTreeDockWidget::editFirmwareRepository()
{
    m_repositoryPromptDeclined = false;
    if (chooseFirmwareRepository(true) && m_firmwareLoaded)
        updateAllFirmwareStatuses();
}

void DeviceTreeDockWidget::reloadFirmwareRepositoryFromSettings(bool showErrors)
{
    m_repositoryPromptDeclined = false;
    m_firmwareLoaded = false;
    m_repositoryRoot.clear();
    clearAllFirmwareStatuses();

    const QString repositoryRoot = AppSettings::firmwareRepositoryRoot();
    if (repositoryRoot.isEmpty())
        return;

    if (loadFirmwareRepository(repositoryRoot, showErrors))
        updateAllFirmwareStatuses();
}

void DeviceTreeDockWidget::clearFirmwareRepository()
{
    AppSettings::clearFirmwareRepositoryRoot();
    m_repositoryRoot.clear();
    m_firmwareLoaded = false;
    clearAllFirmwareStatuses();
}

bool DeviceTreeDockWidget::loadFirmwareRepository(const QString &repositoryRoot,
                                                  bool showErrors)
{
    const QFileInfo repositoryInfo(repositoryRoot);
    if (!repositoryInfo.exists() || !repositoryInfo.isDir()) {
        if (showErrors) {
            QMessageBox::warning(this,
                                 "Репозиторий прошивок",
                                 QString("Каталог репозитория не найден:\n%1")
                                     .arg(repositoryRoot));
        }
        return false;
    }

    const QString firmwarePath = QDir(repositoryInfo.absoluteFilePath())
                                     .filePath(QStringLiteral("firmware"));
    const QFileInfo firmwareInfo(firmwarePath);

    if (!firmwareInfo.exists() || !firmwareInfo.isDir()) {
        if (showErrors) {
            QMessageBox::warning(
                this,
                "Репозиторий прошивок",
                QString("В выбранном каталоге не найдена папка firmware:\n%1")
                    .arg(firmwarePath));
        }
        return false;
    }

    m_firmwareAnalyzer->setPath(firmwareInfo.absoluteFilePath());
    m_firmwareAnalyzer->update();

    if (m_firmwareAnalyzer->error() != firmwareAnalyzer::ok) {
        if (showErrors) {
            QMessageBox::warning(this,
                                 "Репозиторий прошивок",
                                 m_firmwareAnalyzer->errorString());
        }
        return false;
    }

    const QMap<QString, firmwareAnalyzer::fwinfo> firmwareMap =
        m_firmwareAnalyzer->getFirmwareMap();

    if (firmwareMap.isEmpty()) {
        if (showErrors) {
            QMessageBox::warning(
                this,
                "Репозиторий прошивок",
                QString("В каталоге firmware не найдено ни одной распознанной прошивки:\n%1")
                    .arg(firmwareInfo.absoluteFilePath()));
        }
        return false;
    }

    m_repositoryRoot = repositoryInfo.absoluteFilePath();
    m_firmwareLoaded = true;
    AppSettings::setFirmwareRepositoryRoot(m_repositoryRoot);

    const QList<firmwareAnalyzer::fwinfo> rejected =
        m_firmwareAnalyzer->getRejectedFirmware();
    for (const firmwareAnalyzer::fwinfo &info : rejected) {
        debugApp() << "Firmware rejected:" << info.sourcePath
                   << info.err << info.errStr;
    }

    return true;
}

void DeviceTreeDockWidget::updateFirmwareStatus(QStandardItem *moduleItem)
{
    if (!moduleItem || !m_firmwareLoaded)
        return;

    QStandardItem *versionItem = versionInfoItem(moduleItem);
    if (!versionItem)
        return;

    // A mismatch is not necessarily an error. Start from the normal appearance
    // and highlight only a confirmed match with the repository version.
    versionItem->setBackground(QBrush());
    versionItem->setToolTip(QString());

    const QString moduleType = moduleItem->data(ModuleTypeRole).toString().trimmed();
    const QString installedVersion = moduleItem->data(InstalledVersionRole).toString().trimmed();
    const QMap<QString, firmwareAnalyzer::fwinfo> firmwareMap =
        m_firmwareAnalyzer->getFirmwareMap();

    const auto it = firmwareMap.constFind(moduleType);
    if (it == firmwareMap.constEnd()) {
        versionItem->setToolTip(
            QString("Устройство: %1\nВ репозитории нет распознанной прошивки для %2")
                .arg(installedVersion, moduleType));
        return;
    }

    const firmwareAnalyzer::fwinfo &repositoryFirmware = it.value();
    const QString repositoryVersion = repositoryFirmware.version.trimmed();

    if (repositoryVersion.isEmpty()
        || repositoryVersion.compare(QStringLiteral("unknown"), Qt::CaseInsensitive) == 0) {
        versionItem->setToolTip(
            QString("Устройство: %1\nФайл: %2\nВерсия прошивки в репозитории не определена")
                .arg(installedVersion, repositoryFirmware.sourcePath));
        return;
    }

    const bool matches = firmwareAnalyzer::versionsMatch(installedVersion,
                                                         repositoryVersion);

    if (matches) {
        // Same green used by the application's successful/OK state rows.
        versionItem->setBackground(QBrush(QColor(QStringLiteral("#C3E6CB"))));
    }

    versionItem->setToolTip(
        QString("Модуль: %1\n"
                "Устройство: %2\n"
                "Для сравнения: %3\n"
                "Репозиторий: %4\n"
                "Статус: %5\n"
                "Файл: %6")
            .arg(moduleType,
                 installedVersion,
                 firmwareAnalyzer::normalizeVersion(installedVersion),
                 repositoryVersion,
                 matches ? QStringLiteral("версии совпадают")
                         : QStringLiteral("версии не совпадают"),
                 repositoryFirmware.sourcePath));
}

void DeviceTreeDockWidget::clearAllFirmwareStatuses()
{
    for (int rootRow = 0; rootRow < treeModel->rowCount(); ++rootRow) {
        QStandardItem *root = treeModel->item(rootRow);
        if (!root)
            continue;

        for (int moduleRow = 0; moduleRow < root->rowCount(); ++moduleRow) {
            QStandardItem *moduleItem = root->child(moduleRow);
            if (!moduleItem || !moduleItem->data(ModuleItemRole).toBool())
                continue;

            if (QStandardItem *versionItem = versionInfoItem(moduleItem)) {
                versionItem->setBackground(QBrush());
                versionItem->setToolTip(QStringLiteral("Репозиторий прошивок не настроен"));
            }
        }
    }
}

void DeviceTreeDockWidget::updateAllFirmwareStatuses()
{
    if (!m_firmwareLoaded)
        return;

    for (int rootRow = 0; rootRow < treeModel->rowCount(); ++rootRow) {
        QStandardItem *root = treeModel->item(rootRow);
        if (!root)
            continue;

        for (int moduleRow = 0; moduleRow < root->rowCount(); ++moduleRow) {
            QStandardItem *moduleItem = root->child(moduleRow);
            if (moduleItem && moduleItem->data(ModuleItemRole).toBool())
                updateFirmwareStatus(moduleItem);
        }
    }
}

QStandardItem *DeviceTreeDockWidget::findPlcRoot(const LogicBoxTarget &target) const
{
    for (int i = 0; i < treeModel->rowCount(); ++i) {
        QStandardItem *item = treeModel->item(i);
        if (!item)
            continue;
        const LogicBoxTarget existing = item->data(TargetRole).value<LogicBoxTarget>();
        if (existing.isValid() && existing.sameRoute(target))
            return item;
    }
    return nullptr;
}

// QString DeviceTreeDockWidget::toBold(const QString &text)
// {
//     return QString("<b>%1</b>").arg(text);
// }
