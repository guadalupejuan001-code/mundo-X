"""Verify existing-state integration, non-destructive queries and causal coupling."""
from pathlib import Path
from itertools import product
import json,hashlib
import numpy as np
from core_runtime import Core
from engine import build_native,QuantumScene,QuantumGraphicsEngine,street

ROOT=Path(__file__).resolve().parent

def state(core):
    return (core.revision,tuple(core.generators()))

def run():
    build_native()
    report={'scope':'COSMIC X Engine shared native session and Mundo X graphic observer',
            'checks':{},'new_performance_benchmark':False}
    checks=report['checks']
    # Full signed Pauli basis for three qubits, including Y/phase-sensitive terms.
    queries=[''.join(p) for p in product('IXYZ',repeat=3)]
    queries += ['-'+p for p in queries.copy()]
    cases=[[],[('H',0)],[('CNOT',0,1)],[('S',0)],
           [('H',2),('CZ',1,2)],[('X',1),('Z',2),('S',2)]]
    compact=Core(3);dense=Core(3,'dense');maximum=0.;compared=0
    try:
        for batch in cases:
            compact.apply(batch);dense.apply(batch)
            before=state(compact)
            a=compact.query(queries);b=dense.query(queries)
            maximum=max(maximum,float(np.max(np.abs(a-b))));compared+=len(a)
            assert state(compact)==before,'Readout changed the quantum state'
        assert maximum<1e-12,maximum
    finally:
        compact.close();dense.close()
    checks['signed_pauli_reference']={'comparisons':compared,'maximum_absolute_error':maximum,
                                      'state_unchanged':True}

    core=Core(2);core.apply([('H',0),('CNOT',0,1)])
    before=state(core)
    observables=['XX','YY','ZZ','ZI','IZ','XI','IX']
    scene=QuantumScene.from_core(core,count=1_000_000,observables=observables)
    assert scene._core is core and state(core)==before
    assert scene.compile().coefficients.tolist()==[1,-1,1,0,0,0,0]
    checks['existing_core_not_reset_cloned_or_measured']=True
    engine=QuantumGraphicsEngine(scene,192,108);cam=street(192,108)
    try:
        first,_=engine.observe(cam,t=0.)
        assert state(core)==before
        queries_before=scene.observable_batches
        engine.observe(street(192,108,.2),read=False)
        assert scene.observable_batches==queries_before and state(core)==before
        core.apply([('Z',0)])  # Application changes the SAME external core.
        after_gate=state(core)
        second,_=engine.observe(cam,t=0.)
        assert state(core)==after_gate
        assert scene.compile().coefficients.tolist()==[-1,1,1,0,0,0,0]
        changed=int(np.any(first!=second,axis=2).sum());assert changed>0
        checks['external_gate_changes_image']={'changed_pixels':changed,'resolution':[192,108]}
        scene.edit_many({0:6,12345:3,999999:2})
        engine.observe(cam,read=False)
        assert state(core)==after_gate
        checks['camera_and_source_edits_preserve_quantum_state']=True
        # A last channel beyond 64 is retained and uploaded, not silently pruned.
        many=['ZI']*72+['-ZZ']
        scene.set_observables(many)
        engine.observe(cam,read=False)
        assert len(engine.renderer.quantum)==73 and engine.renderer.quantum[-1]==-1
        checks['all_73_requested_channels_uploaded']=True
        stable=scene.compile()
        try:scene.set_observables(['XX','T!'])
        except ValueError:pass
        else:raise AssertionError('Malformed observable accepted')
        assert scene.compile() is stable and state(core)==after_gate
        checks['invalid_observable_batch_preserves_previous_program']=True
        report['driver']=engine.renderer.gl.info
    finally:
        engine.close();scene.close()
    assert core.query(['ZZ']).tolist()==[1]
    checks['closing_scene_preserves_borrowed_core']=True

    # Causal control: with zero coupling, a gate changes observables but not pixels.
    neutral=QuantumScene.from_core(core,count=1_000_000,coupling=0.,observables=['XX','YY'])
    renderer=QuantumGraphicsEngine(neutral,192,108)
    try:
        before_image,_=renderer.observe(cam,t=0.)
        q_before=neutral.compile().coefficients.copy()
        core.apply([('Z',0)])
        after_image,_=renderer.observe(cam,t=0.)
        assert not np.array_equal(q_before,neutral.compile().coefficients)
        delta=int(np.any(before_image!=after_image,axis=2).sum());assert delta==0
        checks['zero_coupling_control']={'changed_pixels':delta,'quantum_observables_changed':True}
    finally:
        renderer.close();neutral.close();core.close()
    for entry in json.loads((ROOT/'native/PROVENANCE.json').read_text())['files']:
        data=(ROOT.parent/entry['path']).read_bytes()
        assert hashlib.sha256(data).hexdigest()==entry['sha256']
    checks['canonical_native_headers_match_provenance']=True
    report['status']='passed'
    (ROOT/'contract-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':run()
