#version 300 es
precision highp float;
precision highp int;
// @COMMON@
uniform mat4 uInvVp;
uniform vec2 uResolution;
// Global native pixel origin for streamed image tiles. Zero for a full framebuffer.
uniform vec2 uPixelOrigin;
uniform vec3 uCameraForward,uCameraRight,uCameraUp;
uniform float uTanHalfFov,uAspect,uNear,uFar;
out vec4 outColor;

bool boxHit(vec3 ro,vec3 rd,vec3 lower,vec3 upper,out float entry,out float leave,out vec3 normal){
  entry=-1e30;leave=1e30;normal=vec3(0);
  for(int axis=0;axis<3;axis++){
    if(abs(rd[axis])<1e-12){if(ro[axis]<lower[axis]||ro[axis]>upper[axis])return false;}
    else{
      float a=(lower[axis]-ro[axis])/rd[axis],b=(upper[axis]-ro[axis])/rd[axis];
      float signNormal=-1.0;
      if(a>b){float tmp=a;a=b;b=tmp;signNormal=1.0;}
      if(a>entry){entry=a;normal=vec3(0);normal[axis]=signNormal;}
      leave=min(leave,b);
    }
  }
  return leave>=max(entry,0.0);
}
vec3 cosmicSky(vec3 direction){
  float horizon=pow(1.0-clamp(abs(direction.y),0.0,1.0),5.0);
  vec3 sky=mix(vec3(.025,.041,.085),vec3(.075,.065,.17),smoothstep(-.1,.7,direction.y));
  sky+=horizon*vec3(.105,.135,.205);
  vec3 glowDirection=normalize(vec3(-.65,.24,-.7));
  sky+=pow(max(dot(direction,glowDirection),0.0),20.0)*vec3(.08,.045,.14);
  return sky;
}
// The ground is a presentation backdrop, not an additional object or pick target.
// Debug IDs always retain the original black background and exact object hits.
vec4 cosmicBackdrop(vec3 direction,vec3 ro,vec3 rd,float maximum){
  vec3 sky=cosmicSky(direction);
  // Evaluate the footprint for every lane before any ray-dependent return.
  // Derivatives after divergent returns are undefined and can change between
  // a complete framebuffer and a tile, even with aligned global pixel quads.
  // The finite denominator changes only rays that cannot hit the ground.
  float groundT=(-.025-ro.y)/min(rd.y,-1e-6);
  vec3 point=ro+rd*groundT;
  vec2 grid=(point.xz+float(uSide)*1.1)/2.2;
  vec2 aa=max(fwidth(grid),vec2(.0002));
  float extent=float(uSide)*1.1+2.2;
  if(rd.y>=-1e-6||groundT<0.0||groundT>maximum
      ||max(abs(point.x),abs(point.z))>extent)return vec4(sky,1.0);
  vec2 line=1.0-smoothstep(aa*.55,aa*1.3,abs(fract(grid+.5)-.5));
  float thinGrid=max(line.x,line.y);
  // Fade fine lines before they become subpixel, preserving a calm distant field.
  thinGrid*=1.0-smoothstep(.12,.45,max(aa.x,aa.y));
  vec3 ground=vec3(.027,.041,.072)+thinGrid*vec3(.055,.105,.15);
  float distanceToEye=groundT+uNear;
  float fogEnd=clamp(uFogDistance*.55,75.0,220.0);
  float fog=smoothstep(fogEnd*.18,fogEnd,distanceToEye);
  return vec4(mix(ground,sky,fog*.95),1.0);
}
void main(){
  outColor=uDebug==1?vec4(0,0,0,1):vec4(.067,.086,.075,1);
  gl_FragDepth=1.0;
  vec2 ndc=2.0*(gl_FragCoord.xy+uPixelOrigin)/uResolution-1.0;
  // Direct camera rays avoid cancellation in a far-plane matrix unprojection.
  vec3 direction=normalize(uCameraForward+ndc.x*uAspect*uTanHalfFov*uCameraRight+ndc.y*uTanHalfFov*uCameraUp);
  float viewCosine=dot(direction,uCameraForward);
  vec3 nearPoint=uEye+direction*(uNear/viewCosine);
  mat3 rot=rotation(),inverseRot=transpose(rot);
  vec3 ro=inverseRot*nearPoint,rd=inverseRot*direction;
  float maximum=(uFar-uNear)/viewCosine;
  if(uPresentation==1&&uDebug!=1){
    outColor=cosmicBackdrop(direction,ro,rd,maximum);
    // GL_LESS against the clear depth must allow the cosmetic background through.
    gl_FragDepth=.999999;
  }
  float halfGrid=float(uSide)*1.1;
  float maxHeight=5.18*(1.0+abs(uQuantumStrength));
  float entry,leave;vec3 normal;
  if(!boxHit(ro,rd,vec3(-halfGrid,0,-halfGrid),vec3(halfGrid,maxHeight,halfGrid),entry,leave,normal))return;
  entry=max(entry,0.0);leave=min(leave,maximum);
  if(leave<entry)return;
  vec2 p=(ro+rd*(entry+.0001)).xz;
  ivec2 cell=clamp(ivec2(floor((p+halfGrid)/2.2)),ivec2(0),ivec2(uSide-1));
  ivec2 stepDir=ivec2(sign(rd.xz));
  vec2 delta=vec2(1e30),nextT=vec2(1e30);
  for(int axis=0;axis<2;axis++){
    float d=rd[axis==0?0:2];
    if(abs(d)>1e-12){
      delta[axis]=2.2/abs(d);
      float boundary=-halfGrid+2.2*float(cell[axis]+(stepDir[axis]>0?1:0));
      nextT[axis]=(boundary-ro[axis==0?0:2])/d;
    }
  }
  // Geometric bound: at most 2*side-1 cells in a side-by-side 2D grid.
  // This is not a quality budget; no valid ray may silently terminate early.
  for(int iteration=0;iteration<2049;iteration++){
    if(any(lessThan(cell,ivec2(0)))||any(greaterThanEqual(cell,ivec2(uSide)))||entry>leave)return;
    int id=cell.y*uSide+cell.x;
    if(id<uCount){
      vec2 center;float height,tone;objectData(id,center,height,tone);
      float hitT,exitT;vec3 hitNormal;
      if(height>0.0 && boxHit(ro,rd,vec3(center.x-.38,0,center.y-.38),
                             vec3(center.x+.38,height,center.y+.38),hitT,exitT,hitNormal)){
        if(hitT>=max(entry-.001,0.0) && hitT<=min(leave,min(nextT.x,nextT.y))+.001){
          vec3 world=rot*(ro+rd*hitT);
          vec4 clip=uVp*vec4(world,1);
          gl_FragDepth=(clip.z/clip.w)*.5+.5;
          outColor=shade(rot*hitNormal,world,tone,id);
          return;
        }
      }
    }
    float crossing=min(nextT.x,nextT.y);
    if(crossing>leave)return;
    // Advancing both axes at an exact corner avoids revisiting zero-length cells.
    bvec2 advance=lessThanEqual(nextT,vec2(crossing));
    for(int axis=0;axis<2;axis++)if(advance[axis]){cell[axis]+=stepDir[axis];nextT[axis]+=delta[axis];}
    entry=crossing;
  }
  // Visible failure sentinel; the runner checks that traversal never exhausts.
  // It must pass GL_LESS against a cleared depth buffer to remain detectable.
  gl_FragDepth=0.0;
  outColor=vec4(1,0,1,1);
}
