#include "discoverdockwidget.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QApplication>
#include <QMenu>
#include <QClipboard>
#include "logmanager.h"
#include "commandmanager.h"

DiscoverDockWidget::DiscoverDockWidget(QWidget *parent)
    : QDockWidget("Discover", parent), lbplc(plcManager::instanse())
{
    QWidget *content = new QWidget(this);
    setWidget(content);
    QVBoxLayout *vbox = new QVBoxLayout(content);
    btnDiscover = new QPushButton("Send Discover");
    QHBoxLayout *hbox = new QHBoxLayout();

    vbox->setSpacing(0);
    hbox->addWidget(btnDiscover);
    hbox->addStretch();
    vbox->addLayout(hbox);

    // Создаем таблицу
    table = new QTableWidget(this);
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({"Name", "Type", "IPv4", "MAC", "Delays (ms)", "IF"});
    // Выделять строку целиком при клике на любую ячейку
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // разрешить выбирать только одну строку за раз
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    // запретить редактирование ячеек, чтобы они не открывались по двойному клику
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setColumnHidden(6, true);
    QHeaderView *header = table->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    // Остальные столбцы: подгоняем под содержимое текста
    for (int i = 1; i < table->columnCount(); ++i) {
        header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    header->setStyleSheet(
        "QHeaderView::section {"
        "    background-color: #f0f0f0;"
        "    font-weight: bold;"
        "    border: 1px solid #dcdcdc;"
        "    padding-left: 4px;" // Отступ только слева, чтобы текст не прилипал
        "    height: 20px;"      // Подсказка для высоты
        "}"
        );
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    vbox->addWidget(table);

    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableView::customContextMenuRequested, this, &DiscoverDockWidget::showContextMenu);


    connect(
        btnDiscover,
        &QPushButton::clicked,
        lbplc,
        &plcManager::startDiscover);

    connect(lbplc, &plcManager::discoverCompleted,
            this, &DiscoverDockWidget::discoverReceived);

    connect(
        table,
        &QTableWidget::cellDoubleClicked,
        this,
        &DiscoverDockWidget::onTableDoubleClicked);

    connect(lbplc, &plcManager::discoverStarting,
            this, &DiscoverDockWidget::cleanRow);
}



void DiscoverDockWidget::cleanRow()
{
    table->clearContents();
    table->setRowCount(0); // Очищаем старые строки
}

void DiscoverDockWidget::onTableDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    if (QApplication::mouseButtons() != Qt::LeftButton) {
        return;
    }
    const LogicBoxTarget target = targetForRow(row);
    if (target.isValid())
        emit deviceSelected(target);
}

void DiscoverDockWidget::showContextMenu(const QPoint &pos)
{
    QTableWidgetItem *item = table->itemAt(pos);
    if (!item) return;
    const int row = item->row();
    const LogicBoxTarget target = targetForRow(row);
    if (!target.isValid())
        return;
    const QTableWidgetItem *keyItem = table->item(row, 6);
    const QString discoveryKey = keyItem ? keyItem->text() : QString();
    plcManager::CommandContext ctx;
    ctx.target = target;

    QMenu menu(this);
    QAction *AddDivice = menu.addAction("Добавить");
    // 2. Делаем его жирным
    QFont font = AddDivice->font();
    font.setBold(true);
    AddDivice->setFont(font);
    QAction *getConf = menu.addAction("Запросить конфигурацию");
    QAction *newConf = menu.addAction("Создать новую конфигурацию");
    QAction *copy = menu.addAction("Копировать строку");
    QAction *Allcopy = menu.addAction("Копировать всё");
    QAction *MacCopy = menu.addAction("Копировать MAC");
    QAction *ipv6Copy = menu.addAction("Копировать IPv6");
    menu.addSeparator();
    CommandManager::instance()->getUptimeAction(ctx, &menu);
    CommandManager::instance()->getRestartAction(ctx, &menu);
    CommandManager::instance()->getFlashAction(ctx, &menu);
    menu.addSeparator();
    CommandManager::instance()->getLogMenu(ctx, &menu);

    QAction *selectedItem = menu.exec(table->viewport()->mapToGlobal(pos));
    QClipboard *clipboard = QGuiApplication::clipboard();

    if (selectedItem == AddDivice){
        emit deviceSelected(ctx.target);
    }else if (selectedItem == copy) {
        clipboard->setText(ldmap.value(discoveryKey).toString());
    }else if (selectedItem == Allcopy) {
        QStringList qstr;
        for (auto i : ldmap) {
            qstr << i.toString();
        }
        clipboard->setText(qstr.join("\n"));
    }else if (selectedItem == getConf) {
        emit deviceSelected(ctx.target);
        emit requestConfig(ctx.target);
    }else if (selectedItem == newConf){
        emit newConfig(ctx.target);
    }else if (selectedItem == MacCopy){
        clipboard->setText(ldmap.value(discoveryKey).mac);
    }else if (selectedItem == ipv6Copy){
        clipboard->setText(ctx.target.endpoint.hostString());
    }
}

// void DiscoverDockWidget::fillTable()
// {

// }

void DiscoverDockWidget::discoverReceived(const QMap<QString, discover::lbinfo> &DiscoverMap)
{
    ldmap = DiscoverMap;
    table->setSortingEnabled(false); // Отключаем сортировку на время вставки для скорости
    int row = 0;
    for (auto it = ldmap.begin(); it != ldmap.end(); ++it) {
        debugPLC() << it.value();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(it.value().name));
        table->setItem(row, 1, new QTableWidgetItem(it.value().type));
        table->setItem(row, 2, new QTableWidgetItem(it.value().ipv4));
        table->setItem(row, 3, new QTableWidgetItem(it.value().mac));
        QStringList strList;
        for (float val : it.value().delay)
            strList << QString::number(val);
        table->setItem(row, 4, new QTableWidgetItem(strList.join(",")));
        strList.clear();
        for (int val : it.value().ifindex)
            strList << QString::number(val);
        table->setItem(row, 5, new QTableWidgetItem(strList.join(",")));

        for (int col = 0; col < table->columnCount(); ++col) {
            QTableWidgetItem *item = table->item(row, col);
            if (item) {
                item->setToolTip(it.key());
                if (it.value().btn) {
                    item->setBackground(alertColor);
                    QFont font = item->font();
                    font.setBold(true);
                    item->setFont(font);
                }
            }
        }
        table->setItem(row, 6, new QTableWidgetItem(it.key()));
        row++;
    }
    table->setSortingEnabled(true); // Возвращаем возможность сортировки
}

LogicBoxTarget DiscoverDockWidget::targetForRow(int row) const
{
    LogicBoxTarget target;
    QTableWidgetItem *keyItem = table ? table->item(row, 6) : nullptr;
    if (!keyItem)
        return target;

    const auto it = ldmap.constFind(keyItem->text());
    if (it == ldmap.constEnd())
        return target;

    target.name = it.value().name;
    target.mac = it.value().mac;
    target.endpoint = it.value().endpoint;
    return target;
}

QMap<QString, discover::lbinfo> DiscoverDockWidget::getLdmap() const
{
    return ldmap;
}
