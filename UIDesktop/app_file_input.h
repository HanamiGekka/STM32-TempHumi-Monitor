#ifndef APP_FILE_INPUT_H
#define APP_FILE_INPUT_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>
#include "app_line_buffer.h"

typedef app_line_record_t app_file_record_t;

struct app_file_poll_t
{
    // records is the canonical ordered raw stream; convenience lists preserve classification.
    QList<app_file_record_t> records;
    QList<QByteArray> lines;
    QList<QByteArray> overflow_fragments;
    bool available = false;
    bool reset = false;
    QString reset_reason;
    QString error_text;
    qint64 offset = 0;
    int pending_bytes = 0;
};

class app_file_input_t
{
public:
    void app_file_set_path(const QString &path);
    QString app_file_path() const;
    app_file_poll_t app_file_poll();
    qint64 app_file_offset() const;
    int app_file_pending_bytes() const;
    QList<app_line_record_t> app_file_finish();
private:
    void app_file_accept_bytes(const QByteArray &bytes, app_file_poll_t *result);
    QString m_path;
    QString m_identity;
    QByteArray m_prefix_anchor;
    QByteArray m_tail_anchor;
    app_line_buffer_t m_line_buffer;
    qint64 m_offset = 0;
    bool m_ever_opened = false;
    bool m_unavailable = false;
};

#endif
