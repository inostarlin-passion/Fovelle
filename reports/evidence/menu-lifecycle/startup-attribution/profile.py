#!/usr/bin/env python3
"""Attribute startup to common observable boundaries, without summing medians."""
import argparse,json,os,re,statistics,subprocess,threading,time
from pathlib import Path

BOUNDARIES=['application-identity','settings-migrated','application-constructed','window-created','first-event-loop-turn','menus-ready']
STAGES=['spawn_to_identity','settings_preparation','application_construction','window_construction_and_show','window_to_first_event','first_event_to_menus']

def run(executable, output):
    env=dict(os.environ,FOVELLE_DEPLOYMENT_PROBE='1',FOVELLE_SYSTEM_PROBE='1',FOVELLE_STARTUP_PERF='1',FOVELLE_DISABLE_AUTO_UPDATE_CHECK='1',FOVELLE_BENCH_MAXIMIZED='1')
    start=time.perf_counter();p=subprocess.Popen([str(executable)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    watchdog=threading.Timer(15,p.kill);watchdog.start();lines=[];marks={};internal={};events=[]
    try:
        for line in p.stdout:
            t=time.perf_counter();lines.append(line)
            m=re.search(r'^FOVELLE_(?:STARTUP|MAINWINDOW) phase=([\w-]+)(?: elapsed_ms=([\d.]+))?',line)
            if m:
                phase=m[1];events.append({'phase':phase,'parent_ms':(t-start)*1000,'internal_ms':float(m[2]) if m[2] else None,'line':line.rstrip()})
                if phase in BOUNDARIES: marks[phase]=(t-start)*1000
                if line.startswith('FOVELLE_STARTUP') and m[2]:internal[phase]=float(m[2])
        code=p.wait(timeout=3)
    finally:
        watchdog.cancel()
        if p.poll() is None:p.kill();p.wait()
    output.write_text(''.join(lines));assert code==0,(output,code)
    assert set(BOUNDARIES)<=marks.keys(),(output,marks)
    assert 'windows=1 maximized=true' in ''.join(lines),output
    values=[0]+[marks[k] for k in BOUNDARIES]
    values[-1]=max(values[-1],values[-2])
    stages={key:b-a for key,a,b in zip(STAGES,values,values[1:])}
    assert all(v>=0 for v in stages.values()),(output,stages)
    return {'stages_ms':stages,'total_ms':values[-1],'internal':internal,'events':events,'returncode':code}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('qview',type=Path);p.add_argument('fovelle',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--runs',type=int,default=20);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    paths={'qview':a.qview.resolve(),'fovelle':a.fovelle.resolve()};samples={k:[] for k in paths}
    for key,path in paths.items():run(path,a.output/f'{key}-warmup.log')
    for i in range(a.runs):
        for key in (['qview','fovelle'] if i%2==0 else ['fovelle','qview']):samples[key].append(run(paths[key],a.output/f'{key}-{i:02}.log'))
    summary={}
    for key,rows in samples.items():
        summary[key]={'total_mean_ms':statistics.mean(r['total_ms'] for r in rows),'total_median_ms':statistics.median(r['total_ms'] for r in rows),'stages_mean_ms':{s:statistics.mean(r['stages_ms'][s] for r in rows) for s in STAGES},'stages_median_ms':{s:statistics.median(r['stages_ms'][s] for r in rows) for s in STAGES}}
    differences={s:summary['fovelle']['stages_mean_ms'][s]-summary['qview']['stages_mean_ms'][s] for s in STAGES}
    (a.output/'profile.json').write_text(json.dumps({'executables':{k:str(v) for k,v in paths.items()},'runs':samples,'summary':summary,'fovelle_minus_qview_stage_mean_ms':differences},indent=2)+'\n');print(json.dumps({'summary':summary,'difference_means':differences},indent=2))
