#!/usr/bin/env python3
"""Verify built Solar APKs against their frozen source snapshots."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import zipfile

ROOT=Path('/workspace/scratch/d0e1d0a0e3a1')
WORK=ROOT/'closure/unification/solar'
OUT=ROOT/'mundo-X/results/repository-unification-2026-10-21/solar/android-builds'
SDK=Path('/workspace/scratch/7def14e3f3d9/solar-build/sdk')
JAVA=Path('/workspace/scratch/7def14e3f3d9/solar-build/jdk/jdk-17.0.20.1+1')
env=dict(os.environ,JAVA_HOME=str(JAVA),PATH=str(JAVA/'bin')+os.pathsep+os.environ.get('PATH',''))
sha=lambda data:hashlib.sha256(data).hexdigest()
variant=sys.argv[1]
if variant not in ('candidate','baseline-defined','candidate-review'):raise SystemExit('invalid variant')
build=json.loads((OUT/(variant+'-build.json')).read_text())
assert build['returncode']==0 and build['source_unchanged_after_build']
manifest=json.loads((WORK/(variant+'-source-manifest.json')).read_text())
snapshot=WORK/variant
logs=WORK/(variant+'-verification');logs.mkdir(exist_ok=True)
def run(args,name):
    text=subprocess.check_output(list(map(str,args)),env=env,stderr=subprocess.STDOUT,text=True)
    (logs/name).write_text(text);return text
def field(text,pattern):
    match=re.search(pattern,text);return match.group(1) if match else None
report={'variant':variant,'verified_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'source_commit':build['source_commit'],'source_snapshot':str(snapshot),
        'source_manifest_sha256':build['source_manifest_sha256'],
        'scope':'Fresh Gradle builds; package, signatures, alignment, ABI and assets verified. No device execution in this report.',
        'apks':{}}
for kind,artifact in build['apks'].items():
    apk=Path(artifact['path']);assert sha(apk.read_bytes())==artifact['sha256']
    signature=run([SDK/'build-tools/35.0.0/apksigner','verify','--verbose','--print-certs',apk],kind+'-signature.txt')
    badging=run([SDK/'build-tools/35.0.0/aapt2','dump','badging',apk],kind+'-badging.txt')
    manifest_text=run([SDK/'build-tools/35.0.0/aapt2','dump','xmltree','--file','AndroidManifest.xml',apk],kind+'-manifest.txt')
    run([SDK/'build-tools/35.0.0/zipalign','-c','-P','16','4',apk],kind+'-alignment.txt')
    entry=dict(artifact,certificate_sha256=field(signature,r'Signer #1 certificate SHA-256 digest: ([0-9a-f]+)'),
               package=field(badging,r"package: name='([^']+)'"),
               version_name=field(badging,r"versionName='([^']*)'"),
               version_code=field(badging,r"versionCode='([^']*)'"),
               application_label=field(badging,r"application-label:'([^']*)'"),
               min_sdk=field(badging,r"(?:minSdkVersion|sdkVersion):'([^']*)'"),
               target_sdk=field(badging,r"targetSdkVersion:'([^']*)'"),
               signature_verified=True,alignment_16k_verified=True,verification_logs=str(logs))
    assert entry['certificate_sha256']
    package='com.cosmicx.solar.review' if variant=='candidate-review' else 'com.cosmicx.solar'
    assert entry['package']==(package if kind=='debug' else package+'.test')
    assert entry['min_sdk']=='26'
    if kind=='debug':
        expected_version={'candidate':('1.3.0','4'),'candidate-review':('1.3.0','4'),'baseline-defined':('1.1.0','2')}[variant]
        assert (entry['version_name'],entry['version_code'])==expected_version and entry['target_sdk']=='35'
        with zipfile.ZipFile(apk) as zipped:
            assets={}
            for path in (snapshot/'solar-android/app/src/main/assets').rglob('*'):
                if path.is_file():
                    name='assets/'+path.relative_to(snapshot/'solar-android/app/src/main/assets').as_posix()
                    expected=sha(path.read_bytes());actual=sha(zipped.read(name));assert actual==expected,name
                    assets[name]=actual
            native={}
            for abi,machine in [('arm64-v8a',183),('x86_64',62)]:
                name='lib/'+abi+'/libmx_graphics.so';data=zipped.read(name)
                assert data[:4]==b'\x7fELF' and data[4]==2 and data[5]==1
                assert struct.unpack_from('<H',data,18)[0]==machine
                native[abi]={'entry':name,'sha256':sha(data),'bytes':len(data),'elf_machine':machine}
            entry.update(assets_sha256=assets,native_libraries=native,
                         shader_matches_source=assets['assets/shaders/solar.frag']==manifest['shader_sha256'])
    else:
        assert 'com.mundox.solar.SolarInstrumentation' in manifest_text
        entry['instrumentation_target']=field(manifest_text,r':targetPackage\([^)]+\)="([^"]+)"')
        assert entry['instrumentation_target']=='com.cosmicx.solar'
    report['apks'][kind]=entry
if 'androidTest' in report['apks']:
    assert report['apks']['debug']['certificate_sha256']==report['apks']['androidTest']['certificate_sha256']
    report['application_and_tests_same_certificate']=True
if variant=='candidate-review':
    candidate=json.loads((OUT/'candidate-verification.json').read_text())
    source=json.loads((WORK/'candidate-source-manifest.json').read_text())
    assert manifest['source_sha256']==source['source_sha256']
    assert report['apks']['debug']['certificate_sha256']==candidate['apks']['debug']['certificate_sha256']
    assert report['apks']['debug']['application_label']=='COSMIC X Solar Pruebas'
    report.update(all_sources_identical_to_candidate=True,certificate_matches_candidate=True,android_test_built=False)
report['all_checks_passed']=True
(OUT/(variant+'-verification.json')).write_text(json.dumps(report,indent=2)+'\n')
(WORK/(variant+'-verification.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'variant':variant,'all_checks_passed':True,'package':report['apks']['debug']['package'],
                  'certificate_sha256':report['apks']['debug']['certificate_sha256'],
                  'shader_sha256':manifest['shader_sha256']}))
