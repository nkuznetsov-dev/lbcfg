#include "configdockwidget.h"
// #include "commandmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QTableView>
#include <QHeaderView>
#include <QEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QInputDialog>
#include "logmanager.h"
#include "mainwindow.h"


ConfigDockWidget::ConfigDockWidget(const QString &name, MainWindow *parent)
    : QDockWidget(QString("Конфигурация: %1").arg(name), parent),
    lbplc(plcManager::instanse()),
    p_mainWindow(parent)
{
    plcName = name;
    QWidget *content = new QWidget(this);
    setWidget(content);

    // Основной вертикальный макет виджета
    QVBoxLayout* mainLayout = new QVBoxLayout(content);
    mainLayout->setSpacing(5);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // QVBoxLayout* layout = new QVBoxLayout(content);
    // layout->setSpacing(0);  // Расстояние МЕЖДУ topLayout и QTextEdit (минимальное)

    // 1. Оставляем верхнюю панель управления
    QHBoxLayout* topLayout = new QHBoxLayout();
    configButton = new QPushButton("Сконфигурировать", this);
    configButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    topLayout->addWidget(configButton,1);

    plcSelector = new QComboBox(this);
    plcSelector->setPlaceholderText("Выберите ПЛК..."); // Подсказка, если список пуст
    QTableView* tableView = new QTableView(this);
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows); // Выделять всю строку целиком
    tableView->setSelectionMode(QAbstractItemView::SingleSelection); // Только один ПЛК за раз
    tableView->setShowGrid(false); // Отключаем сетку для более чистого вида
    tableView->verticalHeader()->hide(); // Прячем левую нумерацию строк
    // Настраиваем поведение колонок, чтобы они занимали всё доступное место
    tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tableView->verticalHeader()->setDefaultSectionSize(20); // Задаем высоту строк такую же, как и у QComboBox (20px)
    tableView->horizontalHeader()->hide(); // Прячем верхние заголовки, если они не нужны
    plcSelector->setView(tableView); // Устанавливаем таблицу как выпадающий виджет
    topLayout->addWidget(plcSelector,3);
    topLayout->addStretch(3);
    mainLayout->addLayout(topLayout);
    plcSelector->installEventFilter(this);

    // 2. Создаем горизонтальный макет для бокового меню и контента
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(2);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // 3. Инициализируем боковую панель навигации (QListWidget)
    sidebarMenu = new QListWidget(this);
    sidebarMenu->setFixedWidth(110); // Фиксированная компактная ширина для текста
    sidebarMenu->setSpacing(4);

    sidebarMenu->setStyleSheet(
        "QListWidget {"
        "   border: none;"
        "   background-color: #f5f5f5;" // Легкий серый фон панели
        "   outline: 0;"
        "}"
        "QListWidget::item {"
        "   padding: 8px 6px;"
        "   border-radius: 4px;"
        "   color: #333333;"
        "}"
        "QListWidget::item:selected {"
        "   background-color: #0078d7;" // Синий акцент активной вкладки
        "   color: white;"
        "}"
        "QListWidget::item:hover:!selected {"
        "   background-color: #e5e5e5;"
        "}"
        );

    sidebarMenu->addItem(new QListWidgetItem("Text View"));
    sidebarMenu->addItem(new QListWidgetItem("Var View"));
    sidebarMenu->addItem(new QListWidgetItem("IO View"));

    // 4. Инициализируем стек-контейнер для страниц
    stackedContainer = new QStackedWidget(this);

    // Страница 0: Текстовый редактор YAML
    yamlPage = new yamlTextView(this);
    stackedContainer->addWidget(yamlPage); // Индекс 0 в стеке

    // Страница 1: Просмотр переменных (VarView)
    varPage = new varView(this);
    stackedContainer->addWidget(varPage); // Индекс 1 в стеке

    // Страница 2: Настройка модулей ПЛК (DeviceView - задел на будущее)
    devicePage = new deviceView(this);
    stackedContainer->addWidget(devicePage); // Индекс 2 в стеке

    // 5. Собираем макет контента
    contentLayout->addWidget(sidebarMenu);
    contentLayout->addWidget(stackedContainer, 1); // Растягиваем контентную область
    mainLayout->addLayout(contentLayout);

    // 6. Подключение сигналов и слотов переключения страниц
    connect(sidebarMenu, &QListWidget::currentRowChanged,
            this, &ConfigDockWidget::onSidebarRowChanged);

    connect(yamlPage, &yamlTextView::TextChanged, this, &ConfigDockWidget::onTextChanged);
    // editor->setContextMenuPolicy(Qt::CustomContextMenu);
    // connect(editor, &QTextEdit::customContextMenuRequested, this, &ConfigDockWidget::showCustomContextMenu);

    connect(configButton, &QPushButton::clicked, this, &ConfigDockWidget::onConfigureClicked);

    connect(plcSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConfigDockWidget::scrollToSelectedPlc);

    confAction = new QAction(QString("Сконфигурировать %1")
                                 .arg(getPlcName()), this);
    connect(confAction, &QAction::triggered, this, [this](){
        onConfigureClicked();
    });
    // CommandManager::instance()->setConfAction(confAction);

    connect(varPage, &varView::onChanged, this, [this]{
        modified = true;
        updateTitle();
    });

    connect(devicePage, &deviceView::onChanged, this, [this]{
        modified = true;
        updateTitle();
    });

    connect(varPage, &varView::addVariableToWatch,
            this, &ConfigDockWidget::onAddVariableToWatch);

    connect(yamlPage, &yamlTextView::isReplaceComplete, varPage, &varView::resetModified);
    connect(yamlPage, &yamlTextView::isReplaceComplete, devicePage, &deviceView::resetModified);

    // Устанавливаем дефолтную страницу (YAML) при запуске
    sidebarMenu->setCurrentRow(0);
}

