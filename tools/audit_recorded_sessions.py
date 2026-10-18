"""Read-only byte/CSV/sidecar audit of real or explicitly labelled test sessions.

No input is repaired, truncated, relocated or inferred from an embedded producer
tag. Offsets, not decoded text, define the exact raw records (including NULs).
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse, collections, csv, hashlib, json, re, struct


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(directory):
    required = ['raw_input.log', 'parsed.csv', 'metadata.json',
                'session_events.jsonl', 'command_tx.jsonl']
    files = {name: {'bytes': (directory/name).stat().st_size,
                    'sha256': digest(directory/name)} for name in required if (directory/name).exists()}
    assert all(name in files for name in required[:4]), (directory, 'Missing recorder file')
    metadata = json.loads((directory/'metadata.json').read_text(encoding='utf-8-sig'))
    raw = (directory/'raw_input.log').read_bytes()
    with (directory/'parsed.csv').open(encoding='utf-8-sig', newline='') as handle:
        rows = list(csv.DictReader(handle))
    events = [json.loads(x) for x in (directory/'session_events.jsonl').read_text(encoding='utf-8-sig').splitlines() if x]
    commands = [json.loads(x) for x in (directory/'command_tx.jsonl').read_text(encoding='utf-8-sig').splitlines() if x] if 'command_tx.jsonl' in files else []
    assert metadata['raw_byte_count'] == len(raw)
    assert metadata['raw_record_count'] == metadata['csv_row_count'] == len(rows)
    assert metadata['event_count'] == len(events)
    offsets = [int(row['raw_byte_offset']) for row in rows] + [len(raw)]
    assert not rows or offsets[0] == 0
    counts = collections.Counter(); pairs = collections.Counter(); sources = collections.Counter()
    flags = collections.Counter(); profiles = collections.Counter(); deltas = collections.Counter()
    pending = previous = None
    paired = oled = numeric_oled = paired_oled = 0
    anomalies = []; unavailable = []; frame_times = []; source_resets = []; exact_values = []
    field_map = {'frame_source':1, 'source_timestamp_ms':2, 'valid':3, 'valid_flags':4,
                 'error_flags':5, 'calibration_id':6, 'sample_count':7, 'temperature_c':8,
                 'humidity_rh':9, 'v_ntc':10, 'v_chs1101':11, 'rh_pwm':12, 'rh_adc':13,
                 'raw_ntc_code':14, 'raw_chs1101_code':15, 'pwm_period_ticks':16,
                 'pwm_high_ticks':17, 'temperature_source':18, 'humidity_source':19,
                 'temperature_valid':20, 'humidity_valid':21, 'mode_generation':22}
    for i, row in enumerate(rows):
        assert int(row['raw_record_index']) == i+1 and offsets[i] <= offsets[i+1]
        line = raw[offsets[i]:offsets[i+1]]
        counts[row['parse_status']] += 1
        if line.startswith(b'[TEMP_CORR]'):
            pending = dict(re.findall(rb'([a-z_]+)=([^\s]+)', line))
        if any(b < 9 or b > 126 for b in line.rstrip(b'\r\n')):
            anomalies.append({'record':i+1,'offset':offsets[i], 'prefix_hex':line[:40].hex(),
                              'embedded_producer_tag': '[TEMP_CORR]' if b'[TEMP_CORR]' in line else '[OLED_TEMP]' if b'[OLED_TEMP]' in line else None,
                              'classification':row['parse_status']})
        if line.startswith(b'TH3,'):
            tokens = line.decode('ascii').strip().split(',')
            assert len(tokens) == 23 and row['parse_status'] == 'PARSED'
            for key, column in field_map.items(): assert row[key] == tokens[column], (i+1,key)
            assert tokens[18] in ['REAL','SIM'] and tokens[19] in ['REAL','SIM']
            assert bool(int(tokens[20])) == (bool(int(tokens[4]) & 1) and tokens[8] != 'NA')
            assert bool(int(tokens[21])) == (bool(int(tokens[4]) & 2) and tokens[9] != 'NA')
            assert bool(int(tokens[3])) == (tokens[20:22] == ['1','1'])
            sources['/'.join(tokens[18:20])] += 1
            flags[tokens[4]+'/'+tokens[5]] += 1
            frame_times.append(int(tokens[2])); pairs[row['correction_pair_status']] += 1
            if previous is not None:
                delta = (int(tokens[2])-int(previous[2])) & 0xffffffff
                if delta >= 0x80000000: source_resets.append({'record':i+1,'previous_ms':previous[2],'current_ms':tokens[2]})
                else: deltas[str(delta)] += 1
            if row['correction_pair_status'] == 'MATCHED_MCU_FLOAT32':
                assert pending is not None, (i+1,'CSV claims missing producer')
                value = struct.unpack('!f', bytes.fromhex(pending[b'raw_bits'].decode()))[0]
                assert int(pending[b't_ms']) == int(tokens[2]) and int(pending[b'cal']) == int(tokens[6])
                assert float(row['physical_temperature_c']) == value
                assert row['correction_profile'] == pending[b'profile'].decode()
                assert abs(float(pending[b'corrected'][:-1])-float(tokens[8])) <= .00551
                paired += 1; profiles[row['correction_profile']] += 1
                exact_values.append({'record':i+1,'raw':value,'published':float(tokens[8]),'profile':int(row['correction_profile'])})
            elif tokens[18]=='REAL' and tokens[20]=='1':
                assert row['physical_temperature_c']=='NA' and row['correction_profile']=='NA'
                unavailable.append({'record':i+1,'timestamp_ms':int(tokens[2]),'pair_status':row['correction_pair_status']})
            pending = None; previous = tokens
        if line.startswith(b'[OLED_TEMP]'):
            fields = dict(re.findall(rb'([a-z_]+)=([^\s]+)', line))
            oled += 1; numeric_oled += int(fields[b'shown'])
            assert fields[b'status'] == b'0'
            if previous is not None and int(fields[b't_ms']) == int(previous[2]):
                paired_oled += 1
                assert int(fields[b'cal'])==int(previous[6]) and fields[b'source'].decode()==previous[18]
                if previous[20]=='1':
                    value=float(fields[b'temperature'][:-1]); shown=float(fields[b'text'][2:-1])
                    assert abs(value-float(previous[8])) <= .00551 and abs(value-shown)<=.05051
                else: assert fields[b'temperature']==b'NAC' and fields[b'text']==b'T:NAC'
    assert counts['PARSED']==metadata['parsed_total_count']==metadata['parsed_th3_count']
    assert counts['DIAGNOSTIC']==metadata['ignored_diagnostic_count']
    assert counts['MALFORMED']==metadata['malformed_count'] and counts['LEGACY']==metadata['legacy_count']
    for i,event in enumerate(events,1):
        assert event['event_index']==i and event['session_id']==directory.name
        if event.get('source_time_known'):
            assert any(row['parse_status']=='PARSED' and row['source_timestamp_ms']==str(int(event['source_timestamp_ms']))
                       and row['segment_id']==str(int(event['segment_id'])) for row in rows),event
    for name,entry in files.items(): assert digest(directory/name)==entry['sha256'], 'Original changed during read-only audit'
    serial_errors=[{'event':e['event_type'],'host_timestamp':e.get('host_event_timestamp'),
                    'details':e['details']} for e in events if 'qt_error_code' in e.get('details',{})]
    return {'session':str(directory.resolve()),'pass':True,'files':files,'raw_bytes':len(raw),
            'raw_CSV_records':len(rows),'parse_counts':dict(counts),'matched_original_means':paired,
            'original_pair_statuses':dict(pairs),'unavailable_real_originals':unavailable,
            'OLED_diagnostics':oled,'paired_OLED_diagnostics':paired_oled,
            'unpaired_OLED_diagnostics':oled-paired_oled,'OLED_numeric_pages':numeric_oled,'byte_anomalies':anomalies,
            'source_counts':dict(sources),'flags_errors_counts':dict(flags),'configured_profiles':dict(profiles),
            'source_timestamp_delta_counts_ms':dict(deltas),'timestamp_rollbacks':source_resets,
            'source_first_last_ms':frame_times[::len(frame_times)-1] if len(frame_times)>1 else frame_times,
            'events':dict(collections.Counter(e['event_type'] for e in events)),
            'event_count':len(events),'TX_attempts':len(commands),'segments':metadata['segments'],
            'wall_start':metadata['session_start_wall_time'],'wall_end':metadata.get('session_end_wall_time'),
            'port_configuration':metadata['input_path'],'stop_reason':metadata.get('stop_reason'),
            'active':metadata['active'],'recording_error':metadata.get('recording_error'),
            'serial_error_events':serial_errors,
            'Qt_error_code':sorted({e['details']['qt_error_code'] for e in serial_errors if type(e['details']['qt_error_code']) is int}) if any(type(e['details']['qt_error_code']) is int for e in serial_errors) else 'No numeric Qt error recorded',
            'Qt_error_code_unknown_event_count':sum(type(e['details']['qt_error_code']) is not int for e in serial_errors),
            'root_cause':'Clean end_session' if metadata.get('stop_reason')=='end_session' else 'Transport failure recorded; exact Qt/Windows and physical trigger UNKNOWN',
            'read_only':True,'sample_values_for_recalculation':exact_values}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sessions',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    results=[audit(d) for d in sorted(args.sessions.iterdir()) if d.is_dir() and (d/'metadata.json').exists()]
    assert results
    args.output.parent.mkdir(parents=True,exist_ok=True)
    result={'at':datetime.now(timezone.utc).isoformat(),'pass':True,'sessions':results,
            'classification':'Recorded stream integrity only; no independent accuracy or physical fault attribution'}
    args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'pass':True,'sessions':[{'session':Path(x['session']).name,'records':x['raw_CSV_records'],
        'TH3':x['parse_counts'].get('PARSED',0),'originals':x['matched_original_means'],'OLED':x['OLED_diagnostics'],
        'events':x['events'],'TX':x['TX_attempts'],'stop_reason':x['stop_reason'],'byte_anomalies':len(x['byte_anomalies'])} for x in results]},indent=2))


if __name__=='__main__':main()
