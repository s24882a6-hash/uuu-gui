#include "logdialog.h"
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QFontDatabase>

LogDialog::LogDialog(const QString& deviceName, QWidget* parent)
    : QDialog(parent)
    , m_title(deviceName)
{
    setWindowTitle(tr("Log — %1").arg(deviceName));
    setMinimumSize(640, 400);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_log->setFont(mono);

    layout->addWidget(m_log);
}

void LogDialog::appendLine(const QString& line)
{
    m_log->appendPlainText(line);
    // Auto-scroll to bottom
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void LogDialog::setStatus(const QString& status)
{
    setWindowTitle(tr("Log — %1 [%2]").arg(m_title, status));
}
