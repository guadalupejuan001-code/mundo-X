#!/usr/bin/env python3
"""Independently decode the 8K SolarPngChecks fixture with Pillow, row by row."""
import argparse,hashlib,json,resource
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('png',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
    import numpy as np
    from PIL import Image
    with Image.open(a.png) as image:
        assert image.mode=='RGBA';w,h=image.size;assert (w,h)==(7680,4320)
        digest=hashlib.sha256()
        for y in range(h):
            row=np.asarray(image.crop((0,y,w,y+1))).reshape(-1)
            index=np.arange((h-1-y)*w*4,(h-y)*w*4,dtype=np.uint32)
            expected=((index*31)^(index>>3)^(index>>11)).astype(np.uint8)
            assert np.array_equal(row,expected),f'RGBA/orientation mismatch on row {y}'
            digest.update(row.tobytes())
    r={'status':'passed','decoder':'Pillow','pixels':w*h,'different_channels':0,
       'rgba_sha256_top_first':digest.hexdigest(),'png_bytes':a.png.stat().st_size,
       'png_sha256':hashlib.sha256(a.png.read_bytes()).hexdigest(),
       'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,'as_budget_bytes':2*1024**3}
    a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
if __name__=='__main__':main()
