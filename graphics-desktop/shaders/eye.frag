#version 300 es
precision highp float;
precision highp int;
// @COMMON@
uniform mat4 uInvVp;
uniform vec2 uResolution;
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
void main(){
  outColor=uDebug==1?vec4(0,0,0,1):vec4(.067,.086,.075,1);
  gl_FragDepth=1.0;
  vec2 ndc=2.0*gl_FragCoord.xy/uResolution-1.0;
  // Direct camera rays avoid cancellation in a far-plane matrix unprojection.
  vec3 direction=normalize(uCameraForward+ndc.x*uAspect*uTanHalfFov*uCameraRight+ndc.y*uTanHalfFov*uCameraUp);
  float viewCosine=dot(direction,uCameraForward);
  vec3 nearPoint=uEye+direction*(uNear/viewCosine);
  mat3 rot=rotation(),inverseRot=transpose(rot);
  vec3 ro=inverseRot*nearPoint,rd=inverseRot*direction;
  float maximum=(uFar-uNear)/viewCosine;
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
