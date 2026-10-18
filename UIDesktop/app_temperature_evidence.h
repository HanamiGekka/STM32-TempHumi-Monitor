#ifndef APP_TEMPERATURE_EVIDENCE_H
#define APP_TEMPERATURE_EVIDENCE_H
#include "app_th3.h"
#include <QRegularExpression>
#include <cmath>
#include <cstring>

// Observe MCU-produced evidence only. Never recalculate a sensor calibration.
// A complete diagnostic is consumed once by the immediately following TH3,
// with matching timestamp/calibration and agreeing published value. Missing or
// stale evidence stays missing; latest ADC is not the physical window mean.
class app_temperature_evidence_t {
public:
    void reset() { m_pending=false; }
    void observe(const QByteArray &line) {
        if(!line.startsWith("[TEMP_CORR]")) return;
        reset();
        static const QRegularExpression pattern(QStringLiteral(
            "^\\[TEMP_CORR\\] raw=([-0-9.]+)C corrected=([-0-9.]+)C offset=([-0-9.]+)C raw_bits=([0-9A-F]{8}) cal=([0-3]) state=(PROVISIONAL|UNCORRECTED) t_ms=([0-9]+) profile=([0-3])$"));
        auto match=pattern.match(QString::fromLatin1(line.trimmed()));if(!match.hasMatch()) return;
        bool ok=false;quint32 bits=match.captured(4).toUInt(&ok,16);if(!ok) return;
        float raw;static_assert(sizeof(raw)==sizeof(bits),"IEEE float32 evidence required");std::memcpy(&raw,&bits,sizeof(raw));
        m_corrected=match.captured(2).toDouble(&ok);if(!ok || !std::isfinite(raw) || !std::isfinite(m_corrected)) return;
        double rounded=match.captured(1).toDouble(&ok);if(!ok || std::abs(rounded-double(raw))>.00051) return;
        const double offset=match.captured(3).toDouble(&ok);if(!ok || std::abs((m_corrected-double(raw))-offset)>.00102) return;
        m_timestamp=match.captured(7).toUInt(&ok);if(!ok) return;
        m_cal=match.captured(5).toUInt();m_profile=match.captured(8).toUInt();
        if((m_cal!=0 && m_cal!=m_profile) || ((m_cal!=0)!=(match.captured(6)=="PROVISIONAL"))) return;
        m_raw=double(raw);m_pending=true;
    }
    void attach(app_th3_frame_t &frame) {
        if(frame.parse_status!=APP_TH3_PARSED) return;
        if(m_pending && frame.timestamp_ms==m_timestamp && frame.calibration_id==m_cal
           &&frame.temperature_source==APP_TH3_SOURCE_REAL &&app_th3_temperature_usable(frame)
           &&std::abs(frame.temperature_c.value-m_corrected)<=.00551) {
            frame.physical_temperature_c.present=true;frame.physical_temperature_c.value=m_raw;
            frame.correction_profile=int(m_profile);
            frame.correction_pair_status="MATCHED_MCU_FLOAT32";
        }
        reset();
    }
private:
    bool m_pending=false;double m_raw=0,m_corrected=0;quint32 m_timestamp=0;
    quint16 m_cal=0,m_profile=0;
};
#endif
