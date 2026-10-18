#include "app_file_input.h"

QList<app_line_record_t> app_file_input_t::app_file_finish()
{
    const QByteArray pending=m_line_buffer.app_line_take_pending();
    m_line_buffer.app_line_reset();
    QList<app_line_record_t> records;
    if(!pending.isEmpty()) { app_line_record_t record; record.bytes=pending; records.append(record); }
    return records;
}

#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <cerrno>
#endif

namespace {
// Bounded polling prevents a growing file from starving the Qt event loop.
const qint64 APP_FILE_MAX_READ_PER_POLL = 256 * 1024;
const int APP_FILE_READ_BLOCK = 16 * 1024;
const int APP_FILE_ANCHOR_BYTES = 128;

static bool app_file_path_missing(const QString &path)
{
#ifdef Q_OS_WIN
    if (GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16())) != INVALID_FILE_ATTRIBUTES) return false;
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
#else
    struct stat info;
    if (::stat(QFile::encodeName(path).constData(), &info) == 0) return false;
    return errno == ENOENT || errno == ENOTDIR;
#endif
}

static QString app_file_read_identity(QFile &file)
{
#ifdef Q_OS_WIN
    const intptr_t native_handle = _get_osfhandle(file.handle());
    if (native_handle == -1) return QString();
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(native_handle), &info)) return QString();
    // Volume + 64-bit file index identifies replacement even if size/mtime are identical.
    return QStringLiteral("%1:%2:%3").arg(info.dwVolumeSerialNumber).arg(info.nFileIndexHigh).arg(info.nFileIndexLow);
#else
    struct stat info;
    if (::fstat(file.handle(), &info) != 0) return QString();
    return QStringLiteral("%1:%2").arg(static_cast<qulonglong>(info.st_dev)).arg(static_cast<qulonglong>(info.st_ino));
#endif
}

static QByteArray app_file_read_anchor(QFile &file, qint64 position, int length)
{
    if (length <= 0) return QByteArray();
    if (!file.seek(position)) return QByteArray();
    return file.read(length);
}

}

void app_file_input_t::app_file_set_path(const QString &path)
{
    m_path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    m_identity.clear();
    m_prefix_anchor.clear();
    m_tail_anchor.clear();
    m_line_buffer.app_line_reset();
    m_offset = 0;
    m_ever_opened = false;
    m_unavailable = false;
}

QString app_file_input_t::app_file_path() const { return m_path; }
qint64 app_file_input_t::app_file_offset() const { return m_offset; }
int app_file_input_t::app_file_pending_bytes() const { return m_line_buffer.app_line_pending_bytes(); }

void app_file_input_t::app_file_accept_bytes(const QByteArray &bytes, app_file_poll_t *result)
{
    const app_line_batch_t batch = m_line_buffer.app_line_feed_bytes(bytes);
    result->records.append(batch.records);
    result->lines.append(batch.lines);
    result->overflow_fragments.append(batch.overflow_fragments);
    if (!batch.error_text.isEmpty()) result->error_text = batch.error_text;
}

app_file_poll_t app_file_input_t::app_file_poll()
{
    app_file_poll_t result;
    result.offset = m_offset;
    result.pending_bytes = m_line_buffer.app_line_pending_bytes();
    QFile file(m_path);
    if (m_path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        // Sharing/permission failures do not imply a new input segment. Keep the consumed
        // position and pending half-line so recovery cannot replay previously recorded rows.
        if (app_file_path_missing(m_path)) m_unavailable = true;
        else result.error_text = file.errorString();
        return result;
    }
    const QString identity = app_file_read_identity(file);
    if (identity.isEmpty()) {
        result.error_text = QStringLiteral("FILE_ID_UNAVAILABLE");
        return result;
    }
    const qint64 size = file.size();
    result.available = true;
    if (!m_ever_opened) result.reset_reason = QStringLiteral("OPEN");
    else if (m_unavailable) result.reset_reason = QStringLiteral("REAPPEARED");
    else if (identity != m_identity) result.reset_reason = QStringLiteral("REPLACED");
    else if (size < m_offset) result.reset_reason = QStringLiteral("TRUNCATED");
    else {
        // A writer may truncate and refill between polls. Previously consumed anchors detect it
        // even when the current size has already caught up with the former read position.
        const QByteArray prefix = app_file_read_anchor(file, 0, m_prefix_anchor.size());
        const QByteArray tail = app_file_read_anchor(file, m_offset - m_tail_anchor.size(), m_tail_anchor.size());
        if (prefix != m_prefix_anchor || tail != m_tail_anchor) result.reset_reason = QStringLiteral("REWRITTEN");
    }
    if (!result.reset_reason.isEmpty()) {
        result.reset = true;
        m_offset = 0;
        m_line_buffer.app_line_reset();
        m_prefix_anchor.clear();
        m_tail_anchor.clear();
    }
    m_ever_opened = true;
    m_unavailable = false;
    m_identity = identity;
    qint64 remaining = qMin(APP_FILE_MAX_READ_PER_POLL, qMax<qint64>(0, size - m_offset));
    if (!file.seek(m_offset)) {
        result.error_text = file.errorString();
    } else {
        while (remaining > 0) {
            const QByteArray block = file.read(qMin<qint64>(APP_FILE_READ_BLOCK, remaining));
            if (block.isEmpty()) {
                if (file.error() != QFile::NoError) result.error_text = file.errorString();
                break;
            }
            m_offset += block.size();
            remaining -= block.size();
            app_file_accept_bytes(block, &result);
        }
    }
    m_prefix_anchor = app_file_read_anchor(file, 0, static_cast<int>(qMin<qint64>(m_offset, APP_FILE_ANCHOR_BYTES)));
    const int tail_length = static_cast<int>(qMin<qint64>(m_offset, APP_FILE_ANCHOR_BYTES));
    m_tail_anchor = app_file_read_anchor(file, m_offset - tail_length, tail_length);
    result.offset = m_offset;
    result.pending_bytes = m_line_buffer.app_line_pending_bytes();
    return result;
}