void ConfigDockWidget::setConfig(const QString &yaml)
{
    if (yamlParser)
        yamlParser->deleteLater();
    yamlParser = new lbyaml(yaml, lbyaml::data, this);

    originalYaml = yaml;
    yamlPage->setText(originalYaml);
    // editor->blockSignals(true);
    // editor->setPlainText(yaml);
    // editor->blockSignals(false);
    modified = false;
    plcSelector->blockSignals(true);
    plcSelector->clear();

    // Используем новый метод для генерации модели
    plcSelector->setModel(createPlcModel(yaml));
    plcSelector->setModelColumn(0);

    plcName = plcSelector->currentText();
    yamlParser->setlbhost(plcName);
    // plcSelector->setCurrentIndex(-1); // Показываем подсказку при открытии нового файла
    plcSelector->blockSignals(false);
}

// QString ConfigDockWidget::config() const
// {
//     return yamlPage->getEditor()->toPlainText();
// }

bool ConfigDockWidget::isModified() const
{
    return modified;
}

bool ConfigDockWidget::openFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&file);
    setConfig(in.readAll());
    currentFilePath = filePath;
    if (yamlParser)
        yamlParser->deleteLater();
    yamlParser = new lbyaml(filePath, lbyaml::file, this);
    yamlParser->setlbhost(plcName);
    updateTitle();
    return true;
}

bool ConfigDockWidget::saveFile()
{
    int result = isModifiedPages(true);

    if (result == QMessageBox::Cancel) {
        return false;
    }

    if (currentFilePath.isEmpty())
        return saveFileAs();
    else
        return writeFile(currentFilePath);
}

bool ConfigDockWidget::saveFileAs()
{
    int result = isModifiedPages(true);

    if (result == QMessageBox::Cancel) {
        return false;
    }

    QString filePath = QFileDialog::getSaveFileName(this, "Сохранить конфигурацию как...",
                                                    plcName,
                                                    "YAML Files (*.yaml *.yml);;All Files (*)");
    if (filePath.isEmpty())
        return false;

    return writeFile(filePath);
}

