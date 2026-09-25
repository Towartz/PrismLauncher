#include "LogModel.h"
#include <algorithm>
#include <utility>

LogModel::LogModel(QObject* parent) : QAbstractListModel(parent)
{
    m_content.resize(m_maxLines);
    m_flushTimer.setTimerType(Qt::CoarseTimer);
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(100);
    connect(&m_flushTimer, &QTimer::timeout, this, &LogModel::flush);
}

int LogModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;

    return m_numLines;
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= m_numLines)
        return QVariant();

    auto row = index.row();
    auto realRow = (row + m_firstLine) % m_maxLines;
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return m_content[realRow].line;
    }
    if (role == LevelRole) {
        return static_cast<int>(m_content[realRow].level.value());
    }

    return QVariant();
}

void LogModel::append(MessageLevel level, QString line)
{
    if (m_suspended) {
        return;
    }
    m_pending.append({ level, std::move(line) });
    if (m_pending.size() >= 512) {
        flush();
    } else if (!m_flushTimer.isActive()) {
        m_flushTimer.start();
    }
}

void LogModel::flush()
{
    if (m_pending.isEmpty()) {
        return;
    }
    if (m_flushTimer.isActive()) {
        m_flushTimer.stop();
    }

    QList<entry> batch;
    batch.swap(m_pending);

    if (m_suspended) {
        return;
    }

    if (m_stopOnOverflow) {
        if (m_numLines >= m_maxLines) {
            return;
        }
        int available = m_maxLines - m_numLines;
        int countToInsert = std::min<int>(static_cast<int>(batch.size()), available);
        if (m_numLines + countToInsert == m_maxLines) {
            batch[countToInsert - 1].level = MessageLevel::Warning;
            batch[countToInsert - 1].line = m_overflowMessage;
        }
        beginInsertRows(QModelIndex(), m_numLines, m_numLines + countToInsert - 1);
        for (int i = 0; i < countToInsert; ++i) {
            int lineNum = (m_firstLine + m_numLines) % m_maxLines;
            m_content[lineNum] = std::move(batch[i]);
            m_numLines++;
        }
        endInsertRows();
        return;
    }

    const int totalNew = static_cast<int>(batch.size());
    if (totalNew >= m_maxLines) {
        beginResetModel();
        int offset = totalNew - m_maxLines;
        for (int i = 0; i < m_maxLines; ++i) {
            m_content[i] = std::move(batch[offset + i]);
        }
        m_firstLine = 0;
        m_numLines = m_maxLines;
        endResetModel();
        return;
    }

    if (m_numLines + totalNew > m_maxLines) {
        int overflow = (m_numLines + totalNew) - m_maxLines;
        beginRemoveRows(QModelIndex(), 0, overflow - 1);
        m_firstLine = (m_firstLine + overflow) % m_maxLines;
        m_numLines -= overflow;
        endRemoveRows();
    }

    int firstRow = m_numLines;
    int lastRow = m_numLines + totalNew - 1;
    beginInsertRows(QModelIndex(), firstRow, lastRow);
    for (int i = 0; i < totalNew; ++i) {
        int lineNum = (m_firstLine + m_numLines) % m_maxLines;
        m_content[lineNum] = std::move(batch[i]);
        m_numLines++;
    }
    endInsertRows();
}

void LogModel::suspend(bool suspend)
{
    if (suspend && !m_suspended) {
        flush();
    }
    m_suspended = suspend;
}

bool LogModel::suspended()
{
    return m_suspended;
}

void LogModel::clear()
{
    m_flushTimer.stop();
    m_pending.clear();
    beginResetModel();
    m_firstLine = 0;
    m_numLines = 0;
    endResetModel();
}

QString LogModel::toPlainText()
{
    flush();
    QString out;
    out.reserve(m_numLines * 80);
    for (int i = 0; i < m_numLines; i++) {
        QString& line = m_content[(m_firstLine + i) % m_maxLines].line;
        out.append(line + '\n');
    }
    out.squeeze();
    return out;
}

void LogModel::setMaxLines(int maxLines)
{
    flush();
    // no-op
    if (maxLines == m_maxLines) {
        return;
    }
    // if it all still fits in the buffer, just resize it
    if (m_firstLine + m_numLines < m_maxLines) {
        m_maxLines = maxLines;
        m_content.resize(maxLines);
        return;
    }
    // otherwise, we need to reorganize the data because it crosses the wrap boundary
    QList<entry> newContent;
    newContent.resize(maxLines);
    if (m_numLines <= maxLines) {
        // if it all fits in the new buffer, just copy it over
        for (int i = 0; i < m_numLines; i++) {
            newContent[i] = m_content[(m_firstLine + i) % m_maxLines];
        }
        m_content.swap(newContent);
    } else {
        // if it doesn't fit, part of the data needs to be thrown away (the oldest log messages)
        int lead = m_numLines - maxLines;
        beginRemoveRows(QModelIndex(), 0, lead - 1);
        for (int i = 0; i < maxLines; i++) {
            newContent[i] = m_content[(m_firstLine + lead + i) % m_maxLines];
        }
        m_numLines = m_maxLines;
        m_content.swap(newContent);
        endRemoveRows();
    }
    m_firstLine = 0;
    m_maxLines = maxLines;
}

int LogModel::getMaxLines()
{
    return m_maxLines;
}

void LogModel::setStopOnOverflow(bool stop)
{
    m_stopOnOverflow = stop;
}

void LogModel::setOverflowMessage(const QString& overflowMessage)
{
    m_overflowMessage = overflowMessage;
}

void LogModel::setLineWrap(bool state)
{
    if (m_lineWrap != state) {
        m_lineWrap = state;
    }
}

bool LogModel::wrapLines() const
{
    return m_lineWrap;
}

void LogModel::setColorLines(bool state)
{
    if (m_colorLines != state) {
        m_colorLines = state;
    }
}

bool LogModel::colorLines() const
{
    return m_colorLines;
}

bool LogModel::isOverFlow()
{
    flush();
    return m_numLines >= m_maxLines && m_stopOnOverflow;
}

MessageLevel LogModel::previousLevel()
{
    if (!m_pending.isEmpty()) {
        return m_pending.last().level;
    }
    if (m_numLines > 0) {
        return m_content[(m_firstLine + m_numLines - 1) % m_maxLines].level;
    }
    return MessageLevel::Unknown;
}
