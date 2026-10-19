#!/usr/bin/env python3
"""Real GLES draw-completion timings, changing frames, and explicit reference."""
from pathlib import Path
import json,time,statistics,platform
import numpy as np
from PIL import Image
from engine import build_native,QuantumScene,QuantumGraphicsEngine
from renderer import street,overview

ROOT=Path(__file__).resolve().parent

def summarize(rows):
    wall=[v['wall_draw_finish_ms'] for v in rows]
    driver=[v['driver_elapsed_ms'] for v in rows if v['driver_elapsed_ms'] is not None]
    return {'samples':rows,'wall_draw_median_ms':statistics.median(wall),
            'wall_draw_min_ms':min(wall),'wall_draw_max_ms':max(wall),
            'wall_draw_p95_ms':float(np.percentile(wall,95)),
            'driver_elapsed_median_ms':statistics.median(driver) if driver else None,
            'total_median_ms':statistics.median(v['total_ms'] for v in rows) if 'total_ms' in rows[0] else None}

def main():
    started=time.perf_counter_ns();build_native();native_ms=(time.perf_counter_ns()-started)/1e6
    report={'date_local':'2026-10-18 America/New_York','native_build_or_check_ms':native_ms,
            'platform':platform.platform(),'numpy':np.__version__,'cases':[],
            'scope':'GLES3 llvmpipe software, real draw+glFinish. No physical GPU or Android tested.',
            'frame_model':'One center ray per pixel, opaque grid boxes, direct directional shading and fog; no LOD or population thinning',
            'timing_exclusions':['Display/compositor','PNG encoding','pixel readback unless explicitly stated','native compilation after initialization'],
            'raster_pixel_equality_claimed':False}
    for count in (1024,65536,1_000_000):
        scene=QuantumScene(count,20);engine=QuantumGraphicsEngine(scene,256,144)
        report['driver']=engine.renderer.gl.info
        _,first=engine.observe(street(256,144),read=False)
        init={'objects':count,'engine_initialization_ms':engine.renderer.initialization_ms,'first_frame':first}
        case={'objects':count,'initialization':init,'scene':scene.describe(),'street_resolutions':[]}
        for width,height in ((256,144),(640,360),(1280,720)):
            engine.resize(width,height)
            cam=street(width,height)
            engine.observe(cam,t=.1,read=False,force=True)
            rows=[]
            for i in range(7):
                _,timing=engine.observe(cam,t=.2+i*.03,read=False,force=True)
                rows.append(timing)
            row={'width':width,'height':height,**summarize(rows)}
            case['street_resolutions'].append(row)
            print(json.dumps({'objects':count,'resolution':[width,height],
                              'median_draw_ms':row['wall_draw_median_ms'],
                              'median_total_ms':row['total_median_ms']}),flush=True)
        if count==1_000_000:
            cam=street(1280,720)
            frame,_=engine.observe(cam,t=.27,force=True)
            Image.fromarray(frame).save(ROOT/'millon-calle.png')
            scene.apply([('Z',0)])
            altered,gate=engine.observe(cam,t=.27)
            Image.fromarray(altered).save(ROOT/'millon-puerta-Z.png')
            case['gate_update']={'timing_with_readback':gate,
                                 'changed_pixels':int(np.sum(np.any(frame!=altered,axis=2)))}
            scene.apply([('Z',0)])
            cam=overview(1000,1280,720)
            engine.observe(cam,t=.27,read=False,force=True)
            overview_rows=[engine.observe(cam,t=.27+i*.02,read=False,force=True)[1] for i in range(7)]
            case['overview_1280x720']=summarize(overview_rows)
            frame,_=engine.observe(cam,t=.27,force=True)
            Image.fromarray(frame).save(ROOT/'millon-panorama.png')
            # More expensive references are measured only at the declared
            # resolution; all three routes use the identical geometry law.
            engine.resize(640,360);cam=street(640,360)
            engine._sync()
            preparation=engine.renderer.scene(count,scene.seed,dict(scene.compile().edits),prepare_explicit=True)
            comparison={route:[] for route in ('eye','explicit','procedural')}
            frames={}
            for route in comparison:
                engine.renderer.draw(route,cam,t=.27)
            for i in range(3):
                routes=('eye','explicit','procedural') if i%2==0 else ('procedural','explicit','eye')
                for route in routes:
                    timing,frame=engine.renderer.draw(route,cam,t=.27+i*.03,read=i==2)
                    comparison[route].append(timing)
                    if frame is not None:frames[route]=frame
                print('Completed million-object reference pair '+str(i+1),flush=True)
            deltas={}
            for route in ('explicit','procedural'):
                delta=np.max(np.abs(frames['eye'].astype(np.int16)-frames[route].astype(np.int16)),axis=2)
                deltas[route]={'different_pixels':int(np.sum(delta>0)),
                               'pixels_over_2_channel_levels':int(np.sum(delta>2)),
                               'max_channel_delta':int(delta.max()),'pixels':640*360}
            case['same_scene_comparison_640x360']={route:summarize(rows) for route,rows in comparison.items()}
            case['reference_preparation']=preparation;case['raster_differences']=deltas
        report['cases'].append(case)
        engine.close();scene.close()
        (ROOT/'benchmark.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Saved benchmark.json and real framebuffer images.',flush=True)

if __name__=='__main__':main()
