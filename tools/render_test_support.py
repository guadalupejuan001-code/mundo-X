"""Test-only adapter for the pre-page GPU edit layout, never used by the app."""
import ctypes as C
import math

def legacy_edit_data(edits, maximum=4096):
    import numpy as np
    width=min(4096,maximum,max(1,len(edits)));height=max(1,math.ceil(len(edits)/width))
    data=np.full((height,width,2),-1,dtype=np.int32)
    for j,(id,state) in enumerate(sorted(edits.items())):data.reshape(-1,2)[j]=(id,state)
    return data

def legacy_edit_texture(renderer, edits, texture):
    from egl_runtime import I
    g=renderer.gl;maximum=I();g.glGetIntegerv(0x0D33,C.byref(maximum))
    data=legacy_edit_data(edits,maximum.value);height,width,_=data.shape
    g.glActiveTexture(0x84C0);g.glBindTexture(0x0DE1,texture)
    g.glTexImage2D(0x0DE1,0,0x823B,width,height,0,0x8228,0x1404,data.ctypes.data)
    for param,val in [(0x2801,0x2600),(0x2800,0x2600),(0x2802,0x812F),(0x2803,0x812F)]:
        g.glTexParameteri(0x0DE1,param,val)
    g.check('legacy edit fixture')
    return data.nbytes
