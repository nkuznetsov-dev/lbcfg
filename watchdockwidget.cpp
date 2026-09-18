#include "watchdockwidget.h"
#include <QHeaderView>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMessageBox>
#include "logmanager.h"
#include "plcmanager.h"



WatchDockWidget::WatchDockWidget(const LogicBoxTarget &initialTarget, QWidget *parent)
    : QDockWidget{QString("Watch: %1").arg(initialTarget.displayName()), parent}, target(initialTarget)
{
    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const int elementHeight = 22;
    QPushButton *addBtn = new QPushButton(this);
    addBtn->setFixedSize(elementHeight, elementHeight);
    addBtn->setToolTip("Добавить переменную вручную");
    addBtn->setText("+");

    QPushButton *remBtn = new QPushButton(this);
    remBtn->setFixedSize(elementHeight, elementHeight);
    remBtn->setToolTip("Удалить выбранную переменную");
    remBtn->setText("-");

    QPushButton *ipBtn = new QPushButton(this);
    ipBtn->setFixedSize(elementHeight, elementHeight);
    ipBtn->setToolTip("Указать IP адрес");
    ipBtn->setText("IP");

    intervalSpin = new QDoubleSpinBox(this);
    intervalSpin->setFixedHeight(elementHeight);
    intervalSpin->setRange(0.1, 10.0);        // Мин и макс значения
    intervalSpin->setValue(1.0);               // Значение по умолчанию (1 секунда)
    intervalSpin->setSingleStep(0.1);          // Начальный шаг
    intervalSpin->setDecimals(1);              // Один знак после запятой (для 0.1)
    intervalSpin->setSuffix(" s");             // Красивый суффикс "s" (секунды)
    intervalSpin->setToolTip("Интервал опроса");
    intervalSpin->setEnabled(false);

    connBtn = new QPushButton(this);
    connBtn->setFixedHeight(elementHeight);
    connBtn->setFixedWidth(70);
    connBtn->setToolTip("Подключиться к PLC для отладки");
    connBtn->setText("Connect");

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(2);
    buttonLayout->addWidget(addBtn);
    buttonLayout->addWidget(remBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(connBtn);
    buttonLayout->addWidget(intervalSpin);
    buttonLayout->addWidget(ipBtn);

    layout->addLayout(buttonLayout);

    watch = new QTableView(this);
    watch->setShowGrid(true);
    watch->setSelectionBehavior(QAbstractItemView::SelectRows);
    watch->setSelectionMode(QAbstractItemView::SingleSelection);
    watch->setAlternatingRowColors(true);
    watch->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(watch, &QTableView::customContextMenuRequested,
            this, &WatchDockWidget::showContextMenu);
    // Разрешаем сброс данных на саму таблицу watch
    watch->setAcceptDrops(true);
    // Устанавливаем фильтр событий на таблицу, чтобы перехватывать Drag & Drop
    watch->installEventFilter(this);


    QHeaderView *header = watch->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(true);
    header->setVisible(true);

    QHeaderView *v_header = watch->verticalHeader();
    v_header->setDefaultSectionSize(22);
    v_header->setVisible(true);

    watchModel = new QStandardItemModel(this);
    watch->setModel(watchModel);
    watchModel->setHorizontalHeaderLabels({"var name","value","set value", "force value"});

    layout->addWidget(watch);
    setWidget(container);

    connect(addBtn, &QPushButton::clicked, this, [this](){
        addVar();
    });

    connect(ipBtn, &QPushButton::clicked, this, [this, ipBtn](){
        showIpEditDialog(ipBtn);
    });

    connect(remBtn, &QPushButton::clicked, this, [this](){
        QModelIndex currentIndex = watch->currentIndex();
        if (currentIndex.isValid()) {
            watchModel->removeRow(currentIndex.row());
            updateSessionVariables();
        }
    });

    connect(connBtn, &QPushButton::clicked, this, [this](){
        toggleConnection();
    });

    connect(watchModel, &QStandardItemModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
        // Проверяем, что изменилась именно первая колонка (var name)
        if (topLeft.column() == 0) {
            updateSessionVariables();
            if (session && session->isConnected())
                updateTableColors();
            // Проверяем, что изменилась именно третья колонка (set value)
        }else if (topLeft.column() == 2){
            int row = topLeft.row();
            QString val = topLeft.data().toString().trimmed();
            if (val.isEmpty()) return;
            QStringList get = {"get"};
            get.append(collectVariables());

            QStringList set = {"set", QString("%1=%2")
                                          .arg(watchModel->item(row, 0)->text())
                                          .arg(val)};
            if (session && session->isConnected())
            {
                session->setQuery({get, set});
                debugApp() << "WatchDockWidget: Список переменных для записи:" << get << set;
                watchModel->blockSignals(true);
                watchModel->item(row, 2)->setText("");
                watchModel->blockSignals(false);
            }
        }else if (topLeft.column() == 3){
            QStringList get = {"get"};
            get.append(collectVariables());
            int row = topLeft.row();
            QString varName = watchModel->item(row, 0)->text();
            QString val = topLeft.data().toString().trimmed();
            if (val != "")
            {
                QStringList force = {"force", QString("%1=%2")
                                                  .arg(varName)
                                                  .arg(val)};
                if (session && session->isConnected()){
                    session->setQuery({get, force});
                    debugApp() << "WatchDockWidget: Список переменных для FORCE:" << get << force;
                    forcedVar.insert(varName);
                    updateTableColors();
                }
            }
        }
    });

    static double lastValue = 1.0;
    connect(intervalSpin, &QDoubleSpinBox::valueChanged, this, [this](double value){
        intervalSpin->blockSignals(true);

        if (qFuzzyCompare(lastValue, 1.0)){
            if (value > 1.0){
                intervalSpin->setSingleStep(1.0);
                intervalSpin->setValue(2.0);
            }else{
                intervalSpin->setSingleStep(0.1);
                intervalSpin->setValue(0.9);
            }
        }

        lastValue = intervalSpin->value();

        intervalSpin->blockSignals(false);
        if (session) {
            int msec = static_cast<int>(intervalSpin->value() * 1000);
            session->setTimeOut(msec);
            debugApp() << "Интервал опроса изменен на:" << msec << "мс";
        }
    });
}


