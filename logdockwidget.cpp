#include "logdockwidget.h"
#include "qmenu.h"
#include <QMetaEnum>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QFileDialog>

LogDockWidget::LogDockWidget(QWidget *parent)
    : QDockWidget{"LogViewer", parent}
{
    setAttribute(Qt::WA_DeleteOnClose);

    QWidget *centralWidget = new QWidget(this);

    logViewer = new QPlainTextEdit(centralWidget);
    logViewer->setReadOnly(true);
    logViewer->setMinimumHeight(50); // Минумум, чтобы совсем не пропал
    logViewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->setMinimumSize(QSize(0, 50)); // Сбрасываем ограничения самого дока
    logViewer->setMaximumBlockCount(1000);

    logViewer->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(logViewer, &QPlainTextEdit::customContextMenuRequested,
            this, &LogDockWidget::showContextMenu);

    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(logViewer);
    setWidget(centralWidget);


    const int elementHeight = 14;
    stopButton = new QPushButton(logViewer);
    stopButton->setFixedSize(elementHeight, elementHeight);
    stopButton->setToolTip(tr("Остановить"));
    // Применяем ваш StyleSheet для квадратной красной кнопки
    stopButton->setStyleSheet(
        "QPushButton {"
        "   background-color: #FF0000;" /* Чистый красный цвет */
        "   border: 1px solid #CC0000;" /* Темно-красная аккуратная рамка */
        "   border-radius: 1px;"        /* Минимальное сглаживание углов */
        "}"
        "QPushButton:hover {"
        "   background-color: #D60000;" /* Цвет при наведении курсора (становится темнее) */
        "}"
        "QPushButton:pressed {"
        "   background-color: #A30000;" /* Цвет при клике (эффект нажатия) */
        "}"
        );

    stopButton->hide();
    connect(stopButton, &QPushButton::clicked, this, &LogDockWidget::stopButtonPressed);

    logViewer->installEventFilter(this);
    logViewer->verticalScrollBar()->installEventFilter(this);

    connect(LogManager::catcher(), &LogCatcher::newLogEntry,
            this, &LogDockWidget::appendLogEntry);
}

void LogDockWidget::onLogStarted()
{
    stopButton->show();
    updateButtonPosition();
}

void LogDockWidget::onLogFinished()
{
    stopButton->hide();
}

bool LogDockWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == logViewer && event->type() == QEvent::Resize) {
        updateButtonPosition();
    }
    if (obj == logViewer->verticalScrollBar()) {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
            updateButtonPosition();
        }
    }
    return QDockWidget::eventFilter(obj, event);
}

