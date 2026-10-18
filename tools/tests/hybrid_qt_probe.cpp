#include "app_th3.h"
#include "app_line_buffer.h"
#include "app_session_recorder.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <cassert>
#include <cstdio>
static int checks=0;
#define CHECK(value) do { assert(value); ++checks; } while(0)
static QByteArray change(const QByteArray &line,int index,const QByteArray &value) {
    auto fields=line.trimmed().split(','); fields[index]=value; return fields.join(',')+"\r\n";
}
int main(int argc,char **argv) {
    QCoreApplication application(argc,argv); CHECK(argc==3);
    QFile input(QString::fromLocal8Bit(argv[1])); CHECK(input.open(QIODevice::ReadOnly));
    const QByteArray bytes=input.readAll(); const QList<QByteArray> lines=bytes.split('\n');
    app_th3_timeline_t timeline;
    app_session_options_t options; options.output_directory=QString::fromLocal8Bit(argv[2]);
    options.input_mode="REPLAY"; options.input_path=input.fileName(); options.classification="FIXTURE";
    app_session_recorder_t recorder; CHECK(recorder.app_start_session(options));
    for(int i=0;i<4;++i) {
        const auto frame=app_th3_parse_line(lines[i]+'\n');
        CHECK(frame.parse_status==APP_TH3_PARSED && frame.tokens.size()==23 && frame.protocol=="TH3");
        CHECK(app_th3_temperature_usable(frame)); CHECK(app_th3_humidity_usable(frame)==(i==1 ||i==3));
        CHECK(frame.mode_generation==quint32(i+1));
        const auto time=timeline.app_th3_append_frame(frame); CHECK(time.new_segment);
        CHECK(recorder.app_record_line(frame,time,QDateTime::currentDateTimeUtc()));
        if(i==1 ||i==2) CHECK(frame.source==APP_TH3_SOURCE_MIXED);
    }
    CHECK(recorder.app_stop_session("HOST_VECTOR_COMPLETE"));
    CHECK(recorder.app_session_summary()["parsed_th3_count"].toInt()==4);
    CHECK(recorder.app_session_summary()["parsed_th2_count"].toInt()==0);
    CHECK(recorder.app_session_summary()["protocol"].toString()=="TH3");
    QFile saved(QDir(recorder.app_session_directory()).filePath("raw_input.log")); CHECK(saved.open(QIODevice::ReadOnly)); CHECK(saved.readAll()==bytes);
    const QByteArray real=lines[0]+'\n',mixed=lines[1]+'\n';
    for(const auto &bad : QList<QByteArray>{change(real,1,"MIXED"),change(mixed,1,"REAL"),change(mixed,18,"SIM"),
        change(mixed,11,"1.2"),change(mixed,20,"0"),change(mixed,21,"2"),change(real,3,"1"),change(real,22,"4294967296"),change(real,19,"MIXED")})
        CHECK(app_th3_parse_line(bad).parse_status==APP_TH3_MALFORMED);
    auto legacy=real.trimmed().split(','); while(legacy.size()>18) legacy.removeLast(); legacy[0]="TH2";
    const auto th2=app_th3_parse_line(legacy.join(',')+'\n'); CHECK(th2.parse_status==APP_TH3_LEGACY &&th2.diagnostic=="UNSUPPORTED_LEGACY_PROTOCOL");
    CHECK(!app_th3_temperature_usable(th2) &&!app_th3_humidity_usable(th2) &&th2.raw_line==legacy.join(',')+'\n');
    for(int cut=0;cut<=bytes.size();++cut) {
        app_line_buffer_t buffer; QByteArray reconstructed;
        const auto first=buffer.app_line_feed_bytes(bytes.left(cut)); const auto second=buffer.app_line_feed_bytes(bytes.mid(cut));
        for(const auto &record:first.records) reconstructed+=record.bytes;
        for(const auto &record:second.records) reconstructed+=record.bytes;
        CHECK(reconstructed==bytes && buffer.app_line_pending_bytes()==0);
    }
    app_th3_timeline_t generationTimeline;
    const auto a=app_th3_parse_line(mixed); CHECK(generationTimeline.app_th3_append_frame(a).new_segment);
    const auto b=app_th3_parse_line(change(mixed,22,"999")); CHECK(generationTimeline.app_th3_append_frame(b).new_segment);
    printf("{\"checks\":%d,\"pass\":true,\"classification\":\"HOST TH3 FIXTURE ONLY\"}\n",checks);
    return 0;
}