QString WatchDockWidget::getPlcName() const
{
    return target.name;
}

const LogicBoxTarget &WatchDockWidget::getTarget() const
{
    return target;
}

void WatchDockWidget::setTarget(const LogicBoxTarget &newTarget)
{
    target = newTarget;
    setWindowTitle(QString("Watch: %1").arg(target.displayName()));
    debugApp() << "WatchDockWidget set target:"
               << target.name << target.endpoint.displayString();
}

bool WatchDockWidget::setEndpointText(const QString &text)
{
    const LbEndpoint endpoint = LbEndpoint::fromString(text, 502);
    if (!endpoint.isValid()) {
        debugApp() << "WatchDockWidget rejected endpoint:" << text;
        return false;
    }
    target.endpoint = endpoint;
    return true;
}

void WatchDockWidget::showIpEditDialog(QPushButton *anchorButton)
{
    if (isConnected()) {
        QMessageBox::information(this, "Watch подключён",
                                 "Сначала отключите Watch, затем измените endpoint.");
        return;
    }

    QLineEdit *ipEdit = new QLineEdit(this);
    ipEdit->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    ipEdit->setAttribute(Qt::WA_DeleteOnClose);
    ipEdit->setText(target.endpoint.isValid() ? target.endpoint.displayString() : QString());
    ipEdit->setPlaceholderText("Enter IP...");
    ipEdit->setMinimumWidth(150);

    // ipEdit->setStyleSheet("QLineEdit { border: 1px solid #999; padding: 2px; background: white; }");

    QPoint globalPos = anchorButton->mapToGlobal(QPoint(0, 0));
    ipEdit->setGeometry(globalPos.x(), globalPos.y(), ipEdit->minimumWidth(), anchorButton->height());

    connect(ipEdit, &QLineEdit::returnPressed, this, [this, ipEdit](){
        QString text = ipEdit->text().trimmed();
        if (!text.isEmpty() && !this->setEndpointText(text)) {
            QMessageBox::warning(this, "Некорректный endpoint",
                                 "Для IPv6 link-local необходимо указать scope интерфейса, например fe80::...%45");
        }
        ipEdit->close();
    });

    ipEdit->show();
    ipEdit->setFocus();
    ipEdit->selectAll();
}

void WatchDockWidget::toggleConnection()
{
    if (session && session->isConnected()){
        session->stop();
        return;
    }

    plcManager::CommandContext ctx;
    ctx.target = target;

    QStringList param = collectVariables();

    WatchSession *old_session = session;

    session = plcManager::instanse()->startWatch(ctx, param, this);
    if (!session)
        return;
    if (session != old_session){
        connect(session, &WatchSession::watchExeComleted, this, &WatchDockWidget::receiveData);
        connect(session, &WatchSession::connected, this, [this](){
            connBtn->setToolTip("Отключиться от PLC для отладки");
            connBtn->setText("Disconnect");
            intervalSpin->setEnabled(true);
            updateTableColors();
        });
        connect(session, &WatchSession::disconnected, this, [this](){
            connBtn->setToolTip("Подключиться к PLC для отладки");
            connBtn->setText("Connect");
            intervalSpin->setEnabled(false);
            updateTableColors();
            session->deleteLater();
        });
        session->start();
    }
}

bool WatchDockWidget::isConnected() const
{
    return session && session->isConnected();
}