bool ConfigDockWidget::writeFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Ошибка", "Не удалось сохранить файл");
        return false;
    }
    QTextStream out(&file);
    out << yamlPage->text();
    currentFilePath = filePath;
    originalYaml = yamlPage->text(); // Сбрасываем флаг модификации
    modified = false;
    plcName = QFileInfo(filePath).fileName();
    updateTitle();
    return true;
}

void ConfigDockWidget::onTextChanged()
{
    if (yamlPage->text() != originalYaml){
        modified = true;
        // setWindowTitle(QString("* Конфигурация: %1").arg(plcName));
    }else{
        modified = false;
        // setWindowTitle(QString("Конфигурация: %1").arg(plcName));
    }
    updateTitle();
}


void ConfigDockWidget::setBoundTarget(const LogicBoxTarget &target)
{
    boundTarget = target;
}

bool ConfigDockWidget::resolveTargetForSelection(const QString &name,
                                                   const QString &mac,
                                                   LogicBoxTarget *target)
{
    if (!target || !lbplc)
        return false;

    LogicBoxTarget macProbe;
    macProbe.mac = mac;
    if (boundTarget.isValid()
        && boundTarget.name.compare(name, Qt::CaseInsensitive) == 0
        && (mac.trimmed().isEmpty()
            || boundTarget.normalizedMac() == macProbe.normalizedMac())) {
        *target = boundTarget;
        return true;
    }

    const auto status = lbplc->resolveTarget(name, mac, target);
    if (status == plcManager::ResolveStatus::Found) {
        boundTarget = *target;
        return true;
    }

    if (status == plcManager::ResolveStatus::NotFound) {
        QMessageBox::warning(this, "Endpoint не найден",
                             QString("Для ПЛК %1 не найден scoped endpoint. "
                                     "Сначала выполните Discover.").arg(name));
        return false;
    }

    const QList<LogicBoxTarget> candidates = lbplc->targetsForIdentity(name, mac);
    QStringList choices;
    for (const LogicBoxTarget &candidate : candidates)
        choices << candidate.endpoint.displayString();

    bool ok = false;
    const QString selected = QInputDialog::getItem(
        this,
        "Выбор сетевого интерфейса",
        QString("ПЛК %1 доступен несколькими маршрутами. Выберите endpoint:").arg(name),
        choices,
        0,
        false,
        &ok);
    if (!ok || selected.isEmpty())
        return false;

    const int selectedIndex = choices.indexOf(selected);
    if (selectedIndex < 0 || selectedIndex >= candidates.size())
        return false;

    *target = candidates.at(selectedIndex);
    target->name = name;
    if (!mac.trimmed().isEmpty())
        target->mac = mac;
    boundTarget = *target;
    return true;
}

