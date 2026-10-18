"""Produce phase-aware research figures and nested model-selection diagnostics."""
from pathlib import Path
import argparse,json,sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from explore_temperature_models import fit,predict
from analyze_temperature_calibration import metrics,PHASES,weight

def main():
    p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True);a=p.parse_args();j=json.loads((a.directory/'model_sweep.json').read_text(encoding='utf-8'))
    rows=np.array([v['excel_row'] for v in j['original_rows']]);x=np.array([v['dut_deg_c'] for v in j['original_rows']]);y=np.array([v['reference_deg_c'] for v in j['original_rows']])
    # Nested whole-phase selection: outer labels never choose the inner model.
    # Still the same experiment; nesting cannot remove common physical bias.
    nested={}
    for family in ['guarded_piecewise','piecewise','polynomial']:
        specs=[v['spec'] for v in j['models'] if v['spec']['family']==family];outer=[]
        for start,end,label in PHASES:
            test=(rows>=start)&(rows<=end);train=~test;scores=[]
            for spec in specs:
                errors=[]
                for si,ei,li in PHASES:
                    validation=train&(rows>=si)&(rows<=ei);inner=train&~validation
                    if not any(validation):continue
                    try:errors.extend(abs(predict(fit(spec,x[inner],y[inner]),x[validation])-y[validation]).tolist())
                    except Exception:errors=[float('inf')];break
                scores.append((float(np.mean(errors)),spec['id'],spec))
            score,_,selected=min(scores,key=lambda v:(v[0],v[1]));m=fit(selected,x[train],y[train]);pred=predict(m,x[test])
            outer.append({'outer_phase':label,'training_rows':rows[train].tolist(),'test_rows':rows[test].tolist(),'selected_using_inner_phases':selected,'inner_mae':score,'outer_metrics':metrics(pred,y[test]),'predictions':pred.tolist()})
        nested[family]={'outer':outer,'pooled':metrics(np.concatenate([v['predictions'] for v in outer]),y)}
    selected=[]
    for profile,ident,name in [(1,None,'Ambient offset'),(2,'guarded_piecewise:2,l1','Guarded 2-segment LAD'),(3,'guarded_piecewise:6,huber','Guarded 6-segment Huber')]:
        model=next((v for v in j['models'] if v['id']==ident),None)
        values=x+.9*weight(x) if model is None else np.array(model['predictions'])
        selected.append({'profile':profile,'name':name,'research_id':ident,'model':model['model'] if model else {'offset':.9,'kind':'ambient_envelope'},'full_internal':metrics(values,y),'phase_metrics':{label:metrics(values[(rows>=s)&(rows<=e)],y[(rows>=s)&(rows<=e)]) for s,e,label in PHASES},'phase_cv':model['pooled_phase_holdout'] if model else None,'predictions':values.tolist(),'training_rows':list(range(2,7)) if profile==1 else list(range(2,51))})
    result={'selected_profiles':selected,'nested_whole_phase_selection':nested,'validation_classification':'INTERNAL ONLY; no random sample split, no independently acquired reference, selection still shares one physical experiment','physical_sensitivity':{'R25_1pct_equivalent_degC_at25':298.15**2/3950*np.log(1.01),'self_heating_illustration_degC_at25':(3.3**2*5000/(8200+5000)**2)*1000/1.2,'condition':'Nominal source topology/3.3V/5kohm/1.2mW per C only; not a measured circuit error or uncertainty budget'},'manual_conflict':'ATH5KR8B3950K local PDF p4/p5 column ordering violates Minimum <= Nominal <= Maximum (e.g. 25C:4.951,5.051,5.001 kohm). Do not silently swap labels or fit physical constants to the table.','dynamic_models':'Time-constant compensation, derivative and state-space fits not identifiable from missing timestamps and non-colocated transient pairs; no invented sample interval or phase-dependent live oracle.'}
    (a.directory/'selected_profiles.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    plt.rcParams.update({'font.size':10});fig,axes=plt.subplots(2,2,figsize=(14,9),layout='constrained')
    axes[0,0].plot(rows,y,'k.--',label='Working reference (manual pairs)');axes[0,0].plot(rows,x,'r-',label='Original DUT')
    colors=['C2','#e67e22','C4'];grid=np.linspace(23,38,1001)
    for color,v in zip(colors,selected):
        values=np.array(v['predictions']);axes[0,0].plot(rows,values,color=color,label=v['name'])
        axes[0,1].plot(rows,values-y,color=color,label=v['name'])
        model=next((z for z in j['models'] if z['id']==v['research_id']),None)
        mapped=grid+.9*weight(grid) if model is None else predict(model['model'],grid)
        axes[1,0].plot(grid,mapped-grid,color=color,label=v['name'])
    axes[0,1].plot(rows,x-y,'r--',alpha=.6,label='Original DUT')
    for limit in [-1,0,1]:axes[0,1].axhline(limit,lw=.7,c='gray',ls=':')
    axes[0,0].set(xlabel='Excel row (not elapsed time)',ylabel='Temperature (deg C)',title='All 49 pairs retained; transient mismatch remains')
    axes[0,1].set(xlabel='Excel row',ylabel='DUT minus reference (deg C)',title='Residuals, not independent accuracy')
    axes[1,0].axvspan(25.2,35.6,alpha=.08,color='gray');axes[1,0].set(xlabel='Physical uncorrected mean (deg C)',ylabel='Correction (deg C)',title='Bounded support; zero correction outside 25.2–35.6')
    ids=['baseline','offset_median','affine_huber','piecewise:2,l1','isotonic','polynomial:5','guarded_piecewise:2,l1','guarded_piecewise:6,huber']
    subset=[next(v for v in j['models'] if v['id']==i) for i in ids];positions=np.arange(len(ids))
    axes[1,1].barh(positions-.17,[v['full']['mae_deg_c'] for v in subset],height=.32,label='Exploratory full-data fit')
    axes[1,1].barh(positions+.17,[v['pooled_phase_holdout']['mae_deg_c'] for v in subset],height=.32,label='Whole-phase holdout')
    axes[1,1].set_yticks(positions,ids,fontsize=8);axes[1,1].set(xlabel='MAE (deg C)',title='Complexity can lower fit error while worsening holdout')
    for ax in axes.flat:ax.legend(fontsize=8);ax.grid(alpha=.15)
    fig.savefig(a.directory/'method_comparison.png',dpi=150);plt.close(fig)
    print(json.dumps({'profiles':[{k:v[k] for k in ['profile','name','full_internal','phase_cv']} for v in selected],'nested':{k:v['pooled'] for k,v in nested.items()}},indent=2))
if __name__=='__main__':main()
