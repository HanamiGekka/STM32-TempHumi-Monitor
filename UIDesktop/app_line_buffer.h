#ifndef APP_LINE_BUFFER_H
#define APP_LINE_BUFFER_H

#include <QByteArray>
#include <QList>
#include <QString>

// Resource bound, not a sensor/protocol limit: a normal TH3 line is well below this.
enum { APP_LINE_MAX_RECORD_BYTES = 64 * 1024 };

struct app_line_record_t
{
    QByteArray bytes;
    bool overflow = false;
};

struct app_line_batch_t
{
    // records is the ordered raw stream, including explicit malformed overflow fragments.
    QList<app_line_record_t> records;
    QList<QByteArray> lines;
    QList<QByteArray> overflow_fragments;
    QString error_text;
    int pending_bytes = 0;
};

class app_line_buffer_t
{
public:
    app_line_batch_t app_line_feed_bytes(const QByteArray &bytes);
    void app_line_reset();
    int app_line_pending_bytes() const;
    QByteArray app_line_take_pending();
private:
    QByteArray m_pending;
    bool m_overflow = false;
};

#endif