void ConfigDockWidget::onConfigureClicked()
{
    int result = isModifiedPages(true);

    if (result == QMessageBox::Cancel) {
        return;
    }
    QString currentSelected = plcSelector->currentText();
    plcSelector->blockSignals(true);
    plcSelector->clear();

    // Передаем актуальный текст из редактора в наш метод
    plcSelector->setModel(createPlcModel(yamlPage->text()));
    plcSelector->setModelColumn(0);

    // Восстанавливаем позицию
    int index = plcSelector->findText(currentSelected);
    plcSelector->setCurrentIndex(index); // Если index == -1, Qt сам покажет placeholder
    plcSelector->blockSignals(false);

    // int currentRow = plcSelector->currentIndex();
    if (index < 0) {
        QMessageBox::warning(this,
                             "Внимание",
                             "Не выбрана конфигурация");
        return;
    }
    // QString selectedPlc = plcSelector->currentText();
    QStandardItemModel* model = qobject_cast<QStandardItemModel*>(plcSelector->model());
    QString mac;
    if (model && model->item(index, 1)) {
        mac = model->item(index, 1)->text();
        debugApp() << "Запуск конфигурации для:" << currentSelected << "с MAC-адресом:" << mac;
    }

    if (modified || currentFilePath.isEmpty()){
        QMessageBox msgBox(QMessageBox::Question,
                           "Внимание",
                           "Конфигурация была изменена или не сохранена.\n"
                           "Сохранить и сконфигурировать?",
                           QMessageBox::Ok | QMessageBox::Cancel,
                           this);
        // Запускаем в синхронном режиме и получаем результат
        int result = msgBox.exec();
        if (result == QMessageBox::Ok) {
            // Код для сохранения файла
            if (!saveFile())
                return;
        } else if (result == QMessageBox::Cancel) {
            // Код для отмены действия
            return;
        }
    }
    if (!lbplc)
        return;

    LogicBoxTarget target;
    if (!resolveTargetForSelection(currentSelected, mac, &target))
        return;
    lbplc->startConf(target, currentFilePath);
}

QList<QAction *> ConfigDockWidget::activeTextActions() const
{
    if (yamlPage) {
        return yamlPage->textActions();
    }
    return QList<QAction*>();
}

void ConfigDockWidget::scrollToSelectedPlc(int index)
{
    if (index < 0) return;
    // Извлекаем сохраненную модель
    switch (sidebarMenu->currentRow()) {
    case 0:{
        QStandardItemModel* model = qobject_cast<QStandardItemModel*>(plcSelector->model());
        if (!model) return;
        QVariant lineData = model->item(index, 0)->data(Qt::UserRole);
        if (!lineData.isValid()) return;
        int targetLine = lineData.toInt();
        yamlPage->scrollToLine(targetLine);
        yamlParser->setlbhost(plcSelector->currentText());
        plcName = plcSelector->currentText();
    }
    break;
    case 1:{
        yamlParser->setlbhost(plcSelector->currentText());
        plcName = plcSelector->currentText();
        varPage->updateData(yamlParser);
    }
    break;
    case 2:{
        yamlParser->setlbhost(plcSelector->currentText());
        plcName = plcSelector->currentText();
        devicePage->updateData(yamlParser);
    }
    break;
    default:
        break;
    }
}

bool ConfigDockWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == plcSelector) {
        if (event->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            QString currentSelected = plcSelector->currentText();

            plcSelector->blockSignals(true);
            plcSelector->clear();

            // Передаем актуальный текст из редактора в наш метод
            plcSelector->setModel(createPlcModel(yamlPage->text()));
            plcSelector->setModelColumn(0);

            // Восстанавливаем позицию
            int index = plcSelector->findText(currentSelected);
            plcSelector->setCurrentIndex(index); // Если index == -1, Qt сам покажет placeholder

            plcSelector->blockSignals(false);
        }
    }

    return QDockWidget::eventFilter(watched, event);
}

