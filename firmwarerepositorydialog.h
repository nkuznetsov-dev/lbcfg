#ifndef FIRMWAREREPOSITORYDIALOG_H
#define FIRMWAREREPOSITORYDIALOG_H

#include <QDialog>

class QLineEdit;

class FirmwareRepositoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FirmwareRepositoryDialog(const QString &initialPath,
                                      bool allowEmpty,
                                      QWidget *parent = nullptr);

    QString repositoryPath() const;

private slots:
    void browse();
    void validateAndAccept();

private:
    QLineEdit *m_pathEdit = nullptr;
    bool m_allowEmpty = false;
};

#endif // FIRMWAREREPOSITORYDIALOG_H
