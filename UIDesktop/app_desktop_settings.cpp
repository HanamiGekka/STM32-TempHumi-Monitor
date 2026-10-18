#include "app_desktop_settings.h"
#include <QCoreApplication>
#include <QSerialPortInfo>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMap>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <setupapi.h>
#endif

static void app_read_windows_attachment(QVector<app_serial_device_t> &devices)
{
#ifdef Q_OS_WIN
    // QSerialPortInfo exposes COM location but not PnP/tree attachment paths.
    // Resolve read-only OS APIs at runtime; no new deployment library, driver,
    // registry mutation, elevation or device restart is involved.
    QLibrary setup(QStringLiteral("setupapi")),registry(QStringLiteral("advapi32"));
    const auto getClass=reinterpret_cast<decltype(&SetupDiGetClassDevsW)>(setup.resolve("SetupDiGetClassDevsW"));
    const auto enumerate=reinterpret_cast<decltype(&SetupDiEnumDeviceInfo)>(setup.resolve("SetupDiEnumDeviceInfo"));
    const auto instance=reinterpret_cast<decltype(&SetupDiGetDeviceInstanceIdW)>(setup.resolve("SetupDiGetDeviceInstanceIdW"));
    const auto property=reinterpret_cast<decltype(&SetupDiGetDeviceRegistryPropertyW)>(setup.resolve("SetupDiGetDeviceRegistryPropertyW"));
    const auto destroy=reinterpret_cast<decltype(&SetupDiDestroyDeviceInfoList)>(setup.resolve("SetupDiDestroyDeviceInfoList"));
    const auto openKey=reinterpret_cast<decltype(&RegOpenKeyExW)>(registry.resolve("RegOpenKeyExW"));
    const auto query=reinterpret_cast<decltype(&RegQueryValueExW)>(registry.resolve("RegQueryValueExW"));
    const auto closeKey=reinterpret_cast<decltype(&RegCloseKey)>(registry.resolve("RegCloseKey"));
    if(!getClass ||!enumerate ||!instance ||!property ||!destroy ||!openKey ||!query ||!closeKey) return;
    const GUID ports={0x4d36e978,0xe325,0x11ce,{0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
    const auto set=getClass(&ports,nullptr,nullptr,DIGCF_PRESENT);
    if(set==INVALID_HANDLE_VALUE) return;
    for(DWORD n=0;;++n) {
        SP_DEVINFO_DATA info={};info.cbSize=sizeof(info);
        if(!enumerate(set,n,&info)) break;
        wchar_t id[4096]={};
        if(!instance(set,&info,id,4096,nullptr)) continue;
        const auto identifier=QString::fromWCharArray(id);
        if(!identifier.startsWith("USB\\",Qt::CaseInsensitive)) continue;
        const auto keyPath=QStringLiteral("SYSTEM\\CurrentControlSet\\Enum\\")+identifier+"\\Device Parameters";
        HKEY key=nullptr;
        if(openKey(HKEY_LOCAL_MACHINE,reinterpret_cast<const wchar_t*>(keyPath.utf16()),0,KEY_READ,&key)!=ERROR_SUCCESS) continue;
        wchar_t port[256]={};DWORD bytes=sizeof(port),type=0;
        const auto result=query(key,L"PortName",nullptr,&type,reinterpret_cast<LPBYTE>(port),&bytes);closeKey(key);
        if(result!=ERROR_SUCCESS ||type!=REG_SZ ||bytes>=sizeof(port)) continue;
        DWORD required=0;
        property(set,&info,SPDRP_LOCATION_PATHS,&type,nullptr,0,&required);
        if(required==0 ||required>65536) continue;
        QVector<wchar_t> buffer(int(required/sizeof(wchar_t))+2,0);
        if(!property(set,&info,SPDRP_LOCATION_PATHS,&type,reinterpret_cast<PBYTE>(buffer.data()),required,nullptr) ||type!=REG_MULTI_SZ) continue;
        QStringList locations;
        for(int pos=0;pos<buffer.size()-1 &&buffer[pos];) {
            int end=pos;while(end<buffer.size() &&buffer[end]) ++end;
            locations.append(QString::fromWCharArray(buffer.data()+pos,end-pos));pos=end+1;
        }
        locations.sort(Qt::CaseInsensitive);
        for(auto &d:devices) if(d.port.compare(QString::fromWCharArray(port),Qt::CaseInsensitive)==0) {
            d.pnp_instance_id=identifier;d.usb_location_paths=locations;
        }
    }
    destroy(set);
#else
    Q_UNUSED(devices);
#endif
}

QString app_desktop_identity_kind(const app_serial_device_t &d)
{
    if(!d.has_vendor ||!d.has_product) return "UNVERIFIED";
    if(!d.serial_number.trimmed().isEmpty()) return "USB_SERIAL";
    if(d.pnp_instance_id.startsWith("USB\\",Qt::CaseInsensitive) &&!d.usb_location_paths.isEmpty()) return "WINDOWS_USB_ATTACHMENT";
    return "UNVERIFIED";
}
bool app_desktop_identity_matches(const app_serial_device_t &expected,const app_serial_device_t &actual)
{
    if(!expected.has_vendor ||!actual.has_vendor ||!expected.has_product ||!actual.has_product
       ||expected.vendor!=actual.vendor ||expected.product!=actual.product) return false;
    const auto kind=app_desktop_identity_kind(expected);
    if(kind=="USB_SERIAL") return !actual.serial_number.isEmpty() &&expected.serial_number==actual.serial_number;
    if(kind!="WINDOWS_USB_ATTACHMENT" ||!actual.serial_number.isEmpty()) return false;
    if(expected.pnp_instance_id.compare(actual.pnp_instance_id,Qt::CaseInsensitive)!=0
       ||expected.usb_location_paths.size()!=actual.usb_location_paths.size()) return false;
    for(int n=0;n<expected.usb_location_paths.size();++n)
        if(expected.usb_location_paths[n].compare(actual.usb_location_paths[n],Qt::CaseInsensitive)!=0) return false;
    return true;
}

std::unique_ptr<QSettings> app_desktop_open_settings()
{
    // An explicit INI is useful for isolated tests/portable preference profiles.
    // Ordinary GUI uses native per-user storage, never the installation folder.
    const QString path=QCoreApplication::instance()->property("appSettingsFile").toString();
    if(!path.isEmpty()) return std::unique_ptr<QSettings>(new QSettings(path,QSettings::IniFormat));
    return std::unique_ptr<QSettings>(new QSettings(QSettings::NativeFormat,QSettings::UserScope,
        "TempHumi", "TempHumi Monitor"));
}
QVector<app_serial_device_t> app_desktop_serial_devices()
{
    QVector<app_serial_device_t> devices;
    for(const auto &port:QSerialPortInfo::availablePorts()) {
        app_serial_device_t d; d.port=port.portName(); d.description=port.description();
        d.serial_number=port.serialNumber(); d.system_location=port.systemLocation();d.manufacturer=port.manufacturer();
        d.vendor=port.vendorIdentifier(); d.product=port.productIdentifier();
        d.has_vendor=port.hasVendorIdentifier(); d.has_product=port.hasProductIdentifier();
        devices.append(d);
    }
    app_read_windows_attachment(devices);
    return devices;
}
app_serial_choice_t app_desktop_choose_serial(const QVector<app_serial_device_t> &devices,
                                             const QString &last_port,const QString &last_serial)
{
    // Prefer a stable USB serial identity over an OS-assigned COM number.
    if(!last_serial.isEmpty()) {
        QVector<QString> matches;
        for(const auto &d:devices) if(d.serial_number==last_serial) matches.append(d.port);
        if(matches.size()==1) return {matches[0],"Restored device identity",false};
    }
    for(const auto &d:devices) if(!last_port.isEmpty() && d.port==last_port)
        return {d.port,"Restored last selected device",false};
    QVector<QString> ch340;
    for(const auto &d:devices)
        if((d.has_vendor && d.has_product && d.vendor==0x1A86 && d.product==0x7523)
           || ((!d.has_vendor ||!d.has_product) &&d.description.contains("CH340",Qt::CaseInsensitive))) ch340.append(d.port);
    if(ch340.size()==1) return {ch340[0],"CH340 detected (1A86:7523)",false};
    if(ch340.size()>1) return {QString(),"Multiple CH340 devices: choose a port",true};
    return {QString(),"No preferred device connected: choose an available port",true};
}
QString app_desktop_recording_root()
{
    const auto settings=app_desktop_open_settings();
    const QString saved=settings->value("recordingRoot").toString();
    if(!saved.isEmpty()) return saved; // Invalid saved paths must report errors, not fall back.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("recordings");
}
QString app_desktop_export_directory()
{
    const auto settings=app_desktop_open_settings();
    const QString saved=settings->value("lastExportDirectory").toString();
    return saved.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation):saved;
}
QString app_desktop_png_directory()
{
    const auto settings=app_desktop_open_settings();
    const QString saved=settings->value("lastPngDirectory").toString();
    return saved.isEmpty()?app_desktop_export_directory():saved;
}