void ConfigDockWidget::closeEvent(QCloseEvent *event)
{
    // 1. Сначала синхронизируем вкладки Var View и IO View.
    // Если пользователь нажал Cancel в этом диалоге, отменяем закрытие.
    if (isModifiedPages(true) == QMessageBox::Cancel) {
        event->ignore(); // Не закрывать док
        return;
    }

    // 2. Теперь проверяем, изменен ли сам YAML (или был изменен только что через вкладки)
    if (modified) {
        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setWindowTitle("Закрытие конфигурации");
        msgBox.setText(QString("Конфигурация \"%1\" была изменена.").arg(plcName));
        msgBox.setInformativeText("Хотите сохранить изменения перед закрытием?");

        // Создаем кнопки с понятным текстом
        QPushButton *saveButton = msgBox.addButton("Сохранить", QMessageBox::AcceptRole);
        QPushButton *discardButton = msgBox.addButton("Закрыть без сохранения", QMessageBox::DestructiveRole);
        QPushButton *cancelButton = msgBox.addButton("Отмена", QMessageBox::RejectRole);

        msgBox.setDefaultButton(cancelButton);
        msgBox.exec();

        if (msgBox.clickedButton() == saveButton) {
            // Пытаемся сохранить файл
            if (saveFile())
                event->accept(); // Сохранено успешно, закрываем док
            else
                event->ignore(); // Ошибка сохранения или отмена в диалоге Сохранить Как
        }
        else if (msgBox.clickedButton() == discardButton) {
            event->accept(); // Закрываем без сохранения (данные теряются)
        }
        else if (msgBox.clickedButton() == cancelButton) {
            event->ignore(); // Возврат к работе над конфигурацией
        }
    } else {
        // Если изменений не было, просто закрываем
        event->accept();
    }
}


QString ConfigDockWidget::getPlcName() const
{
    return plcName;
}

void ConfigDockWidget::updateTitle()
{
    QString prefix = modified ? "* " : "";
    setWindowTitle(QString("%1Конфигурация: %2").arg(prefix, plcName));
}

QStandardItemModel *ConfigDockWidget::createPlcModel(const QString &yamlText)
{
    yamlParser->setConfig(yamlPage->text(), lbyaml::data);
    QMultiMap<QString, lbyaml::lbhost> mmap = yamlParser->getallhostline();
    // lbyaml *y = new lbyaml(yamlText, lbyaml::data);
    // QMultiMap<QString, lbyaml::lbhost> mmap = y->getallhostline();
    // y->deleteLater();

    QStandardItemModel* model = new QStandardItemModel(mmap.size(), 2, this);
    int row = 0;
    for (auto it = mmap.constBegin(); it != mmap.constEnd(); ++it) {
        // Колонка 0: Имя ПЛК
        QStandardItem* nameItem = new QStandardItem(it.key());
        nameItem->setData(it.value().line, Qt::UserRole);
        model->setItem(row, 0, nameItem);

        // Колонка 1: MAC-адрес
        QStandardItem* macItem = new QStandardItem(it.value().mac);
        macItem->setForeground(QBrush(Qt::gray)); // Сохраняем ваш серый цвет для MAC
        model->setItem(row, 1, macItem);

        row++;
    }
    return model;
}

void ConfigDockWidget::onSidebarRowChanged(int index)
{
    if (index < 0 || !stackedContainer) return;

    // Переключаем видимый виджет в контейнере
    stackedContainer->setCurrentIndex(index);

    // Логика синхронизации данных между представлениями
    if (index == 1) {
        isModifiedPages();
        if (modified)
            yamlParser->setConfig(yamlPage->text(), lbyaml::data);
        if (varPage)
            varPage->updateData(yamlParser);
    }
    else if (index == 2) {
        // Пользователь перешел во вкладку "Модули I/O" (DeviceView)
        // Аналогично парсим YAML под нужды конфигуратора модулей
        isModifiedPages();
        if (modified)
            yamlParser->setConfig(yamlPage->text(), lbyaml::data);
        if (devicePage)
            devicePage->updateData(yamlParser);
    }
    else if (index == 0) {
        isModifiedPages();
        scrollToSelectedPlc(plcSelector->currentIndex());
    }
}

