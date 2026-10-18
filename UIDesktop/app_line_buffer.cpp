#include "app_line_buffer.h"

QByteArray app_line_buffer_t::app_line_take_pending()
{
    QByteArray bytes=m_pending;
    m_pending.clear();
    return bytes;
}

static void app_line_append_record(const QByteArray &bytes, bool overflow, app_line_batch_t *batch)
{
    app_line_record_t record;
    record.bytes = bytes;
    record.overflow = overflow;
    batch->records.append(record);
    if (overflow) {
        batch->overflow_fragments.append(bytes);
        batch->error_text = QStringLiteral("LINE_TOO_LONG");
    } else {
        batch->lines.append(bytes);
    }
}

void app_line_buffer_t::app_line_reset()
{
    m_pending.clear();
    m_overflow = false;
}

int app_line_buffer_t::app_line_pending_bytes() const { return m_pending.size(); }

app_line_batch_t app_line_buffer_t::app_line_feed_bytes(const QByteArray &bytes)
{
    app_line_batch_t batch;
    int position = 0;
    while (position < bytes.size()) {
        const int newline = bytes.indexOf('\n', position);
        const int end = newline >= 0 ? newline + 1 : bytes.size();
        // Find the next LF once, including when one long line spans many output
        // fragments. Re-scanning its remaining suffix per fragment is quadratic.
        while (position < end) {
            const int span = end - position;
            if (m_overflow) {
                // A caller may pass readAll() of a very large backlog. Split its raw
                // fragments too; no individual output record exceeds the memory bound.
                const int length = qMin<int>(span, APP_LINE_MAX_RECORD_BYTES);
                app_line_append_record(bytes.mid(position, length), true, &batch);
                position += length;
                if (position == end && newline >= 0) m_overflow = false;
                continue;
            }
            const int capacity = APP_LINE_MAX_RECORD_BYTES - m_pending.size();
            if (span > capacity) {
                // The line is now known to exceed the limit. Preserve its first bounded
                // fragment and all following bytes; never silently discard or parse them.
                m_pending.append(bytes.constData() + position, capacity);
                app_line_append_record(m_pending, true, &batch);
                m_pending.clear();
                m_overflow = true;
                position += capacity;
                continue;
            }
            m_pending.append(bytes.constData() + position, span);
            position = end;
            if (newline >= 0) {
                app_line_append_record(m_pending, false, &batch);
                m_pending.clear();
            }
        }
    }
    batch.pending_bytes = m_pending.size();
    return batch;
}