void LogDockWidget::appendLogEntry(const QDateTime &timestamp,
                                   LogCatcher::Source source,
                                   LogCatcher::Level level,
                                   const QString &message,
                                   const plcManager::CommandContext &ctx,
                                   LogCatcher::Wrapped wrap,
                                   LogCatcher::TimeType timeType)
{
    QString timeStr;
    QString timeColor = "gray";
    switch (timeType) {
    case LogCatcher::TimeUptime:{
        qint64 totalMs = timestamp.toMSecsSinceEpoch();

        qint64 msecs = totalMs % 1000;
        qint64 totalSeconds = totalMs / 1000;

        qint64 seconds = totalSeconds % 60;
        qint64 totalMinutes = totalSeconds / 60;

        qint64 minutes = totalMinutes % 60;
        qint64 totalHours = totalMinutes / 60;

        qint64 hours = totalHours % 24;
        qint64 days = totalHours / 24;

        timeStr = QString("%1:%2:%3.%4")
                      .arg(hours, 2, 10, QChar('0'))
                      .arg(minutes, 2, 10, QChar('0'))
                      .arg(seconds, 2, 10, QChar('0'))
                      .arg(msecs, 3, 10, QChar('0'));

        if (days > 0) {
            timeStr = QString("%1:%2").arg(days).arg(timeStr);
        }
        timeColor = "#483D8B";
        break;
    }
    default:
        timeStr = timestamp.toString("HH:mm:ss.zzz");
        break;
    }

    QString sourceStr;
    QString sourceColor = "gray";
    switch (source) {
    case LogCatcher::App:
        sourceStr = "[APP]";
        sourceColor = "SteelBlue";
        break;
    case LogCatcher::PLC:
        sourceStr = "[PLC]";
        sourceColor = "darkCyan";
        break;
    default:
        sourceStr = "[UNK]";
        sourceColor = "gray";
        break;
    }

    QString levelStr = QString::fromUtf8(QMetaEnum::fromType<LogCatcher::Level>().valueToKey(static_cast<LogCatcher::Level>(level))).toUpper();
    if (levelStr.isEmpty()) levelStr = "UNK";

    QString levelColor = "black"; // Цвет по умолчанию
    switch (level) {
    case LogCatcher::Debug:
        levelColor = "blue";
        break;
    case LogCatcher::Warning:
        levelColor = "orange";
        break;
    case LogCatcher::Alarm:
    case LogCatcher::Critical:
        levelColor = "red";
        break;
    default:
        break; // Для Info и остальных останется черный цвет
    }

    // QString wrappedMessage = (wrap == LogCatcher::wrapYes)?
    //         QString("<pre style='margin: 0; font-family: monospace;'>%1</pre>")
    //                              .arg(message.toHtmlEscaped()):message.toHtmlEscaped();

    QString htmlLine = QString("<font color='%7'>%1</font> "
                               "<b><font color='%2'>%3 %8</font> <font color='%4'>%5:</font></b> %6")
                           .arg(timeStr)
                           .arg(sourceColor)
                           .arg(sourceStr)
                           .arg(levelColor)
                           .arg(levelStr)
                           // .arg(message.toHtmlEscaped());
                           .arg((wrap == LogCatcher::wrapYes)?
                                    QString("<br><span style='white-space: pre-wrap;'>%1</span>")
                                        .arg(message.toHtmlEscaped()):message.toHtmlEscaped())
                           .arg(timeColor)
                           .arg(ctx.isSlot()?QString("%1 slot %2").arg(ctx.target.name).arg(ctx.slot):ctx.target.name);
    if (isAutoScrollEnabled) {
        logViewer->appendHtml(htmlLine);
    } else {
        int savedScrollPos = logViewer->verticalScrollBar()->value();
        bool wasBlocked = logViewer->verticalScrollBar()->blockSignals(true);
        logViewer->appendHtml(htmlLine);
        logViewer->verticalScrollBar()->setValue(savedScrollPos);
        logViewer->verticalScrollBar()->blockSignals(wasBlocked);
    }
}

void LogDockWidget::updateButtonPosition()
{
    if (!stopButton->isVisible()) return;

    int padding = 10; // Отступ от краев
    int btnWidth = stopButton->width();

    int scrollBarWidth = 0;
    if (logViewer->verticalScrollBar()->isVisible()) {
        scrollBarWidth = logViewer->verticalScrollBar()->width();
    }

    // Вычисляем x так, чтобы кнопка была у правого края с учетом отступа
    int x = logViewer->width() - btnWidth - padding - scrollBarWidth;
    int y = padding;

    stopButton->move(x, y);
}

void LogDockWidget::showContextMenu(const QPoint &pos)
{
    QMenu *menu = logViewer->createStandardContextMenu(pos);
    menu->addSeparator();

    menu->addAction("Очистить лог", this, &LogDockWidget::clearLog);
    // clearAction->setIcon(QIcon::fromTheme("edit-clear"));

    menu->addAction(tr("Сохранить в файл ..."), this, &LogDockWidget::saveLogToFile);
    // saveAction->setIcon(QIcon::fromTheme("document-save"));

    menu->addSeparator();

    QAction *scrollAction = menu->addAction("Автоматическая прокрутка");
    scrollAction->setCheckable(true);
    scrollAction->setChecked(isAutoScrollEnabled);
    connect(scrollAction, &QAction::toggled, this, &LogDockWidget::toggleAutoScroll);

    menu->exec(logViewer->mapToGlobal(pos));
    delete menu;
}

void LogDockWidget::clearLog()
{
    logViewer->clear();
}

void LogDockWidget::saveLogToFile()
{
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Сохранить лог"), "", tr("Log Files (*.log);;Text Files (*.txt);;All Files (*)"));

    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);
    // Извлекаем чистый текст без HTML-тегов для удобного чтения лога в блокноте
    out << logViewer->toPlainText();
    file.close();
}

void LogDockWidget::toggleAutoScroll(bool checked)
{
    isAutoScrollEnabled = checked;
    if (isAutoScrollEnabled) {
        logViewer->moveCursor(QTextCursor::End);
        logViewer->verticalScrollBar()->setValue(logViewer->verticalScrollBar()->maximum());
    }
}