void ConfigDockWidget::onAddVariableToWatch(const QString &varName)
{
    const QString currentPlc = plcSelector->currentText();
    if (currentPlc.isEmpty()) {
        QMessageBox::warning(this, "Внимание", "Не выбрана конфигурация");
        return;
    }

    const int index = plcSelector->findText(currentPlc);
    QStandardItemModel *model = qobject_cast<QStandardItemModel*>(plcSelector->model());
    const QString mac = (model && index >= 0 && model->item(index, 1))
        ? model->item(index, 1)->text() : QString();

    LogicBoxTarget watchTarget;
    if (!resolveTargetForSelection(currentPlc, mac, &watchTarget))
        return;

    WatchDockWidget *watch = nullptr;
    for (WatchDockWidget *candidate : p_mainWindow->getWatchDocks()) {
        if (candidate && candidate->getTarget().routeKey() == watchTarget.routeKey()) {
            watch = candidate;
            break;
        }
    }

    if (!watch) {
        debugApp() << "onAddVariableToWatch: New Watch"
                   << watchTarget.endpoint.displayString() << currentPlc << varName;
        watch = p_mainWindow->createWatchDockWidget(watchTarget);
        watch->addVar(varName);
        watch->toggleConnection();
    } else {
        debugApp() << "onAddVariableToWatch:"
                   << watchTarget.endpoint.displayString() << currentPlc << varName;
        watch->addVar(varName);
        if (!watch->isConnected())
            watch->toggleConnection();
    }
    watch->show();
    watch->raise();
}

bool ConfigDockWidget::replacePlcBlockInYaml(const QString &newPlcBlockText)
{
    if (!yamlParser)
        yamlParser = new lbyaml(QString(), lbyaml::data, this);
    QMap<int, QStringList> plcLineMap;
    QMultiMap<QString, lbyaml::lbhost> mmap = yamlParser->getallhostline();

    for (auto it = mmap.constBegin(); it != mmap.constEnd(); ++it) {
        QStringList plcInfo;
        plcInfo << it.key() << it.value().mac;
        plcLineMap.insert(it.value().line, plcInfo);
    }

    QString currentMac;
    QStandardItemModel* comboModel = qobject_cast<QStandardItemModel*>(plcSelector->model());
    int comboIndex = plcSelector->currentIndex();
    if (comboModel && comboIndex >= 0) {
        currentMac = comboModel->item(comboIndex, 1)->text();
    }

    auto targetIt = plcLineMap.end();
    for (auto it = plcLineMap.begin(); it != plcLineMap.end(); ++it) {
        if (it.value().at(0) == plcName && it.value().at(1) == currentMac) {
            targetIt = it;
            break;
        }
    }

    int startLine = 0;
    int endLine = 0;

    if (targetIt == plcLineMap.end()) {
        auto reply = QMessageBox::question(this, "Конфигурация не найдена",
                                           QString("ПЛК %1 (MAC: %2) не найден в файле.\n"
                                                   "Хотите создать для него новую конфигурацию в начале файла?")
                                               .arg(plcName, currentMac),
                                           QMessageBox::Yes | QMessageBox::No);

        if (reply != QMessageBox::Yes) {
            return false;
        }

        startLine = 0;
        endLine = 0;
    }
    else {
        startLine = targetIt.key();
        auto nextIt = std::next(targetIt);

        if (nextIt != plcLineMap.end()) {
            endLine = nextIt.key() - 1;
        } else {
            endLine = -1;
        }
    }

    const QString oldYamlText = yamlPage->text();

    yamlPage->replacePlcBlock(
        startLine,
        endLine,
        newPlcBlockText
    );

    const QString candidateYamlText = yamlPage->text();

    try {
        yamlParser->setConfig(
            candidateYamlText,
            lbyaml::data
        );
    }
    catch (...) {
        debugApp() << "replacePlcBlockInYaml: invalid YAML, rollback";

// #ifndef NDEBUG
//         // Keep the failed candidate only in debug builds and outside the
//         // working directory. Production builds must not leave a copy of PLC
//         // configuration next to the executable/project.
//         const QString debugPath =
//             QStandardPaths::writableLocation(QStandardPaths::TempLocation)
//             + "/lbcfg-sync-candidate.yaml";
//         QFile debugFile(debugPath);
//         if (debugFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
//             QTextStream out(&debugFile);
//             out << candidateYamlText;
//             debugFile.close();
//             debugApp() << "Invalid YAML candidate saved to" << debugPath;
//         }
// #endif

        // Возвращаем пользователю заведомо валидный исходный текст.
        yamlPage->setText(oldYamlText);

        // И возвращаем parser в согласованное состояние.
        yamlParser->setConfig(
            oldYamlText,
            lbyaml::data
        );

        throw;
    }

    return true;
}

