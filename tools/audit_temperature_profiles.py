"""Reconcile recorded MCU originals, configured fit, wire output and CSV3 bytes."""
from pathlib import Path
import argparse,csv,hashlib,json,re,struct
import numpy as np
from explore_temperature_models import predict
from analyze_temperature_calibration import weight

def main():
    p=argparse.ArgumentParser();p.add_argument('--sessions',type=Path,required=True);p.add_argument('--sweep',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--allow-injected-transport-stop',action='store_true');p.add_argument('--require-oled',action='store_true');a=p.parse_args()
    models=json.loads(a.sweep.read_text(encoding='utf-8'))['models'];selected={2:next(v['model'] for v in models if v['id']=='guarded_piecewise:2,l1'),3:next(v['model'] for v in models if v['id']=='guarded_piecewise:6,huber')};results=[]
    # Match the firmware's float32 support constants at the exact endpoints.
    support_low,support_high=float(np.float32(25.2)),float(np.float32(35.6))
    directories=[a.sessions] if (a.sessions/'metadata.json').is_file() else sorted(a.sessions.iterdir())
    for directory in directories:
        if not (directory/'metadata.json').exists():continue
        metadata=json.loads((directory/'metadata.json').read_text());raw=(directory/'raw_input.log').read_bytes();lines=raw.splitlines(keepends=True)
        rows=list(csv.DictReader((directory/'parsed.csv').open(encoding='utf-8',newline='')));assert len(rows)==len(lines)
        allowed=['end_session']+(['transport_or_parse_error'] if a.allow_injected_transport_stop else [])
        assert metadata['csv_schema_version']==3 and metadata['stop_reason'] in allowed
        assert metadata['active'] is False and not metadata.get('recording_error')
        offset=0;pending=None;frames=paired=0;active={str(i):0 for i in range(4)};configured={str(i):0 for i in range(4)};max_delta=0.;rs=rr=0
        oled=shown=leading_oled=0;last_frame=None;last_evidence=None;pending_oled=False
        for i,(line,row) in enumerate(zip(lines,rows),1):
            assert int(row['raw_record_index'])==i and int(row['raw_byte_offset'])==offset;offset+=len(line)
            if line.startswith(b'[TEMP_CORR]'):
                fields=dict(re.findall(rb'([a-z_]+)=([^\s]+)',line));pending=fields
            if line.startswith(b'[OLED_TEMP]'):
                fields=dict(re.findall(rb'([a-z_]+)=([^\s]+)',line))
                assert fields[b'status']==b'0','OLED transport failure'
                if last_frame is None:
                    # A reconnect can start between the previous TH3 and its
                    # subsequent OLED write. Preserve/classify it, never pair
                    # it with the following frame or count it as verified data.
                    leading_oled+=1;assert leading_oled==1,'Multiple orphan OLED windows'
                    continue
                assert last_frame is not None and pending_oled,'Unexpected/repeated OLED evidence'
                assert int(fields[b't_ms'])==int(last_frame[2]) and int(fields[b'cal'])==int(last_frame[6])
                assert fields[b'source'].decode()==last_frame[18] and fields[b'status']==b'0','OLED transport failure'
                if last_frame[20]=='1':
                    value=float(fields[b'temperature'][:-1]);display=float(fields[b'text'][2:-1])
                    assert abs(value-float(last_frame[8]))<=.00551
                    assert abs(display-value)<=.05051,'One-decimal glyph text disagrees with snapshot'
                    if last_evidence is not None:
                        assert fields[b'temperature']==last_evidence[b'corrected']
                        assert fields[b'profile']==last_evidence[b'profile']
                else:
                    assert fields[b'temperature']==b'NAC' and fields[b'text']==b'T:NAC'
                assert fields[b'shown'] in [b'0',b'1']
                if last_frame[18:20]!=['REAL','REAL']:assert fields[b'shown']==b'1'
                oled+=1;shown+=int(fields[b'shown']);pending_oled=False
            if not line.startswith(b'TH3,'):continue
            if a.require_oled:assert not pending_oled,'An interior TH3 window lost OLED evidence'
            frames+=1;tokens=line.decode('ascii').strip().split(',');assert len(tokens)==23
            last_frame=tokens;last_evidence=pending;pending_oled=True
            assert row['temperature_c']==tokens[8] and row['calibration_id']==tokens[6]
            if tokens[18]=='SIM':
                assert tokens[6]=='0' and row['physical_temperature_c']=='NA' and row['correction_profile']=='NA'
            if tokens[18:20]==['REAL','SIM']:
                rs+=1;assert tokens[20:22]==['1','1'] and tokens[5]=='0'
            if tokens[18:20]==['REAL','REAL']:rr+=1
            if pending is not None and tokens[18]=='REAL' and tokens[20]=='1':
                t=struct.unpack('<f',bytes.fromhex(pending[b'raw_bits'].decode())[::-1])[0]
                profile=int(pending[b'profile']);cal=int(pending[b'cal']);assert int(pending[b't_ms'])==int(tokens[2]) and cal==int(tokens[6])
                assert float(row['physical_temperature_c'])==t and int(row['correction_profile'])==profile
                assert row['correction_pair_status']=='MATCHED_MCU_FLOAT32';paired+=1;configured[str(profile)]+=1;active[str(cal)]+=1
                if profile==0:expected=t
                elif profile==1:expected=float(t+.9*weight(np.array([t]))[0])
                else:expected=float(predict(selected[profile],np.array([t]))[0])
                delta=abs(expected-float(tokens[8]));max_delta=max(max_delta,delta);assert delta<=.00503,(i,profile,t,delta)
                assert cal==(profile if profile and support_low<t<support_high else 0),(i,t,profile,cal)
            pending=None
        if a.require_oled:assert frames>0 and (oled==frames or (oled==frames-1 and pending_oled)),'Required interior OLED evidence missing'
        results.append({'session':directory.name,'raw_bytes':len(raw),'raw_sha256':hashlib.sha256(raw).hexdigest(),'records':len(rows),'th3_frames':frames,'paired_original_means':paired,'real_sim_frames':rs,'real_real_frames':rr,'configured_profile_counts':configured,'active_calibration_id_counts':active,'maximum_model_vs_wire_error_deg_c':max_delta,'oled_windows':oled,'oled_numeric_pages':shown,'oled_leading_capture_boundary_unpaired':leading_oled,'oled_final_capture_boundary_unobserved':bool(a.require_oled and pending_oled),'stop_reason':metadata['stop_reason'],'pass':True})
    assert results
    if a.require_oled:assert sum(s['oled_windows'] for s in results)>0,'No complete OLED window observed'
    result={'pass':True,'sessions':results,'classification':'Exact producer/CSV/model reconciliation, not independent reference accuracy'};a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
