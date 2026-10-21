#!/usr/bin/env python3
"""Paired Solar APK instrumentation in a fresh 2 GB Android 15 emulator.
Pass identical instrumentation builds for both production versions. The emulator
and adb run in one process tree, with bounded boot/test times and teardown.
This script never installs anything on a physical phone.
"""
import argparse,hashlib,json,os,re,shutil,subprocess,tarfile,tempfile,time
from pathlib import Path

def verify_driver_config(trace,report):
    """Require evidence that the emulator opened the exact isolated config file.

    strace is filtered to that one path when the process is started; it records
    open calls, never file contents. A written but unread setting is not proof.
    """
    data=trace.read_text(errors='replace') if trace.exists() else ''
    observed=bool(re.search(r'SwiftShader\.ini".*O_RDONLY.*\)\s*=\s*\d+',data))
    report['driver_config_read_observed']=observed
    report['driver_config_trace_sha256']=hashlib.sha256(data.encode()).hexdigest()
    if not observed:raise RuntimeError('The emulator did not observably read the requested SwiftShader.ini; refusing to claim that worker configuration')

def driver_processes(emulator):
    """Inspect only the launched emulator/strace process tree, not other apps."""
    pending=[emulator.pid];seen=set();result=[]
    while pending:
        pid=pending.pop()
        if pid in seen:continue
        seen.add(pid);root=Path('/proc')/str(pid)
        try:
            children=(root/'task'/str(pid)/'children').read_text().split()
            pending.extend(map(int,children))
            maps=[line for line in (root/'maps').read_text().splitlines() if 'swiftshader' in line.lower() or 'libGLES' in line or 'libEGL' in line]
            names=[p.read_text().strip() for p in (root/'task').glob('*/comm')]
            result.append({'pid':pid,'name':(root/'comm').read_text().strip(),'threads':len(names),'thread_names':names,'driver_mappings':maps})
        except (OSError,ValueError):continue
    return result

def wait_for_boot(emulator,adb,serial,env,budget_seconds,report,save):
    """Retry slow ADB polls without extending the original boot deadline."""
    begin=time.monotonic();deadline=begin+budget_seconds
    report['boot_poll_timeouts']=0
    try:
        while True:
            if emulator.poll() is not None:raise RuntimeError('Emulator exited: '+str(emulator.returncode))
            remaining=deadline-time.monotonic()
            if remaining<=0:raise TimeoutError('Android boot exceeded budget')
            try:
                r=subprocess.run([adb,'-s',serial,'shell','getprop','sys.boot_completed'],env=env,capture_output=True,text=True,timeout=min(5,remaining))
                if r.returncode==0 and r.stdout.strip()=='1' and time.monotonic()<=deadline:return
            except subprocess.TimeoutExpired:
                report['boot_poll_timeouts']+=1
                report['boot_seconds']=time.monotonic()-begin;save()
            remaining=deadline-time.monotonic()
            if remaining>0:time.sleep(min(2,remaining))
    finally:
        report['boot_seconds']=time.monotonic()-begin

def classify_evidence(report):
    """Describe an inconclusive fixture separately from an exact pixel mismatch."""
    issues=[];versions={c['version']:c['result'] for c in report['cases']}
    def input_difference(first,second):
        if not isinstance(first,dict) or not isinstance(second,dict):return None
        return sorted(k for k in first.keys()|second.keys() if first.get(k)!=second.get(k))
    for version,result in versions.items():
        cases=result.get('cases',[])
        if len(cases)!=8:issues.append({'scope':version,'kind':'incomplete_instrumentation','completed_cases':len(cases)})
        repeated=False
        for c in cases:
            if c.get('repeatCaptureExact') is False or ('repeatRgbaSha256' in c and c.get('rgbaSha256')!=c['repeatRgbaSha256']):
                repeated=True;fields=input_difference(c.get('inputSha256'),c.get('repeatInputSha256'))
                kind='repeat_inputs_changed' if fields else 'repeat_pixels_changed_with_equal_recorded_inputs' if fields==[] else 'repeat_pixels_changed_without_input_evidence'
                issues.append({'scope':version,'kind':kind,'case':c.get('case'),'changed_input_fields':fields})
        if result.get('status')!='passed' and not repeated:
            issues.append({'scope':version,'kind':'instrumentation_failed','error':result.get('error')})
    old=versions.get('before',{}).get('cases',[]);new=versions.get('after',{}).get('cases',[])
    if 'before' in versions and 'after' in versions:
        report['exact_capture_matches']=sum(x['rgbaSha256']==y['rgbaSha256'] for x,y in zip(old,new))
        for x,y in zip(old,new):
            if x['rgbaSha256']==y['rgbaSha256']:continue
            fields=input_difference(x.get('inputSha256'),y.get('inputSha256'))
            kind='paired_inputs_changed' if fields else 'paired_pixels_changed_with_equal_recorded_inputs' if fields==[] else 'paired_pixels_changed_without_input_evidence'
            issues.append({'scope':'paired','kind':kind,'case':x.get('case'),'changed_input_fields':fields})
    report['evidence_issues']=issues
    report['baseline_inconclusive']=any(i['scope']=='before' for i in issues)
    report['instrumentation_failures']=[v for v,r in versions.items() if r.get('status')!='passed']

