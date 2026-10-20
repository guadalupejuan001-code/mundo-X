"""Small ctypes EGL/GLES3 runner; no pip OpenGL bindings required.
The renderer string is always reported. Software GL is never labeled hardware.
"""
import os
import ctypes as C
import ctypes.util
import numpy as np

U=C.c_uint; I=C.c_int; F=C.c_float; P=C.c_void_p

class GL:
    def __init__(self):
        os.environ.setdefault('EGL_PLATFORM','surfaceless')
        self.egl=C.CDLL(ctypes.util.find_library('EGL'))
        e=self.egl
        signatures={
            'eglGetDisplay':(P,[P]),'eglInitialize':(U,[P,C.POINTER(I),C.POINTER(I)]),
            'eglBindAPI':(U,[U]),'eglChooseConfig':(U,[P,C.POINTER(I),C.POINTER(P),I,C.POINTER(I)]),
            'eglCreatePbufferSurface':(P,[P,P,C.POINTER(I)]),
            'eglCreateContext':(P,[P,P,P,C.POINTER(I)]),
            'eglMakeCurrent':(U,[P,P,P,P]),'eglGetProcAddress':(P,[C.c_char_p]),
            'eglDestroyContext':(U,[P,P]),'eglDestroySurface':(U,[P,P]),'eglTerminate':(U,[P])}
        for name,(ret,args) in signatures.items():
            fn=getattr(e,name);fn.restype=ret;fn.argtypes=args
        self.display=e.eglGetDisplay(None); major=I();minor=I()
        assert e.eglInitialize(self.display,C.byref(major),C.byref(minor)),'EGL initialize'
        assert e.eglBindAPI(0x30A0),'EGL ES API'
        attrs=(I*13)(0x3033,1,0x3040,0x40,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038)
        conf=P();num=I()
        assert e.eglChooseConfig(self.display,attrs,C.byref(conf),1,C.byref(num)) and num.value
        self.surface=e.eglCreatePbufferSurface(self.display,conf,(I*5)(0x3057,16,0x3056,16,0x3038))
        self.context=e.eglCreateContext(self.display,conf,None,(I*3)(0x3098,3,0x3038))
        assert self.context and self.surface and e.eglMakeCurrent(self.display,self.surface,self.surface,self.context)
        self.functions={}
        specs={
          'glGetString':(C.c_char_p,[U]),'glGetError':(U,[]),'glGetIntegerv':(None,[U,C.POINTER(I)]),
          'glCreateShader':(U,[U]),'glShaderSource':(None,[U,I,C.POINTER(C.c_char_p),C.POINTER(I)]),
          'glCompileShader':(None,[U]),'glGetShaderiv':(None,[U,U,C.POINTER(I)]),
          'glGetShaderInfoLog':(None,[U,I,C.POINTER(I),P]),'glDeleteShader':(None,[U]),
          'glCreateProgram':(U,[]),'glAttachShader':(None,[U,U]),'glLinkProgram':(None,[U]),
          'glGetProgramiv':(None,[U,U,C.POINTER(I)]),'glGetProgramInfoLog':(None,[U,I,C.POINTER(I),P]),
          'glUseProgram':(None,[U]),'glDeleteProgram':(None,[U]),'glGetUniformLocation':(I,[U,C.c_char_p]),
          'glUniform1i':(None,[I,I]),'glUniform1f':(None,[I,F]),'glUniform1fv':(None,[I,I,P]),
          'glUniform2f':(None,[I,F,F]),'glUniform3f':(None,[I,F,F,F]),
          'glUniform4ui':(None,[I,U,U,U,U]),'glUniformMatrix4fv':(None,[I,I,U,P]),
          'glGenVertexArrays':(None,[I,C.POINTER(U)]),'glBindVertexArray':(None,[U]),
          'glGenBuffers':(None,[I,C.POINTER(U)]),'glBindBuffer':(None,[U,U]),
          'glBufferData':(None,[U,C.c_ssize_t,P,U]),'glEnableVertexAttribArray':(None,[U]),
          'glDisableVertexAttribArray':(None,[U]),
          'glVertexAttribPointer':(None,[U,I,U,U,I,P]),'glVertexAttribDivisor':(None,[U,U]),
          'glGenTextures':(None,[I,C.POINTER(U)]),'glBindTexture':(None,[U,U]),
          'glActiveTexture':(None,[U]),'glTexParameteri':(None,[U,U,I]),
          'glTexImage2D':(None,[U,I,I,I,I,I,U,U,P]),
          'glGenFramebuffers':(None,[I,C.POINTER(U)]),'glBindFramebuffer':(None,[U,U]),
          'glFramebufferTexture2D':(None,[U,U,U,U,I]),'glCheckFramebufferStatus':(U,[U]),
          'glGenRenderbuffers':(None,[I,C.POINTER(U)]),'glBindRenderbuffer':(None,[U,U]),
          'glRenderbufferStorage':(None,[U,U,I,I]),'glFramebufferRenderbuffer':(None,[U,U,U,U]),
          'glViewport':(None,[I,I,I,I]),'glEnable':(None,[U]),'glDisable':(None,[U]),
          'glScissor':(None,[I,I,I,I]),
          'glDepthFunc':(None,[U]),'glDepthMask':(None,[U]),'glClearColor':(None,[F,F,F,F]),
          'glClear':(None,[U]),'glDrawArrays':(None,[U,I,I]),'glDrawArraysInstanced':(None,[U,I,I,I]),
          'glFinish':(None,[]),'glReadPixels':(None,[I,I,I,I,U,U,P]),
          'glGenQueries':(None,[I,C.POINTER(U)]),'glBeginQuery':(None,[U,U]),'glEndQuery':(None,[U]),
          'glGetQueryiv':(None,[U,U,C.POINTER(I)]),
          'glGetQueryObjectui64vEXT':(None,[U,U,C.POINTER(C.c_uint64)])}
        for name,(ret,args) in specs.items():
            address=e.eglGetProcAddress(name.encode())
            if not address:
                # Timing extensions are optional; draw+glFinish still works.
                if name=='glGetQueryObjectui64vEXT':continue
                raise RuntimeError('Missing '+name)
            setattr(self,name,C.CFUNCTYPE(ret,*args)(address))
        self.info={name:self.glGetString(key).decode() for name,key in
                   [('vendor',0x1F00),('renderer',0x1F01),('version',0x1F02),('glsl',0x8B8C)]}
        self.info['hardware_gpu_available']=not any(x in self.info['renderer'].lower() for x in ('llvmpipe','softpipe','swiftshader'))
        extensions=self.glGetString(0x1F03).decode().split()
        self.timer='GL_EXT_disjoint_timer_query' in extensions and hasattr(self,'glGetQueryObjectui64vEXT')
        if self.timer:
            bits=I();self.glGetQueryiv(0x88BF,0x8864,C.byref(bits));self.timer=bits.value>0
        self.info['timer_query_supported']=self.timer

    def check(self,where):
        error=self.glGetError()
        if error:raise RuntimeError(f'{where}: GL error {error:#x}')

    def object(self,name):
        v=U();getattr(self,name)(1,C.byref(v));return v.value

    def program(self,vertex,fragment):
        shaders=[]
        for kind,source in ((0x8B31,vertex),(0x8B30,fragment)):
            shader=self.glCreateShader(kind);data=C.c_char_p(source.encode())
            self.glShaderSource(shader,1,C.byref(data),None);self.glCompileShader(shader)
            ok=I();self.glGetShaderiv(shader,0x8B81,C.byref(ok))
            if not ok.value:
                buf=C.create_string_buffer(65536);self.glGetShaderInfoLog(shader,len(buf),None,buf)
                raise RuntimeError(buf.value.decode())
            shaders.append(shader)
        program=self.glCreateProgram()
        for s in shaders:self.glAttachShader(program,s)
        self.glLinkProgram(program);ok=I();self.glGetProgramiv(program,0x8B82,C.byref(ok))
        if not ok.value:
            buf=C.create_string_buffer(65536);self.glGetProgramInfoLog(program,len(buf),None,buf)
            raise RuntimeError(buf.value.decode())
        for s in shaders:self.glDeleteShader(s)
        return program

    def close(self):
        e=self.egl;e.eglMakeCurrent(self.display,None,None,None)
        e.eglDestroyContext(self.display,self.context);e.eglDestroySurface(self.display,self.surface);e.eglTerminate(self.display)
