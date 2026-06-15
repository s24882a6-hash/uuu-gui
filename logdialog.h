#pragma once
#include <QDialog>

class QPlainTextEdit;

class LogDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LogDialog(const QString& deviceName, QWidget* parent = nullptr);

public slots:
    void appendLine(const QString& line);
    void setStatus(const QString& status);

private:
    QPlainTextEdit* m_log   = nullptr;
    QString         m_title;
};