int ConfigDockWidget::isModifiedPages(bool allowCancel)
{
    // Проверяем, есть ли вообще изменения во вкладках
    bool hasChanges = varPage->isModified() || devicePage->isModified();
    if (!hasChanges) {
        return QMessageBox::No; // Изменений нет, можно продолжать стандартный флоу
    }

    // Настраиваем конфигурацию кнопок в зависимости от контекста
    QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No;
    if (allowCancel) {
        buttons |= QMessageBox::Cancel;
    }

    // Показываем диалог
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Изменение конфигурации",
        "Данные в визуальных вкладках были изменены. Обновить исходный YAML?",
        buttons
        );

    // Если пользователь нажал Cancel или No, изменения в YAML не вносятся
    if (reply == QMessageBox::Cancel || reply == QMessageBox::No) {
        return reply;
    }

    try {
        debugApp() << "SYNC: begin";
        if (varPage->isModified()){
            debugApp() << "SYNC: getUpdatedData";
            auto updatedData = varPage->getUpdatedData();
            debugApp() << "SYNC: implementLbVarMap";
            yamlParser->implementLbVarMap(updatedData);
            debugApp() << "SYNC: getFormattedYaml";
            QString plcYamlText = yamlParser->getFormattedYaml(lbyaml::retainY);
            debugApp() << "SYNC: formatted YAML size" << plcYamlText.size();
            debugApp() << "SYNC: replacePlcBlockInYaml";
            if (!replacePlcBlockInYaml(plcYamlText)) {
                debugApp() << "SYNC: replacePlcBlockInYaml returned false";
                QMessageBox::critical(
                    this,
                    "Ошибка синхронизации",
                    "Не удалось заменить блок ПЛК в YAML."
                );
                return QMessageBox::No;
            }
            debugApp() << "SYNC: Var View completed";
        }
        else if (devicePage->isModified()){
            debugApp() << "SYNC: devicePage getUpdateData";
            // Забираем измененную структуру в виде JSON
            QJsonObject updatedJson = devicePage->getUpdateData();
            debugApp() << "SYNC: devicePage getlbconf";
            // Генерируем текст измененного блока модулей ПЛК
            QString updatedYamlText = yamlParser->getlbconf(updatedJson);
            debugApp() << "SYNC: devicePage replacePlcBlockInYaml";
            if (!replacePlcBlockInYaml(updatedYamlText)) {
                QMessageBox::critical(
                    this,
                    "Ошибка синхронизации",
                    "Не удалось заменить блок ПЛК в YAML."
                );

                return QMessageBox::No;
            }
            debugApp() << "SYNC: IO View completed";
        }
        debugApp() << "SYNC: end";
    }
    catch (const std::exception &e) {
        QString errorText =
            QString("Ошибка преобразования конфигурации: %1")
                .arg(QString::fromUtf8(e.what()));

        debugApp() << "SYNC EXCEPTION:" << errorText;

        QMessageBox::critical(
            this,
            "Ошибка синхронизации YAML",
            errorText
        );

        return QMessageBox::No;
    }
    catch (...) {
        debugApp() << "SYNC: unknown exception";

        QMessageBox::critical(
            this,
            "Ошибка синхронизации YAML",
            "Возникло неизвестное исключение при обновлении YAML."
        );

        return QMessageBox::No;
    }
    return reply;
}

QAction *ConfigDockWidget::getConfigureAction() const
{
    return confAction;
}

QString ConfigDockWidget::getCurrentFilePath() const
{
    return currentFilePath;
}
