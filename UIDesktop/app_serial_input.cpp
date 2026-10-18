#include "app_serial_input.h"

app_serial_input_t::app_serial_input_t(QObject *parent) : QSerialPort(parent)
{
    setReadBufferSize(64*1024);
}
bool app_serial_input_t::app_serial_open(const QString &port,int baud)
{
    close(); m_lines.app_line_reset(); m_received=0;
    setPortName(port);
    if(!setBaudRate(baud)||!setDataBits(Data8)||!setParity(NoParity)
       ||!setStopBits(OneStop)||!setFlowControl(NoFlowControl)) return false;
    if(!open(QIODevice::ReadWrite)) return false;
    setDataTerminalReady(false); setRequestToSend(false);
    return true;
}
app_line_batch_t app_serial_input_t::app_serial_read()
{
    const QByteArray bytes=read(8192);
    m_received+=quint64(bytes.size());
    return m_lines.app_line_feed_bytes(bytes);
}
app_line_batch_t app_serial_input_t::app_serial_finish()
{
    // Preserve an unfinished final line as raw evidence; never carry it across
    // reopen or join it to a later session's first command/data frame.
    app_line_batch_t result;
    const QByteArray pending=m_lines.app_line_take_pending();
    if(!pending.isEmpty()) { app_line_record_t record; record.bytes=pending; result.records.append(record); }
    m_lines.app_line_reset();
    return result;
}
