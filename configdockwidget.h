#ifndef CONFIGDOCKWIDGET_H
#define CONFIGDOCKWIDGET_H

#include <QDockWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QStandardItemModel>
#include <QListWidget>
#include <QStackedWidget>
#include <QTableView>
#include "plcmanager.h"
#include "yamltextview.h"
#include "varview.h"
#include "deviceview.h"

class MainWindow;
class ConfigDockWidget : public QDockWidget
{
    Q_OBJECT
public:
    explicit ConfigDockWidget(const QString& name,
                              MainWindow *parent = nullptr);

    // QString config() const;
    bool isModified() const;
    void setConfig(const QString &yaml);
    bool openFile(const QString &filePath);
    bool saveFile();
    bool saveFileAs();

    QString getCurrentFilePath() const;
    QString getPlcName() const;
    void setBoundTarget(const LogicBoxTarget &target);

    void onConfigureClicked();
    QList<QAction*> activeTextActions() const;

    QAction *getConfigureAction() const;

signals:
    // void getSaveFile();

private slots:
    void onTextChanged();
    void scrollToSelectedPlc(int index);


protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    plcManager *lbplc = nullptr;
    MainWindow *p_mainWindow = nullptr;
    lbyaml *yamlParser = nullptr;
    // Добавить в private секцию в configdockwidget.h
    QListWidget *sidebarMenu = nullptr;
    QStackedWidget *stackedContainer = nullptr;
    // Страницы-контейнеры
    yamlTextView *yamlPage = nullptr;
    varView *varPage = nullptr;
    deviceView *devicePage = nullptr;

    // QTextEdit* editor = nullptr;
    // Новые таблицы для представлений
    QTableView *varTableView = nullptr;
    QTableView *deviceTableView = nullptr;

    QComboBox *plcSelector = nullptr;
    QPushButton *configButton = nullptr;
    QString originalYaml;
    bool modified = false;
    QString plcName;
    LogicBoxTarget boundTarget;
    bool writeFile(const QString &filePath);

    QString currentFilePath;
    void updateTitle();
    QStandardItemModel* createPlcModel(const QString &yamlText);

    // Слот для обработки переключения страниц
    void onSidebarRowChanged(int index);
    void onAddVariableToWatch(const QString &varName);
    bool resolveTargetForSelection(const QString &name, const QString &mac,
                                   LogicBoxTarget *target);

    bool replacePlcBlockInYaml(const QString& newPlcBlockText);
    int isModifiedPages(bool allowCancel = false);

    QAction *confAction = nullptr;
};

#endif // CONFIGDOCKWIDGET_H
