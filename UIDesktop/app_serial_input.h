#ifndef APP_SERIAL_INPUT_H
#define APP_SERIAL_INPUT_H
#include <QSerialPort>
#include "app_line_buffer.h"

// Same framing/parser as files; a 64 KiB OS backlog and 8 KiB read slices
// bound each event-loop turn. Sensor conversion never occurs here.
class app_serial_input_t : public QSerialPort
{
public:
    explicit app_serial_input_t(QObject *parent=nullptr);
    bool app_serial_open(const QString &port,int baud);
    app_line_batch_t app_serial_read();
    app_line_batch_t app_serial_finish();
    quint64 app_serial_received_bytes() const { return m_received; }
    int app_serial_pending_bytes() const { return m_lines.app_line_pending_bytes(); }
private:
    app_line_buffer_t m_lines;
    quint64 m_received=0;
};
#endif
