#!/usr/bin/env python3
"""Paired Solar APK instrumentation in a fresh 2 GB Android 15 emulator.
Pass identical instrumentation builds for both production versions. The emulator
and adb run in one process tree, with bounded boot/test times and teardown.
This script never installs anything on a physical phone.
"""
import argparse,hashlib,json,os,subprocess,tarfile,tempfile,time
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--after',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--boot-seconds',type=int,default=300);p.add_argument('--test-seconds',type=int,default=600)
    p.add_argument('--no-accel',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    output=a.output.resolve();sdk=a.sdk.resolve();adb=str(sdk/'platform-tools/adb');serial='emulator-5580'
    env=dict(os.environ,ANDROID_HOME=str(sdk),ANDROID_SDK_ROOT=str(sdk))
    report={'ram_mb':2048,'software_gles':'swiftshader','virtualization':'TCG' if a.no_accel else 'auto','status':'starting','cases':[]}
    def save(): (output/'runner.json').write_text(json.dumps(report,indent=2)+'\n')
    def command(args,**kwargs):return subprocess.run(args,env=env,check=True,**kwargs)
    def android(args,**kwargs):return command([adb,'-s',serial,*args],**kwargs)
    with tempfile.TemporaryDirectory(prefix='solar-avd-') as temp:
        user=Path(temp);env['ANDROID_USER_HOME']=str(user);env['ANDROID_AVD_HOME']=str(user/'avd');(user/'avd').mkdir()
        emulator=None
        try:
            command([str(sdk/'cmdline-tools/latest/bin/avdmanager'),'create','avd','--force','--name','solar-pair','--package','system-images;android-35;google_apis;x86_64','--device','pixel_2','--path',str(user/'solar-pair.avd')],input='no\n',text=True,timeout=90,capture_output=True)
            command([adb,'start-server'],timeout=20)
            with open(output/'emulator.log','w') as log:
                args=[str(sdk/'emulator/emulator'),'-avd','solar-pair','-port','5580','-no-window','-no-audio','-no-boot-anim','-no-snapshot','-gpu','swiftshader','-memory','2048','-cores','2','-feature','-Vulkan']
                if a.no_accel:args+=['-accel','off']
                emulator=subprocess.Popen(args,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
                begin=time.monotonic();booted=False
                while time.monotonic()-begin<a.boot_seconds:
                    if emulator.poll() is not None:raise RuntimeError('Emulator exited: '+str(emulator.returncode))
                    r=subprocess.run([adb,'-s',serial,'shell','getprop','sys.boot_completed'],env=env,capture_output=True,text=True,timeout=5)
                    if r.stdout.strip()=='1':booted=True;break
                    time.sleep(2)
                report['boot_seconds']=time.monotonic()-begin
                if not booted:raise TimeoutError('Android boot exceeded budget')
                for cmd in [['shell','wm','size','640x360'],['shell','wm','density','160'],['shell','input','keyevent','82'],['shell','settings','put','secure','immersive_mode_confirmations','confirmed']]:android(cmd,timeout=30)
                for label,root in [('before',a.baseline),('after',a.after)]:
                    target=output/label;target.mkdir(exist_ok=True)
                    apks=[root/'debug/app-debug.apk',root/'androidTest/debug/app-debug-androidTest.apk']
                    hashes={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in apks}
                    for f in apks:android(['install','-r',str(f.resolve())],timeout=120)
                    android(['shell','pm','clear','com.cosmicx.solar'],timeout=30)
                    android(['logcat','-c'],timeout=15)
                    with open(target/'instrumentation.txt','w') as f:
                        android(['shell','am','instrument','-w','-r','-e','performance','true','com.cosmicx.solar.test/com.mundox.solar.SolarInstrumentation'],stdout=f,stderr=subprocess.STDOUT,timeout=a.test_seconds)
                    for name,args in [('memory.txt',['shell','dumpsys','meminfo','com.cosmicx.solar']),('logcat.txt',['logcat','-d'])]:
                        with open(target/name,'w') as f:android(args,stdout=f,stderr=subprocess.STDOUT,timeout=30)
                    with open(target/'evidence.tar','wb') as f:android(['exec-out','run-as','com.cosmicx.solar','tar','-C','files','-cf','-','solar-performance'],stdout=f,timeout=60)
                    with tarfile.open(target/'evidence.tar') as tar:tar.extractall(target,filter='data')
                    result=json.loads((target/'solar-performance/results.json').read_text());report['cases'].append({'version':label,'apks':hashes,'result':result});save()
                    if result['status']!='passed':raise RuntimeError('Instrumentation failed for '+label)
                    android(['shell','am','force-stop','com.cosmicx.solar'],timeout=20)
                old,new=[case['result']['cases'] for case in report['cases']]
                report['exact_capture_matches']=sum(x['rgbaSha256']==y['rgbaSha256'] for x,y in zip(old,new))
                if len(old)!=8 or len(new)!=8 or report['exact_capture_matches']!=8:raise AssertionError('Before/after capture changed')
                report['status']='passed'
        except Exception as error:
            report['status']='blocked_or_failed';report['error']=type(error).__name__+': '+str(error)
        finally:
            if emulator is not None:
                try:os.killpg(emulator.pid,15);emulator.wait(timeout=10)
                except (ProcessLookupError,subprocess.TimeoutExpired):
                    try:os.killpg(emulator.pid,9);emulator.wait(timeout=5)
                    except ProcessLookupError:pass
            subprocess.run([adb,'kill-server'],env=env,timeout=10,capture_output=True)
            save()
    print(json.dumps(report,indent=2));return 0 if report['status']=='passed' else 1
if __name__=='__main__':raise SystemExit(main())
