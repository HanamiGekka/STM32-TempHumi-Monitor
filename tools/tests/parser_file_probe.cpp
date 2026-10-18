#include "app_th3.h"
#include "app_file_input.h"
#include "app_line_buffer.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// FIXTURE / REPLAY only. These records never claim current board acquisition.
static const QByteArray sim = "TH3,SIM,1000,1,3,0,0,1,29.00,60.00,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,0\r\n";
static const QByteArray real = "TH3,REAL,1234,1,207,32,0,1,28.25,62.50,1.234,3.056,NA,NA,9872,24448,NA,NA,REAL,REAL,1,1,0\r\n";
static QByteArray change(const QByteArray &line, int index, const QByteArray &value)
{
    auto fields = line.trimmed().split(','); fields[index] = value;
    return fields.join(',') + "\r\n";
}
static QJsonArray results;
static int failed = 0;
static void check(const QString &name, bool condition, const QString &detail = QString())
{
    QJsonObject r; r["name"] = name; r["pass"] = condition; r["detail"] = detail;
    results.append(r); if (!condition) ++failed;
    QTextStream(stdout) << (condition ? "PASS " : "FAIL ") << name << " " << detail << '\n';
}
static void parser_tests()
{
    auto a = app_th3_parse_line(sim); auto b = app_th3_parse_line(real);
    check("normal_SIM", a.parse_status == APP_TH3_PARSED && a.source == APP_TH3_SOURCE_SIM && a.tokens.size() == 23 && a.raw_line == sim && a.temperature_c.present && a.temperature_c.value == 29 && a.humidity_rh.value == 60);
    check("REAL_FIXTURE_PWM_NO_SAMPLE", b.parse_status == APP_TH3_PARSED && b.source == APP_TH3_SOURCE_REAL && b.error_flags == 32 && app_th3_temperature_usable(b) && app_th3_humidity_usable(b) && b.raw_ntc_code.value == 9872 && b.raw_chs1101_code.value == 24448 && b.v_ntc.value == 1.234);
    check("physical_NA_not_zero", !a.v_ntc.present && !a.v_chs1101.present && !a.rh_pwm.present && !a.rh_adc.present && !a.raw_ntc_code.present && !a.raw_chs1101_code.present && !a.pwm_period_ticks.present && !a.pwm_high_ticks.present);
    auto invalid = app_th3_parse_line(change(change(change(change(sim,3,"0"),4,"0"),20,"0"),21,"0"));
    check("valid_zero_preserved", invalid.parse_status == APP_TH3_PARSED && !invalid.valid && !app_th3_temperature_usable(invalid) && !app_th3_humidity_usable(invalid));
    auto nt = app_th3_parse_line(change(change(change(sim,8,"NA"),3,"0"),20,"0"));
    check("temperature_NA", nt.parse_status == APP_TH3_PARSED && !nt.temperature_c.present && !app_th3_temperature_usable(nt));
    auto nh = app_th3_parse_line(change(change(change(sim,9,"NA"),3,"0"),21,"0"));
    check("humidity_NA", nh.parse_status == APP_TH3_PARSED && !nh.humidity_rh.present && !app_th3_humidity_usable(nh));
    auto flags = app_th3_parse_line(change(real, 5, "255"));
    check("multiple_error_flags", flags.parse_status == APP_TH3_PARSED && flags.error_flags == 255 && app_th3_temperature_usable(flags));
    auto decimal = app_th3_parse_line(change(real, 4, "65535"));
    check("decimal_uint16_flags", decimal.parse_status == APP_TH3_PARSED && decimal.valid_flags == 65535);
    auto noflag = app_th3_parse_line(change(change(change(sim,4,"2"),3,"0"),20,"0"));
    check("missing_temperature_flag", noflag.parse_status == APP_TH3_PARSED && !app_th3_temperature_usable(noflag) && app_th3_humidity_usable(noflag));
    auto wrap = app_th3_parse_line(change(sim, 2, "4294967295"));
    check("uint32_timestamp_max", wrap.parse_status == APP_TH3_PARSED && wrap.timestamp_ms == 4294967295U);
    auto neg = app_th3_parse_line(change(sim, 8, "-40.00"));
    check("negative_temperature", neg.parse_status == APP_TH3_PARSED && neg.temperature_c.value == -40.0);
    auto edge = app_th3_parse_line(change(change(real, 14, "-32768"), 15, "32767"));
    check("int16_range_edges", edge.parse_status == APP_TH3_PARSED && edge.raw_ntc_code.value == -32768 && edge.raw_chs1101_code.value == 32767);
    auto ticks = app_th3_parse_line(change(change(real, 16, "4294967295"), 17, "0"));
    check("uint32_tick_edges", ticks.parse_status == APP_TH3_PARSED && ticks.pwm_period_ticks.present && ticks.pwm_period_ticks.value == 4294967295U && ticks.pwm_high_ticks.value == 0);
    auto fields = sim.trimmed().split(','); fields.removeLast();
    check("missing_field", app_th3_parse_line(fields.join(',') + "\r\n").parse_status == APP_TH3_MALFORMED);
    check("extra_field", app_th3_parse_line(sim.trimmed() + ",extra\r\n").parse_status == APP_TH3_MALFORMED);
    const QList<QPair<int,QByteArray>> malformed = {{8,"banana"},{8,"nan"},{8,"inf"},{8,"1e400"},{8,"1e39"},{8,""},{8,"29x"},{8," 29"},{2,"-1"},{2,"4294967296"},{2,"1.0"},{3,"2"},{4,"65536"},{5,"0x20"},{6,"-1"},{7,"65536"},{14,"-32769"},{15,"32768"},{16,"4294967296"},{1,"UNKNOWN"},{1,"NA"}};
    int i=0; for (const auto &item : malformed) {
        // Raw/code/tick malformed mutations use REAL so the intended numeric check is exercised.
        const QByteArray base = item.first >= 14 ? real : sim;
        auto m = app_th3_parse_line(change(base,item.first,item.second));
        check(QString("malformed_%1_field%2_%3").arg(++i).arg(item.first).arg(QString::fromLatin1(item.second)), m.parse_status == APP_TH3_MALFORMED && !m.diagnostic.isEmpty() && m.raw_line == change(base,item.first,item.second));
    }
    check("SIM_physical_rejected", app_th3_parse_line(change(sim, 10, "1.000")).parse_status == APP_TH3_MALFORMED);
    check("old_TH_legacy", app_th3_parse_line("TH,29,60\r\n").parse_status == APP_TH3_LEGACY);
    check("ADS_diagnostic", app_th3_parse_line("[ADS_DBG] code=123\r\n").parse_status == APP_TH3_DIAGNOSTIC);
    check("boot_diagnostic", app_th3_parse_line("BOOT OK\r\n").parse_status == APP_TH3_DIAGNOSTIC);
    check("strict_prefix", app_th3_parse_line(" TH2,SIM,0\r\n").parse_status == APP_TH3_DIAGNOSTIC);
    check("embedded_newline_rejected", app_th3_parse_line(sim + real).parse_status == APP_TH3_MALFORMED);
    check("nul_rejected", app_th3_parse_line(change(sim,8,QByteArray("2\0",2))).parse_status == APP_TH3_MALFORMED);
    int parsed=0,diagnostic=0,legacy=0;
    for (const auto &line : QList<QByteArray>{"BOOT OK\r\n",sim,"[ADS_DBG] x\r\n",real,"TH,1,2\r\n"}) {
        auto f=app_th3_parse_line(line); parsed += f.parse_status==APP_TH3_PARSED; diagnostic += f.parse_status==APP_TH3_DIAGNOSTIC; legacy += f.parse_status==APP_TH3_LEGACY;
    }
    check("mixed_text_TH2_classification", parsed==2 && diagnostic==2 && legacy==1);
    check("status_source_text", app_th3_source_text(a.source)=="SIM" && app_th3_source_text(b.source)=="REAL" && app_th3_parse_status_text(a.parse_status)=="PARSED");
    app_th3_timeline_t timeline;
    auto t0=timeline.app_th3_append_frame(app_th3_parse_line(change(sim,2,"4294967200")));
    auto t1=timeline.app_th3_append_frame(app_th3_parse_line(change(sim,2,"40")));
    check("timestamp_wrap_136ms", t0.new_segment && !t1.new_segment && t1.segment_id==t0.segment_id && std::abs(t1.elapsed_s-0.136)<1e-12 && t1.unwrapped_timestamp_ms==4294967336ULL);
    auto same=timeline.app_th3_append_frame(app_th3_parse_line(change(sim,2,"40")));
    check("same_timestamp_allowed", !same.new_segment && same.elapsed_s==t1.elapsed_s);
    timeline.app_th3_reset_segment("TEST_RESET"); auto x=timeline.app_th3_append_frame(a);
    auto y=timeline.app_th3_append_frame(app_th3_parse_line(change(sim,2,"0")));
    check("timestamp_rollback_new_segment", x.new_segment && y.new_segment && y.segment_id==x.segment_id+1 && y.elapsed_s==0 && y.reason=="TIMESTAMP_ROLLBACK");
    auto source=timeline.app_th3_append_frame(b);
    check("source_change_new_segment", source.new_segment && source.segment_id==y.segment_id+1 && source.elapsed_s==0 && source.reason=="SOURCE_CHANGE");
    timeline.app_th3_reset_segment("FILE_REPLACED"); auto reset=timeline.app_th3_append_frame(b);
    check("explicit_file_segment_reset", reset.new_segment && reset.segment_id==source.segment_id+1 && reset.reason=="FILE_REPLACED");
}
static bool write_file(const QString &path, const QByteArray &bytes, bool append=false)
{
    QFile f(path); if(!f.open(QIODevice::WriteOnly|(append?QIODevice::Append:QIODevice::Truncate))) return false;
    return f.write(bytes)==bytes.size() && f.flush();
}
static QByteArray bytes(const app_file_poll_t &p) { QByteArray all; for(const auto &r:p.records) all+=r.bytes; return all; }
static void file_tests(const QString &scratch)
{
    QDir().mkpath(scratch); const QString path=QDir(scratch).filePath("input.txt");
    QFile::remove(path); app_file_input_t input; input.app_file_set_path(path);
    auto missing=input.app_file_poll(); check("file_missing_waiting", !missing.available && missing.lines.isEmpty());
    write_file(path,sim); auto whole=input.app_file_poll();
    check("whole_line_appearance", whole.available && whole.reset && whole.lines.size()==1 && whole.lines[0]==sim && whole.offset==sim.size());
    check("growth_no_duplicate", input.app_file_poll().records.isEmpty());
    const int split=27; write_file(path,sim.left(split),true); auto half=input.app_file_poll();
    check("split_line_pending", half.lines.isEmpty() && half.pending_bytes==split);
    write_file(path,sim.mid(split),true); auto finished=input.app_file_poll();
    check("split_line_no_loss", finished.lines.size()==1 && finished.lines[0]==sim && finished.pending_bytes==0 && input.app_file_poll().records.isEmpty());
    write_file(path,sim+real+sim,true); auto multi=input.app_file_poll();
    check("multiple_lines_in_one_chunk", multi.lines.size()==3 && bytes(multi)==sim+real+sim);
    write_file(path,real.left(real.size()-2),true); auto noend=input.app_file_poll();
    check("missing_newline_waiting", noend.lines.isEmpty() && noend.pending_bytes==real.size()-2);
    write_file(path,"\r",true); auto cr=input.app_file_poll();
    check("CR_without_LF_waits", cr.lines.isEmpty() && cr.pending_bytes==real.size()-1);
    write_file(path,"\n",true); auto lf=input.app_file_poll();
    check("CRLF_split_preserved", lf.lines.size()==1 && lf.lines[0]==real);
    write_file(path,sim); auto truncated=input.app_file_poll();
    check("truncate_offset_reset", truncated.reset && truncated.reset_reason=="TRUNCATED" && truncated.lines.size()==1 && truncated.lines[0]==sim && truncated.offset==sim.size());
    const QString replacement=path+".new"; write_file(replacement,real); QFile::remove(path); QFile::rename(replacement,path);
    auto replaced=input.app_file_poll();
    check("replacement_file_ID", replaced.reset && replaced.reset_reason=="REPLACED" && replaced.lines.size()==1 && replaced.lines[0]==real);
    QByteArray newrun=change(sim,2,"0")+sim+sim; write_file(path,newrun); auto rewritten=input.app_file_poll();
    check("truncate_regrow_same_handle_detected", rewritten.reset && rewritten.lines.size()==3 && bytes(rewritten)==newrun && rewritten.reset_reason=="REWRITTEN");
    app_th3_timeline_t timeline; timeline.app_th3_append_frame(app_th3_parse_line(sim));
    auto restart=timeline.app_th3_append_frame(app_th3_parse_line(change(sim,2,"0")));
    check("simulator_restart_timestamp", restart.new_segment && restart.reason=="TIMESTAMP_ROLLBACK" && restart.elapsed_s==0);
    QFile::remove(path); auto disappeared=input.app_file_poll();
    check("file_disappears_safe", !disappeared.available && disappeared.lines.isEmpty());
    write_file(path,sim); auto reappear=input.app_file_poll();
    check("file_reappears_recovers", reappear.available && reappear.reset && reappear.reset_reason=="REAPPEARED" && reappear.lines.size()==1 && reappear.lines[0]==sim);
    // Pause does not change the adapter state; newly appended data is consumed once on resume.
    write_file(path,real,true); auto resume=input.app_file_poll();
    check("pause_resume_keeps_offset", !resume.reset && resume.lines.size()==1 && resume.lines[0]==real && input.app_file_poll().records.isEmpty());
#ifdef Q_OS_WIN
    // Existing files can briefly reject reads (e.g. an exclusive writer). This is not reappearance.
    write_file(path,sim.left(split),true); input.app_file_poll();
    const qint64 before_failure=input.app_file_offset();
    const HANDLE exclusive=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check("exclusive_file_lock_acquired",exclusive!=INVALID_HANDLE_VALUE);
    auto locked=input.app_file_poll();
    check("existing_file_read_failure_reports_error",!locked.available && !locked.error_text.isEmpty());
    check("transient_failure_keeps_offset_pending",locked.offset==before_failure && locked.pending_bytes==split && locked.records.isEmpty());
    if(exclusive!=INVALID_HANDLE_VALUE) CloseHandle(exclusive);
    auto unlocked=input.app_file_poll();
    check("transient_recovery_no_replay_or_reset",unlocked.available && !unlocked.reset && unlocked.records.isEmpty() && unlocked.offset==before_failure && unlocked.pending_bytes==split);
    write_file(path,sim.mid(split),true); auto after_failure=input.app_file_poll();
    check("split_line_after_read_failure_no_loss",after_failure.lines.size()==1 && after_failure.lines[0]==sim && input.app_file_poll().records.isEmpty());
#endif
    write_file(path,QByteArray()); input.app_file_poll(); QByteArray huge; for(int n=0;n<7000;++n) huge+=sim;
    write_file(path,huge,true); QByteArray seen; int pollcount=0,linecount=0,maxpending=0; bool bound=true;
    while(input.app_file_offset()<huge.size() && pollcount<20) { auto p=input.app_file_poll(); seen+=bytes(p); linecount+=p.lines.size(); maxpending=qMax(maxpending,p.pending_bytes); bound &= p.offset-(pollcount==0?0:qMin<qint64>(pollcount*256*1024,huge.size()))<=256*1024; ++pollcount; }
    check("bounded_read_many_lines_no_loss_duplicate", seen==huge && linecount==7000 && pollcount>=2 && bound && maxpending<=64*1024);
    write_file(path,QByteArray()); input.app_file_poll(); QByteArray oversize(140000,'X'); write_file(path,oversize,true); auto over=input.app_file_poll();
    check("oversize_no_newline_bounded_error", over.lines.isEmpty() && !over.overflow_fragments.isEmpty() && over.error_text=="LINE_TOO_LONG" && over.pending_bytes<=64*1024);
    write_file(path,"\n"+sim,true); auto recover=input.app_file_poll();
    check("oversize_raw_byte_order_and_recovery", bytes(over)+bytes(recover)==oversize+"\n"+sim && recover.lines.size()==1 && recover.lines[0]==sim && recover.pending_bytes==0);
    write_file(path,sim.left(split),true); input.app_file_poll(); write_file(path,real); auto clearhalf=input.app_file_poll();
    check("replacement_clears_old_half_line", clearhalf.reset && clearhalf.lines.size()==1 && clearhalf.lines[0]==real && clearhalf.pending_bytes==0);
}
static void append_batch(const app_line_batch_t &batch, QByteArray *raw, QList<QByteArray> *lines,
                         bool *bounded, bool *overflow)
{
    for (const auto &record : batch.records) {
        *raw += record.bytes;
        *bounded &= !record.bytes.isEmpty() && record.bytes.size() <= APP_LINE_MAX_RECORD_BYTES;
        *overflow |= record.overflow;
    }
    lines->append(batch.lines);
    *bounded &= batch.pending_bytes <= APP_LINE_MAX_RECORD_BYTES;
}
static QList<QByteArray> expected_complete_lines(const QByteArray &data)
{
    QList<QByteArray> expected;
    const auto parts=data.split('\n');
    for(int i=0;i+1<parts.size();++i) expected.append(parts[i]+"\n");
    return expected;
}
static void chunk_tests()
{
    const QByteArray dataset="BOOT OK\r\n"+sim+"\n"+real+"TH2,SIM,0,broken\r\nTH,1,2\n[ADS_DBG] code=1\r\n"+QByteArray("bad\0value\n",10);
    const auto expected=expected_complete_lines(dataset);
    for(int boundary=0;boundary<=dataset.size();++boundary) {
        app_line_buffer_t buffer; QByteArray raw; QList<QByteArray> lines; bool bounded=true,overflow=false;
        append_batch(buffer.app_line_feed_bytes(dataset.left(boundary)),&raw,&lines,&bounded,&overflow);
        const int pending=buffer.app_line_pending_bytes(); auto zero=buffer.app_line_feed_bytes(QByteArray());
        bounded &= zero.records.isEmpty() && zero.pending_bytes==pending;
        append_batch(buffer.app_line_feed_bytes(dataset.mid(boundary)),&raw,&lines,&bounded,&overflow);
        check(QString("all_byte_boundaries_%1").arg(boundary),raw==dataset && lines==expected && bounded && !overflow && buffer.app_line_pending_bytes()==0);
    }
    app_line_buffer_t single; QByteArray raw; QList<QByteArray> lines; bool bounded=true,overflow=false;
    for(char byte:dataset) append_batch(single.app_line_feed_bytes(QByteArray(1,byte)),&raw,&lines,&bounded,&overflow);
    check("one_byte_chunks_CRLF_multi_malformed_exact",raw==dataset && lines==expected && bounded && !overflow && single.app_line_pending_bytes()==0);
    auto empty=single.app_line_feed_bytes(QByteArray());
    check("zero_byte_no_repeat",empty.records.isEmpty() && empty.pending_bytes==0);
    const QByteArray partial=sim.left(sim.size()-1); app_line_buffer_t tail;
    check("missing_LF_keeps_CR_pending",tail.app_line_feed_bytes(partial).records.isEmpty() && tail.app_line_pending_bytes()==partial.size());
    auto completed=tail.app_line_feed_bytes("\n");
    check("later_LF_restores_original_CRLF",completed.lines.size()==1 && completed.lines[0]==sim && completed.pending_bytes==0);
    tail.app_line_feed_bytes("abandoned partial"); tail.app_line_reset();
    auto reset=tail.app_line_feed_bytes(sim);
    check("explicit_reset_drops_only_pending_in_previous_segment",reset.lines.size()==1 && reset.lines[0]==sim && reset.pending_bytes==0);
    app_line_buffer_t limit;
    QByteArray exact(APP_LINE_MAX_RECORD_BYTES-1,'A'); exact+='\n';
    auto exact_batch=limit.app_line_feed_bytes(exact);
    check("exact_64KiB_complete_line_allowed",exact_batch.lines.size()==1 && exact_batch.lines[0]==exact && exact_batch.error_text.isEmpty());
    const QByteArray too_long=QByteArray(3*APP_LINE_MAX_RECORD_BYTES+17,'B')+"\r\n"+sim;
    auto huge=limit.app_line_feed_bytes(too_long); raw.clear(); lines.clear(); bounded=true;overflow=false;
    append_batch(huge,&raw,&lines,&bounded,&overflow);
    check("very_long_single_chunk_fragment_bound_byte_identity",raw==too_long && bounded && overflow && lines.size()==1 && lines[0]==sim && huge.error_text=="LINE_TOO_LONG" && huge.pending_bytes==0);
    auto more=limit.app_line_feed_bytes(real);
    check("long_line_recovery_no_duplicate",more.lines.size()==1 && more.lines[0]==real && more.overflow_fragments.isEmpty());
    app_line_buffer_t endless;
    QByteArray no_newline(5*APP_LINE_MAX_RECORD_BYTES+9,'C'); raw.clear();lines.clear();bounded=true;overflow=false;
    append_batch(endless.app_line_feed_bytes(no_newline),&raw,&lines,&bounded,&overflow);
    append_batch(endless.app_line_feed_bytes("\n"+sim),&raw,&lines,&bounded,&overflow);
    check("endless_line_then_newline_raw_order_recovery",raw==no_newline+"\n"+sim && lines.size()==1 && lines[0]==sim && bounded && overflow && endless.app_line_pending_bytes()==0);
    app_line_buffer_t overflowing_reset; overflowing_reset.app_line_feed_bytes(no_newline); overflowing_reset.app_line_reset();
    auto fresh=overflowing_reset.app_line_feed_bytes(sim);
    check("reset_clears_overflow_state",fresh.lines.size()==1 && fresh.lines[0]==sim && fresh.error_text.isEmpty());
    QByteArray burst; for(int i=0;i<1000;++i) burst+=dataset;
    const auto burst_lines=expected_complete_lines(burst);
    for(quint32 seed=1;seed<=24;++seed) {
        app_line_buffer_t random; quint32 state=seed; raw.clear();lines.clear();bounded=true;overflow=false;int position=0;
        while(position<burst.size()) { state^=state<<13; state^=state>>17; state^=state<<5; const int chunk=1+(state%17011); auto batch=random.app_line_feed_bytes(burst.mid(position,chunk)); position+=qMin(chunk,burst.size()-position); append_batch(batch,&raw,&lines,&bounded,&overflow); }
        check(QString("random_split_stream_seed_%1").arg(seed),raw==burst && lines==burst_lines && bounded && !overflow && random.app_line_pending_bytes()==0);
    }
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv); if(argc<4) return 2;
    const QString phase=QString::fromLocal8Bit(argv[1]);
    if(phase=="parser") parser_tests(); else if(phase=="file") file_tests(QString::fromLocal8Bit(argv[2])); else if(phase=="chunks") chunk_tests(); else return 2;
    QJsonObject summary; summary["phase"]=phase; summary["classification"]="SIMULATED / FIXTURE / REPLAY; NOT TESTED ON HARDWARE";
    summary["passed"]=results.size()-failed; summary["failed"]=failed; summary["status"]=failed?"FAIL":"PASS"; summary["cases"]=results;
    QFile out(QString::fromLocal8Bit(argv[3])); if(!out.open(QIODevice::WriteOnly)) return 3; out.write(QJsonDocument(summary).toJson()); return failed?1:0;
}
