#ifndef APP_DESKTOP_SETTINGS_H
#define APP_DESKTOP_SETTINGS_H
#include <QSettings>
#include <QVector>
#include <QString>
#include <QStringList>
#include <memory>

struct app_serial_device_t {
    QString port,description,serial_number,system_location;
    quint16 vendor=0,product=0;
    bool has_vendor=false,has_product=false;
    // Windows attachment evidence, not an invented USB/board serial number.
    QString manufacturer,pnp_instance_id;
    QStringList usb_location_paths;
};
struct app_serial_choice_t {
    QString port,reason; bool needs_selection;
    app_serial_choice_t(const QString &p=QString(),const QString &r=QString(),bool choose=false)
        :port(p),reason(r),needs_selection(choose) {}
};
std::unique_ptr<QSettings> app_desktop_open_settings();
QVector<app_serial_device_t> app_desktop_serial_devices();
QString app_desktop_identity_kind(const app_serial_device_t &device);
bool app_desktop_identity_matches(const app_serial_device_t &expected,const app_serial_device_t &actual);
app_serial_choice_t app_desktop_choose_serial(const QVector<app_serial_device_t> &devices,
                                             const QString &last_port,const QString &last_serial);
QString app_desktop_recording_root();
QString app_desktop_export_directory();
QString app_desktop_png_directory();
#endif