bool WatchDockWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == watch) {
        if (event->type() == QEvent::DragEnter) {
            // qDebug()<< "event->type() == QEvent::DragEnter";
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasText()) {
                dragEvent->acceptProposedAction();
                return true; // Перехватили, Qt дальше не передает
            }
        }
        else if (event->type() == QEvent::DragMove) {
            // qDebug()<< "event->type() == QEvent::DragMove";
            QDragMoveEvent *moveEvent = static_cast<QDragMoveEvent*>(event);
            moveEvent->acceptProposedAction();
            return true;
        }
        else if (event->type() == QEvent::Drop) {
            // qDebug()<< "event->type() == QEvent::Drop";
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);

            // Просто извлекаем текст напрямую!
            QString varName = dropEvent->mimeData()->text().trimmed();

            if (!varName.isEmpty()) {
                this->addVar(varName); // Добавляем в таблицу
            }

            dropEvent->setDropAction(Qt::CopyAction);
            dropEvent->accept();
            return true; // Полностью блокируем стандартный drop таблицы/модели
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void WatchDockWidget::receiveData(const QStringList &data)
{
    debugApp()<<"data receive from:"<< target.name << data;
    for (int i = 0; i < data.size(); ++i) {
        if (i < watchModel->rowCount()) {
            QStandardItem *valueItem = watchModel->item(i, 1);
            if (valueItem) {
                valueItem->setText(data.at(i));
            }
        }
    }
}

QStringList WatchDockWidget::collectVariables() const
{
    QStringList param;
    for (int row = 0; row < watchModel->rowCount(); ++row) {
        QStandardItem *item = watchModel->item(row, 0);
        if (item && !item->text().isEmpty()) {
            param.append(item->text());
        }
    }
    return param;
}

void WatchDockWidget::updateSessionVariables()
{
    if (session && session->isConnected()) {
        QStringList param = collectVariables();
        session->setQuery(collectVariables());
        debugApp() << "WatchDockWidget: Список переменных опроса динамически обновлен:" << param;
    }
}

void WatchDockWidget::showContextMenu(const QPoint &pos)
{
    QModelIndex index = watch->indexAt(pos);
    if (!index.isValid()) {
        return; // Кликнули по пустому месту таблицы, меню не показываем
    }
    int row = index.row();

    QStandardItem *varItem = watchModel->item(row, 0);
    if (!varItem || varItem->text().isEmpty()) {
        return; // Строка пустая, меню не показываем
    }
    QString varName = varItem->text();


    QMenu menu(this);

    QAction *unforce = menu.addAction("Unforce");
    unforce->setEnabled(session && session->isConnected());

    connect(unforce, &QAction::triggered, this, [this,varName, row](){
        if (session){
            QStringList get = {"get"};
            get.append(collectVariables());

            QStringList unforce = {"unforce", varName};
            session->setQuery({get, unforce});
            debugApp() << "WatchDockWidget: Сброс FORCE для:" << varName;
            forcedVar.remove(varName);
            updateTableColors();

            QStandardItem *fItem = watchModel->item(row, 3);
            if (fItem) {
                fItem->setText("");
            }
        }
    });


    menu.exec(watch->viewport()->mapToGlobal(pos));
}

void WatchDockWidget::updateTableColors()
{
    bool connected = (session && session->isConnected());
    debugApp()<< "QSet<QString> forcedVar is :" << forcedVar;
    for (int row = 0; row < watchModel->rowCount(); ++row) {
        // Получаем имя переменной из первой колонки
        QStandardItem *varItem = watchModel->item(row, 0);
        if (!varItem || varItem->text().isEmpty()) continue;

        QString varName = varItem->text();

        // По умолчанию — стандартный цвет (прозрачный/системный)
        QBrush rowColor = QBrush();

        if (connected) {
            // Если имя переменной есть в вашем QSet forcedVar — красим в красный
            if (forcedVar.contains(varName)) {
                rowColor = QBrush(QColor(255, 200, 200)); // Мягкий красный
            } else {
                rowColor = QBrush(QColor(220, 245, 220)); // Мягкий зеленый (успешный опрос)
            }
        }

        // Применяем цвет фона ко 2 колонке в текущей строке
        QStandardItem *cellItem = watchModel->item(row, 1);
        if (cellItem) {
            cellItem->setData(rowColor, Qt::BackgroundRole);
        }
    }
}

void WatchDockWidget::addVar(const QString &varName)
{
    QStandardItem *varNameItem   = new QStandardItem();
    QStandardItem *valueItem     = new QStandardItem();
    QStandardItem *setValueItem  = new QStandardItem();
    QStandardItem *forceValueItem = new QStandardItem();
    valueItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    watchModel->appendRow({varNameItem, valueItem, setValueItem, forceValueItem});
    if (varName.isEmpty())
        return;
    varNameItem->setText(varName);
}