def cleanup(emulator,adb,env,report,save):
    """Keep the primary failure and save the report even if teardown times out."""
    report['stage_before_cleanup']=report.get('stage');report['stage']='cleanup'
    errors=[]
    try:
        save()
        if emulator is not None:
            try:
                try:os.killpg(emulator.pid,15);emulator.wait(timeout=10)
                except subprocess.TimeoutExpired:os.killpg(emulator.pid,9);emulator.wait(timeout=5)
            except ProcessLookupError:pass
            except Exception as error:errors.append('Emulator cleanup: '+type(error).__name__+': '+str(error))
        try:subprocess.run([adb,'kill-server'],env=env,check=True,timeout=10,capture_output=True)
        except Exception as error:errors.append('ADB cleanup: '+type(error).__name__+': '+str(error))
    finally:
        if errors:
            report['cleanup_errors']=errors
            if report['status']=='passed':
                report['status']='blocked_or_failed';report['error']='Cleanup did not complete'
        report['stage']='finished';save()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--after',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--boot-seconds',type=int,default=300);p.add_argument('--test-seconds',type=int,default=600)
    p.add_argument('--system-image',default='system-images;android-35;google_apis;x86_64')
    p.add_argument('--aot',action='store_true',help='Compile both packages with ART speed before running; record separately from ordinary installation')
    p.add_argument('--swiftshader-threads',type=int,help='Use an isolated official SwiftShader worker configuration; require a traced config-file read (strace)')
    p.add_argument('--no-accel',action='store_true');a=p.parse_args()
    if a.swiftshader_threads is not None and a.swiftshader_threads<0:p.error('--swiftshader-threads must be nonnegative')
    a.output.mkdir(parents=True,exist_ok=True)
    output=a.output.resolve();sdk=a.sdk.resolve();adb=str(sdk/'platform-tools/adb');serial='emulator-5580'
    env=dict(os.environ,ANDROID_HOME=str(sdk),ANDROID_SDK_ROOT=str(sdk))
    report={'ram_mb':2048,'software_gles':'swiftshader','virtualization':'TCG' if a.no_accel else 'auto','system_image':a.system_image,'art_compile':'speed' if a.aot else 'default','status':'starting','stage':'initializing','cases':[]}
    def save():
        pending=output/'runner.json.tmp';pending.write_text(json.dumps(report,indent=2)+'\n');pending.replace(output/'runner.json')
    def stage(name,version=None):
        report['stage']=name;report['active_version']=version;save()
    def command(args,**kwargs):return subprocess.run(args,env=env,check=True,**kwargs)
    def android(args,**kwargs):return command([adb,'-s',serial,*args],**kwargs)
    save()
    with tempfile.TemporaryDirectory(prefix='solar-avd-') as temp:
        user=Path(temp);env['ANDROID_USER_HOME']=str(user);env['ANDROID_AVD_HOME']=str(user/'avd');(user/'avd').mkdir()
        emulator=None
        try:
            if a.swiftshader_threads is not None:
                config=f'[Processor]\nThreadCount={a.swiftshader_threads}\n[Testing]\nDisableServer=1\n'
                (user/'SwiftShader.ini').write_text(config);(output/'SwiftShader.ini').write_text(config)
                report.update(swiftshader_threads=a.swiftshader_threads,driver_configuration=config,driver_configuration_sha256=hashlib.sha256(config.encode()).hexdigest(),driver_configuration_directory=str(user))
                tracer=shutil.which('strace')
                if not tracer:raise RuntimeError('strace is required to verify the requested emulator SwiftShader configuration')
            stage('creating_avd')
            command([str(sdk/'cmdline-tools/latest/bin/avdmanager'),'create','avd','--force','--name','solar-pair','--package',a.system_image,'--device','pixel_2','--path',str(user/'solar-pair.avd')],input='no\n',text=True,timeout=90,capture_output=True)
            stage('starting_adb')
            command([adb,'start-server'],timeout=20)
            with open(output/'emulator.log','w') as log:
                args=[str(sdk/'emulator/emulator'),'-avd','solar-pair','-port','5580','-no-window','-no-audio','-no-boot-anim','-no-snapshot','-gpu','swiftshader','-memory','2048','-cores','2','-feature','-Vulkan']
                if a.no_accel:args+=['-accel','off']
                if a.swiftshader_threads is not None:
                    # Keep both spellings: some strace/path-namespace combinations
                    # omit openat(AT_FDCWD, "SwiftShader.ini", ...) when only the
                    # absolute filter is supplied. strace runs in this same user
                    # directory, so both filters identify the isolated config.
                    args=[tracer,'-f','-qq','-e','trace=open,openat','-P',str(user/'SwiftShader.ini'),'-P','SwiftShader.ini','-o',str(output/'driver-config-open.log'),*args]
                stage('booting_emulator')
                emulator=subprocess.Popen(args,env=env,cwd=user,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
                wait_for_boot(emulator,adb,serial,env,a.boot_seconds,report,save)
                if a.swiftshader_threads is not None:
                    stage('verifying_driver_configuration')
                    report['emulator_processes']=driver_processes(emulator)
                    verify_driver_config(output/'driver-config-open.log',report);save()
                report['status']='running';stage('configuring_display')
                for cmd in [['shell','wm','size','640x360'],['shell','wm','density','160'],['shell','input','keyevent','82'],['shell','settings','put','secure','immersive_mode_confirmations','confirmed']]:android(cmd,timeout=30)
                for label,root in [('before',a.baseline),('after',a.after)]:
                    stage('preparing_apks',label)
                    target=output/label;target.mkdir(exist_ok=True)
                    apks=[root/'debug/app-debug.apk',root/'androidTest/debug/app-debug-androidTest.apk']
                    hashes={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in apks}
                    report.setdefault('apk_hashes',{})[label]=hashes;stage('installing_apks',label)
                    for f in apks:android(['install','-r',str(f.resolve())],timeout=120)
                    stage('clearing_app_data',label)
                    android(['shell','pm','clear','com.cosmicx.solar'],timeout=30)
                    if a.aot:
                        stage('compiling_art',label)
                        with open(target/'art-compile.txt','w') as f:
                            for package in ['com.cosmicx.solar','com.cosmicx.solar.test']:
                                android(['shell','cmd','package','compile','-m','speed','-f',package],stdout=f,stderr=subprocess.STDOUT,timeout=120)
                    stage('running_instrumentation',label)
                    android(['logcat','-c'],timeout=15)
                    with open(target/'instrumentation.txt','w') as f:
                        android(['shell','am','instrument','-w','-r','-e','performance','true','com.cosmicx.solar.test/com.mundox.solar.SolarInstrumentation'],stdout=f,stderr=subprocess.STDOUT,timeout=a.test_seconds)
                    stage('collecting_diagnostics',label)
                    for name,args in [('memory.txt',['shell','dumpsys','meminfo','com.cosmicx.solar']),('logcat.txt',['logcat','-d'])]:
                        with open(target/name,'w') as f:android(args,stdout=f,stderr=subprocess.STDOUT,timeout=30)
                    stage('collecting_evidence',label)
                    with open(target/'evidence.tar','wb') as f:android(['exec-out','run-as','com.cosmicx.solar','tar','-C','files','-cf','-','solar-performance'],stdout=f,timeout=60)
                    with tarfile.open(target/'evidence.tar') as tar:tar.extractall(target,filter='data')
                    result=json.loads((target/'solar-performance/results.json').read_text());report['cases'].append({'version':label,'apks':hashes,'result':result});classify_evidence(report);save()
                    if result['status']!='passed' and len(result.get('cases',[]))!=8:raise RuntimeError('Incomplete instrumentation for '+label)
                    stage('stopping_app',label)
                    android(['shell','am','force-stop','com.cosmicx.solar'],timeout=20)
                stage('comparing_captures')
                old,new=[case['result']['cases'] for case in report['cases']]
                classify_evidence(report)
                if report['instrumentation_failures']:raise AssertionError('Instrumentation failed: '+', '.join(report['instrumentation_failures']))
                if len(old)!=8 or len(new)!=8 or report['exact_capture_matches']!=8:raise AssertionError('Before/after capture changed')
                if report['evidence_issues']:raise AssertionError('Fixed-scene evidence is inconclusive')
                report['status']='passed'
        except Exception as error:
            report['status']='blocked_or_failed';report['error']=type(error).__name__+': '+str(error)
        finally:
            cleanup(emulator,adb,env,report,save)
    print(json.dumps(report,indent=2));return 0 if report['status']=='passed' else 1
if __name__=='__main__':raise SystemExit(main())
