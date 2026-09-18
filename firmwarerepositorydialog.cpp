#include "firmwarerepositorydialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

FirmwareRepositoryDialog::FirmwareRepositoryDialog(const QString &initialPath,
                                                     bool allowEmpty,
                                                     QWidget *parent)
    : QDialog(parent), m_allowEmpty(allowEmpty)
{
    setWindowTitle(QStringLiteral("Репозиторий прошивок LogicBox"));
    setModal(true);
    setMinimumWidth(650);

    auto *layout = new QVBoxLayout(this);

    auto *description = new QLabel(
        QStringLiteral("Укажите корневой каталог репозитория LogicBox. "
                       "Внутри него должна находиться папка firmware. "
                       "Путь можно ввести вручную или выбрать через кнопку «Обзор…»."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *pathLayout = new QHBoxLayout;
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setText(QDir::toNativeSeparators(initialPath));
    m_pathEdit->setPlaceholderText(QStringLiteral("Например: C:\\git\\logicbox"));
    m_pathEdit->setClearButtonEnabled(true);
    pathLayout->addWidget(m_pathEdit, 1);

    auto *browseButton = new QPushButton(QStringLiteral("Обзор…"), this);
    connect(browseButton, &QPushButton::clicked,
            this, &FirmwareRepositoryDialog::browse);
    pathLayout->addWidget(browseButton);
    layout->addLayout(pathLayout);

    if (m_allowEmpty) {
        auto *hint = new QLabel(
            QStringLiteral("Чтобы сбросить сохранённый путь, очистите поле и нажмите «Сохранить»."),
            this);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         Qt::Horizontal,
                                         this);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    connect(buttons, &QDialogButtonBox::accepted,
            this, &FirmwareRepositoryDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    layout->addWidget(buttons);

    m_pathEdit->setFocus();
    m_pathEdit->selectAll();
}

QString FirmwareRepositoryDialog::repositoryPath() const
{
    const QString text = m_pathEdit ? m_pathEdit->text().trimmed() : QString();
    if (text.isEmpty())
        return {};

    return QDir::cleanPath(QDir::fromNativeSeparators(text));
}

void FirmwareRepositoryDialog::browse()
{
    QString initialPath = repositoryPath();
    if (initialPath.isEmpty() || !QFileInfo(initialPath).isDir())
        initialPath = QDir::homePath();

    const QString selected = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Выберите каталог репозитория LogicBox"),
        initialPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!selected.isEmpty())
        m_pathEdit->setText(QDir::toNativeSeparators(QDir::cleanPath(selected)));
}

void FirmwareRepositoryDialog::validateAndAccept()
{
    const QString selected = repositoryPath();

    if (selected.isEmpty()) {
        if (m_allowEmpty) {
            QDialog::accept();
            return;
        }

        QMessageBox::warning(this,
                             QStringLiteral("Репозиторий прошивок"),
                             QStringLiteral("Укажите путь к репозиторию LogicBox."));
        return;
    }

    const QFileInfo repositoryInfo(selected);
    if (!repositoryInfo.exists() || !repositoryInfo.isDir()) {
        QMessageBox::warning(this,
                             QStringLiteral("Репозиторий прошивок"),
                             QStringLiteral("Каталог не найден:\n%1")
                                 .arg(QDir::toNativeSeparators(selected)));
        return;
    }

    const QString firmwarePath = QDir(repositoryInfo.absoluteFilePath())
                                     .filePath(QStringLiteral("firmware"));
    const QFileInfo firmwareInfo(firmwarePath);
    if (!firmwareInfo.exists() || !firmwareInfo.isDir()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Репозиторий прошивок"),
            QStringLiteral("В выбранном каталоге не найдена папка firmware:\n%1")
                .arg(QDir::toNativeSeparators(firmwarePath)));
        return;
    }

    QDialog::accept();
}
