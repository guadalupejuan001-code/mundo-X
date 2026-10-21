#!/usr/bin/env python3
"""File inventory and observed test linkage, explicitly not line/branch coverage."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference',default='HEAD')
p.add_argument('--build-dir',type=Path,required=True)
p.add_argument('--ctest-inventory',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git',*args],cwd=root)
reference=git('rev-parse',a.reference).decode().strip()
files=git('ls-tree','-r','--name-only',reference).decode().splitlines()
tests=json.loads(a.ctest_inventory.read_text())['tests']
targets=defaultdict(list)
for t in tests:
    if t.get('command'):targets[Path(t['command'][0]).name].append(t['name'])
linked=defaultdict(set)
for dep in a.build_dir.rglob('*.o.d'):
    target=next((x[:-4] for x in dep.parts if x.endswith('.dir')),None)
    names=targets.get(target,[])
    if target=='mundo_x_graphics_core':names=['infinite_bridge','core_60','core_1000']
    for token in dep.read_text().replace('\\\n',' ').split():
        try:local=str(Path(token).resolve().relative_to(root))
        except ValueError:continue
        if local in files:linked[local].update(names)

manual=set('''native/include/mundo_x/ether_core.hpp
native/include/mundo_x/infinite_core.hpp
native/include/mundo_x/infinite_session.hpp
native/include/mundo_x/infinite_address.hpp
native/include/mundo_x/infinite_program.hpp
native/include/mundo_x/infinite_bridge.h
native/include/mundo_x/infinite_bridge.hpp
native/include/mundo_x/pauli_observer.hpp
native/include/mundo_x/engine_session.hpp
native/include/mundo_x/compact_core.hpp
native/src/infinite_bridge.cpp
native/src/graphics_core_bridge.cpp
native/tests/ether_fusion.cpp
native/tests/ether_batch.cpp
native/tests/ether_stress.cpp
graphics-android/app/src/main/cpp/graphics_jni.cpp
graphics-android/app/src/main/java/com/mundox/graphics/NativeCore.java
graphics-android/app/src/main/java/com/mundox/graphics/EtherCoreNative.java
graphics-android/app/src/main/java/com/mundox/graphics/InfiniteProgram.java
graphics-android/app/src/main/java/com/mundox/graphics/QuantumRenderer.java
graphics-android/app/src/main/java/com/mundox/graphics/CosmicProject.java
graphics-android/app/src/main/java/com/mundox/graphics/RenderResolution.java
graphics-android/app/src/main/assets/shaders/eye.frag
graphics-desktop/engine.py
graphics-desktop/renderer.py
graphics-desktop/infinite_runtime.py
graphics-desktop/core_runtime.py
graphics-desktop/infinite_format.py
graphics-desktop/project.py
graphics-desktop/shaders/eye.frag
solar-android/app/src/main/java/com/mundox/solar/QuantumSolarBridge.java
solar-android/app/src/main/java/com/mundox/solar/SolarVisualEther.java
solar-android/app/src/main/java/com/mundox/solar/SolarRenderer.java
solar-android/app/src/main/java/com/mundox/solar/SolarData.java
solar-android/app/src/main/java/com/mundox/solar/SolarFlightPhysics.java
solar-android/app/src/main/assets/shaders/solar.frag
pc-android/native/quantum_device.hpp
pc-android/native/pc_machine.hpp
pc-android/native/x86_cpu.hpp
pc-android/native/sparse_ram.hpp
pc-android/app/src/main/cpp/pc_jni.cpp
pc-android/app/src/main/java/com/cosmicx/pc/MainActivity.java
cosmic-android/app/src/main/java/com/mundox/cosmic/CosmicActivity.java
app/src/main/cpp/native-lib.cpp
app/src/main/cpp/micro-jni.cpp
src/quantum_core.py
src/bridge_x.py
src/transformer_x.py
CMakeLists.txt
pc-android/CMakeLists.txt
graphics-android/app/src/main/cpp/CMakeLists.txt
tools/cosmic_source_provenance.py'''.splitlines())
java={'NativeCore.java','InfiniteProgram.java','EtherCoreNative.java','QuantumSolarBridge.java','EtherFusionJni.java'}
solar={'SolarData.java','SolarEnvironment.java','SolarFlightPhysics.java','SolarDataChecks.java','SolarVisualEther.java','SolarVisualEtherChecks.java'}
python_exec={'engine.py','renderer.py','egl_runtime.py','core_runtime.py','infinite_runtime.py','infinite_format.py','project.py','validate.py','validate_contract.py','validate_projects.py','validate_infinite.py'}
out=[]
for name in files:
    data=git('show',reference+':'+name);text=data.decode('utf-8','replace')
    evidence=[]
    if linked[name]:evidence.append({'kind':'dependency_of_executed_native_target','tests':sorted(linked[name])})
    if name.startswith(('graphics-android/','solar-android/','tests/java/')) and Path(name).name in java:evidence.append({'kind':'host_JVM_JNI','result':'jni-after/results.json'})
    if name.startswith('solar-android/') and Path(name).name in solar:evidence.append({'kind':'host_Java','result':'solar-after.log / solar-science.log'})
    if name.startswith('graphics-desktop/') and (Path(name).name in python_exec or '/shaders/' in name):evidence.append({'kind':'host_Mesa_GLES','result':'desktop-*.json; shader use depends on route'})
    if name.startswith('tools/ether_') or name in ('tools/validate_ether_jni.py','tools/cosmic_source_provenance.py'):
        evidence.append({'kind':'audit_host_harness','result':'same-named campaign outputs or coverage.json; not branch coverage'})
    deps=[{'line':i,'text':line.strip()} for i,line in enumerate(text.splitlines(),1)
          if re.match(r'\s*(#\s*include|from\s+\S+\s+import|import\s+|.*sourceSets|.*include_directories|.*target_link_libraries)',line)]
    symbols=sorted(set(re.findall(r'\b(?:EtherCore|InfiniteCore|InfiniteSession|CompactCore|DenseCore|DenseReference|FactorizedCore|EngineSession|SolarVisualEther)\b',text)))
    out.append({'path':name,'git_blob':git('rev-parse',reference+':'+name).decode().strip(),
        'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'lines':len(text.splitlines()),
        'scope':name.split('/')[0],'review':'targeted_manual_paths' if name in manual or name.startswith('tools/ether_') else 'inventory_and_dependency_scan',
        'execution_evidence':evidence,'symbols':symbols,'direct_dependency_lines':deps,
        'pending':'No line/branch coverage measured; unexecuted platform paths remain pending.'})
report={'reference':reference,'baseline':'7b028db2344912b2ab86a0affa2a1fe56180922b',
    'meaning':'All tracked files inventoried. Targeted manual review does not mean every line read. Native linkage records compilation dependencies of executed targets, not that every function/branch executed. No Android UI or physical-phone execution in this audit.',
    'counts':{'files':len(out),'targeted_manual':sum(x['review']=='targeted_manual_paths' for x in out),
              'with_execution_link':sum(bool(x['execution_evidence']) for x in out)},
    'by_scope':dict(Counter(x['scope'] for x in out)),'files':out}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(report['counts']))
