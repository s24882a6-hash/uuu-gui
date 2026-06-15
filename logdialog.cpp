#include "logdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QFontDatabase>

LogDialog::LogDialog(const QString& deviceName, QWidget* parent)
    : QDialog(parent)
    , m_title(deviceName)
{
    setWindowTitle(QString("Log — %1").arg(deviceName));
    setMinimumSize(640, 400);

    auto* layout = new QVBoxLayout(this);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_log->setFont(mono);

    layout->addWidget(m_log);

    auto* btnClose = new QPushButton("Close", this);
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    layout->addLayout(btnLayout);

    connect(btnClose, &QPushButton::clicked, this, &QDialog::hide);
}

void LogDialog::appendLine(const QString& line)
{
    m_log->appendPlainText(line);
    // Auto-scroll to bottom
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void LogDialog::setStatus(const QString& status)
{
    setWindowTitle(QString("Log — %1 [%2]").arg(m_title, status));
}
