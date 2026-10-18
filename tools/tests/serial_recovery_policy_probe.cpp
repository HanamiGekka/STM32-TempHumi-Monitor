#include "app_serial_recovery.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <cstdio>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);int checks=0;QJsonArray failures;
    const auto check=[&](bool ok,const QString &name) { ++checks;if(!ok) failures.append(name); };
    app_serial_device_t d;d.port="COM13";d.description="CH340 test fixture";
    d.vendor=0x1a86;d.product=0x7523;d.has_vendor=d.has_product=true;
    d.pnp_instance_id="USB\\VID_1A86&PID_7523\\TEST_ATTACHMENT";d.usb_location_paths=QStringList{"PCIROOT(TEST)#USB(TEST1)"};
    check(app_desktop_identity_kind(d)=="WINDOWS_USB_ATTACHMENT","real Windows attachment fields, not invented serial");
    auto renamed=d;renamed.port="COM21";
    check(app_desktop_identity_matches(d,renamed),"COM renumber allowed for identical attachment");
    auto moved=d;moved.usb_location_paths=QStringList{"PCIROOT(TEST)#USB(TEST2)"};
    check(!app_desktop_identity_matches(d,moved),"same COM/VID/PID cannot replace attachment evidence");
    auto missing=d;missing.usb_location_paths.clear();
    check(app_desktop_identity_kind(missing)=="UNVERIFIED","missing physical location fails closed");
    auto serial=d;serial.serial_number="TEST_SERIAL_ONLY";serial.pnp_instance_id.clear();serial.usb_location_paths.clear();
    check(app_desktop_identity_kind(serial)=="USB_SERIAL","actual supplied serial remains independent identity route");
    check(!app_mcu_identity_hash("000000000000000000000000","64","1040").size(),"blank MCU UID rejected");
    check(app_mcu_identity_hash("ffffffffffffffffffffffff","64","1040").isEmpty(),"erased MCU UID rejected");
    check(app_mcu_identity_hash("012345670123456701234567","64","1040").isEmpty(),"repeated UID words rejected");
    check(app_mcu_identity_hash("0123456789abcdef10325476","64","1040").size()==64,"valid memory-order UID hashed");
    check(app_mcu_identity_hash("0123456789abcdef10325476","64","1234").isEmpty(),"unexpected MCU family rejected");
    check(app_mcu_identity_hash("0123456789abcdef10325476","0","1040").isEmpty(),"invalid factory capacity rejected");
    check(app_mcu_identity_hash("0123456789abcdef10325476","64","0")==app_mcu_identity_hash("0123456789abcdef10325476","64","1040"),"ES096 non-debug code zero preserves factory UID identity");
    check(app_mcu_identity_hash("0123456789abcdef10325476","128","0").size()==64,"supported capacity with unavailable debug code");
    check(app_mcu_identity_hash("000000000000000000000000","64","0").isEmpty(),"unavailable debug code never accepts blank UID");
    check(app_mcu_identity_hash("ffffffffffffffffffffffff","64","0").isEmpty(),"unavailable debug code never accepts erased UID");
    check(app_mcu_identity_hash("012345670123456701234567","64","0").isEmpty(),"unavailable debug code never accepts repeated UID");
    check(app_mcu_identity_hash("0123456789abcdef10325476","0","0").isEmpty(),"unavailable debug code never accepts invalid capacity");
    check(app_mcu_identity_hash("0123456789abcdef10325476","64","").isEmpty(),"missing diagnostic field remains malformed");
    app_serial_recovery_t p;p.start(d,0);p.error(QSerialPort::ResourceError,0);
    check(p.state()=="WAITING_FOR_USER" &&!p.due(999999),"automatic recovery defaults off");
    p.configure(true);p.start(missing,0);p.error(QSerialPort::ResourceError,0);
    check(!p.automatic_identity() &&!p.due(10000),"opt-in cannot invent missing identity");
    for(auto error:{QSerialPort::ResourceError,QSerialPort::DeviceNotFoundError,QSerialPort::PermissionError,
                    QSerialPort::OpenError,QSerialPort::ReadError,QSerialPort::WriteError,QSerialPort::UnknownError}) {
        p.start(d,0);p.error(error,0);
        check(!p.due(4999) &&p.due(5000),"delayed first attempt error "+QString::number(error));
        for(int n=1;n<=12;++n) {
            const qint64 at=n*5000;
            check(p.candidate({renamed},at)=="COM21" &&p.attempts()==n,"unique renamed candidate attempt "+QString::number(n));
            p.open_failed(error,at);
        }
        check(p.state()=="WAITING_FOR_USER" &&!p.due(999999) &&p.attempts()==12,"twelve attempt ceiling error "+QString::number(error));
    }
    p.start(d,0);p.error(QSerialPort::ResourceError,0);
    check(p.candidate({d,renamed},5000).isEmpty() &&p.state()=="WAITING_FOR_USER","multiple identical adapters require manual selection");
    p.start(d,0);p.error(QSerialPort::ResourceError,0);
    check(p.candidate({moved},5000).isEmpty() &&p.state()=="WAITING_FOR_USER","changed attachment cannot auto-open");
    p.start(d,0);p.error(QSerialPort::ResourceError,0);
    for(int n=1;n<=12;++n) check(p.candidate({},n*5000).isEmpty(),"absent adapter attempt accounted "+QString::number(n));
    check(p.state()=="WAITING_FOR_USER" &&p.attempts()==12,"absence also bounded to twelve polls");
    p.start(d,0);p.error(QSerialPort::ResourceError,0);p.candidate({d},5000);p.opened(5000);p.frame(5100);
    check(p.state()=="RECOVERING" &&p.attempts()==1,"TH3 alone neither confirms nor replenishes incident");
    p.error(QSerialPort::ResourceError,5200);p.candidate({d},10200);
    check(p.attempts()==2,"incomplete handshake retains incident budget");
    p.opened(10200);p.frame(10300);p.confirmed(10400);
    check(p.state()=="CONNECTED" &&p.attempts()==0,"full handshake completes incident");
    p.error(QSerialPort::ResourceError,10500);p.candidate({d},15500);
    check(p.attempts()==1,"new independently confirmed incident has new budget");
    p.start(d,0);p.frame(10);
    check(!p.stale(3509) &&p.stale(3510),"stale threshold is monotonic 3500ms");
    check(!p.stream_expired(8009) &&p.stream_expired(8010),"controlled stream timeout8000ms");
    p.stream_lost(8010);check(p.due(13010),"open-but-silent stream progresses to bounded recovery");
    p.stop();check(!p.due(999999) &&!p.stream_expired(999999),"End cancels every recovery path");
    p.start(d,0);
    for(auto error:{QSerialPort::ParityError,QSerialPort::FramingError,QSerialPort::BreakConditionError,QSerialPort::TimeoutError})
        check(p.error(error,1)==app_serial_recovery_t::RECOVERY_KEEP_STREAM &&p.attempts()==0,"transient stays open "+QString::number(error));
    p.error(QSerialPort::UnsupportedOperationError,2);
    check(p.state()=="WAITING_FOR_USER" &&!p.due(999999),"unsupported settings never auto-loop");
    p.start(d,0);check(p.trusted({renamed},"COM21") &&!p.trusted({renamed,moved},"COM21"),"mutation trust requires unique exact current adapter");
    QJsonArray catalog;
    for(const auto &device:app_desktop_serial_devices()) {
        const QByteArray identity=(device.pnp_instance_id+"|"+device.usb_location_paths.join("|")).toUtf8();
        catalog.append(QJsonObject{{"port",device.port},{"VID",device.has_vendor?int(device.vendor):-1},{"PID",device.has_product?int(device.product):-1},
          {"serial_supplied",!device.serial_number.isEmpty()},{"pnp_supplied",!device.pnp_instance_id.isEmpty()},
          {"location_count",device.usb_location_paths.size()},{"identity_kind",app_desktop_identity_kind(device)},
          {"attachment_sha256",QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex())}});
    }
    const QJsonObject result{{"pass",failures.isEmpty()},{"checks",checks},{"failures",failures},{"catalog_read_only",catalog},
      {"classification","HOST POLICY FIXTURES; catalog read-only, no port open or hardware failure injection"}};
    const auto output=QJsonDocument(result).toJson(QJsonDocument::Indented);std::fwrite(output.constData(),1,size_t(output.size()),stdout);
    return failures.isEmpty()?0:1;
}
