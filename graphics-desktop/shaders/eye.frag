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
#if defined(MX_MIRRORS) || defined(MX_STABLE_NORMALS)
  float normalEntry=-1e30;
#endif
  for(int axis=0;axis<3;axis++){
    if(abs(rd[axis])<1e-12){if(ro[axis]<lower[axis]||ro[axis]>upper[axis])return false;}
    else{
      float a=(lower[axis]-ro[axis])/rd[axis],b=(upper[axis]-ro[axis])/rd[axis];
      float signNormal=-1.0;
      if(a>b){float tmp=a;a=b;b=tmp;signNormal=1.0;}
#if defined(MX_MIRRORS) || defined(MX_STABLE_NORMALS)
      // At a box edge there is no unique geometric normal. Choose the first
      // axis within 1e-5 ray units; reflecting the origin must not flip shading
      // because the two slab times differ only by roundoff. Hit distance stays
      // the strict maximum, so this tie rule never expands the geometry.
      if(a>normalEntry+.00001){normalEntry=a;normal=vec3(0);normal[axis]=signNormal;}
      entry=max(entry,a);
#else
      if(a>entry){entry=a;normal=vec3(0);normal[axis]=signNormal;}
#endif
      leave=min(leave,b);
    }
  }
  return leave>=max(entry,0.0);
}
// A rule-defined column can only affect rays that cross its XZ footprint.
// Test that necessary condition before evaluating hash, edits, animation or
// Ether coefficients. The full box test still decides the hit and normal.
bool columnMayHit(vec3 ro,vec3 rd,vec2 center){
  float entry=-1e30,leave=1e30;
  for(int j=0;j<2;j++){
    int axis=j==0?0:2;
    float lower=center[j]-.38,upper=center[j]+.38;
    if(abs(rd[axis])<1e-12){if(ro[axis]<lower||ro[axis]>upper)return false;}
    else{
      float a=(lower-ro[axis])/rd[axis],b=(upper-ro[axis])/rd[axis];
      entry=max(entry,min(a,b));leave=min(leave,max(a,b));
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
// Compile distinct specializations: the primary-only shader keeps its verified
// instruction structure and avoids the extra live values of a reflected query.
#ifdef MX_MIRRORS
// One geometric query, shared by primary and reflected rays. No per-object storage.
// 1 = first hit, 0 = miss, -1 = traversal failure (never a silent miss).
int traceGrid(vec3 ro,vec3 rd,float maximum,out float resultT,out vec3 resultNormal,
              out float resultTone,out int resultId){
  float halfGrid=float(uSide)*1.1;
  // With no exceptions, the exact law is bounded by
  // (.7 + 255/128) * 1.08 = 2.9075625. Round upward; edited columns
  // retain the full bound. Nothing below either bound is discarded.
  float maxHeight=(uEditCount==0?2.908:5.18)*(1.0+abs(uQuantumStrength));
  float entry,leave;vec3 normal;
  if(!boxHit(ro,rd,vec3(-halfGrid,0,-halfGrid),vec3(halfGrid,maxHeight,halfGrid),entry,leave,normal))return 0;
  entry=max(entry,0.0);leave=min(leave,maximum);
  if(leave<entry)return 0;
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
    if(any(lessThan(cell,ivec2(0)))||any(greaterThanEqual(cell,ivec2(uSide)))||entry>leave)return 0;
    int id=cell.y*uSide+cell.x;
    if(id<uCount){
      vec2 center=(vec2(id%uSide,id/uSide)-float(uSide-1)*.5)*2.2;
      if(columnMayHit(ro,rd,center)){
        float height,tone;objectData(id,center,height,tone);
        float hitT,exitT;vec3 hitNormal;
        if(height>0.0 && boxHit(ro,rd,vec3(center.x-.38,0,center.y-.38),
                               vec3(center.x+.38,height,center.y+.38),hitT,exitT,hitNormal)){
          if(hitT>=max(entry-.001,0.0) && hitT<=min(leave,min(nextT.x,nextT.y))+.001){
            resultT=hitT;resultNormal=hitNormal;resultTone=tone;resultId=id;
            return 1;
          }
        }
      }
    }
    float crossing=min(nextT.x,nextT.y);
    if(crossing>leave)return 0;
    // Advancing both axes at an exact corner avoids revisiting zero-length cells.
    bvec2 advance=lessThanEqual(nextT,vec2(crossing));
    for(int axis=0;axis<2;axis++)if(advance[axis]){cell[axis]+=stepDir[axis];nextT[axis]+=delta[axis];}
    entry=crossing;
  }
  return -1;
}

#ifdef MX_MIRRORS
// Observer-owned optical rules; the world, edits and Ether snapshot are shared.
// Mode 1: horizontal mirror. Mode 2: two facing finite rectangular mirrors.
uniform int uMirrorMode,uMirrorBudget,uMirrorFold;
uniform float uMirrorReflectance;

float sceneHeight(){return (uEditCount==0?2.908:5.18)*(1.0+abs(uQuantumStrength));}
float mirrorExtent(){return float(uSide)*1.1+2.2;}
// Fixed optical geometry: editing a box must not resize the mirrors. All
// permitted columns fit below 6.475, including edits and Ether coupling.
float mirrorTop(){return 8.0;}

// Geometric intersections with the added surfaces, before any shading.
// kind=1 floor; kind=2 side mirror; kind=0 no surface. Normals are local.
float opticalSurface(vec3 ro,vec3 rd,out vec3 normal,out int kind){
  float best=1e30,e=mirrorExtent();kind=0;normal=vec3(0);
  if(abs(rd.y)>1e-12){
    float t=(-.025-ro.y)/rd.y;vec3 p=ro+rd*t;
    if(t>1e-5&&max(abs(p.x),abs(p.z))<=e){best=t;normal=vec3(0,1,0);kind=1;}
  }
  if(uMirrorMode==2&&abs(rd.x)>1e-12){
    for(int side=-1;side<=1;side+=2){
      float t=(float(side)*e-ro.x)/rd.x;vec3 p=ro+rd*t;
      if(t>1e-5&&t<best&&p.y>=-.025&&p.y<=mirrorTop()&&abs(p.z)<=e){
        best=t;normal=vec3(float(side),0,0);kind=2;
      }
    }
  }
  return best;
}

// A conservative YZ slab containing EVERY object and the opaque ground.
// Until entry, side reflections cannot change Y/Z, so nothing can be hit.
float firstPossibleContent(vec3 ro,vec3 rd,float maximum){
  float enter=0.0,leave=maximum;float e=mirrorExtent();
  for(int j=0;j<2;j++){
    int axis=j+1;float lower=j==0?-.025:-e,upper=j==0?sceneHeight():e;
    if(abs(rd[axis])<1e-12){if(ro[axis]<lower||ro[axis]>upper)return maximum;}
    else{
      float a=(lower-ro[axis])/rd[axis],b=(upper-ro[axis])/rd[axis];
      enter=max(enter,min(a,b));leave=min(leave,max(a,b));
    }
  }
  return leave>=enter?enter:maximum;
}

// Compose WHOLE crossings only, starting exactly at a side mirror. Stop before
// possible content, the panel edge or the optical far limit. Retain the exact
// wall coordinate and direction parity; no large unfolded X coordinate exists.
float foldEmptyCrossings(inout vec3 ro,inout vec3 rd,inout float remaining,
                        inout float travelled){
  if(uMirrorFold==0||uMirrorMode!=2||ro.x*rd.x>=0.0||abs(rd.x)<1e-12
      ||(rd.y==0.0&&rd.z==0.0))return 0.0;
  float e=mirrorExtent(),safe=firstPossibleContent(ro,rd,remaining);
  if(safe<=0.0)return 0.0;
  for(int j=0;j<2;j++){
    int axis=j+1;float lower=j==0?-.025:-e,upper=j==0?mirrorTop():e;
    if(rd[axis]!=0.0){float boundary=rd[axis]>0.0?upper:lower;safe=min(safe,(boundary-ro[axis])/rd[axis]);}
  }
  // Leave a floating-point safety margin before every non-mirror event.
  safe-=max(.001,abs(safe)*.000002);
  float period=2.0*e/abs(rd.x),crossings=floor(safe/period);
  // Above 2^23, float parity ceases to be trustworthy. Fall back to the
  // checked iterative path, rather than aliasing a reflected direction.
  if(crossings<2.0||crossings>=8388608.0)return 0.0;
  float advance=crossings*period;
  ro.yz+=rd.yz*advance;
  if(mod(crossings,2.0)==1.0){ro.x=-ro.x;rd.x=-rd.x;}
  remaining-=advance;travelled+=advance;
  return crossings;
}

vec4 reflectedSky(vec3 direction){
  return uPresentation==1?vec4(cosmicSky(direction),1):vec4(.067,.086,.075,1);
}
vec4 reflectedGround(vec3 p,vec3 direction,float distanceToEye,vec3 rd){
  if(uPresentation!=1)return vec4(.067,.086,.075,1);
  // Analytic footprint: derivatives inside a divergent reflection loop are
  // undefined. Global pixel angle also keeps tiled exports deterministic.
  vec2 grid=(p.xz+float(uSide)*1.1)/2.2;
  float footprint=max(.0002,distanceToEye*uPixelAngle/(2.2*max(abs(rd.y),.01)));
  vec2 line=1.0-smoothstep(vec2(footprint*.55),vec2(footprint*1.3),abs(fract(grid+.5)-.5));
  float thinGrid=max(line.x,line.y)*(1.0-smoothstep(.12,.45,footprint));
  vec3 ground=vec3(.027,.041,.072)+thinGrid*vec3(.055,.105,.15);
  float fogEnd=clamp(uFogDistance*.55,75.0,220.0);
  float fog=smoothstep(fogEnd*.18,fogEnd,distanceToEye);
  return vec4(mix(ground,cosmicSky(direction),fog*.95),1);
}
void mirrorResult(vec4 color,int id,float weight,float bounces,float skipped,int queries){
  if(uDebug==1)outColor=id<0?vec4(0,0,0,1):encodeId(id);
  else if(uDebug==2)outColor=encodeId(int(min(bounces,16777213.0)));
  else if(uDebug==3)outColor=encodeId(int(min(skipped,16777213.0)));
  else if(uDebug==4)outColor=encodeId(queries);
  else outColor=vec4(color.rgb*weight,1);
}
void mirrorFailure(){gl_FragDepth=0.0;outColor=vec4(1,0,1,1);}
// Intersect a monotone YZ ray with a slab; X reflection leaves it unchanged.
bool yzInterval(vec3 ro,vec3 rd,vec2 lower,vec2 upper,float maximum,
                out float entry,out float leave,out vec3 normal){
  entry=0.0;leave=maximum;normal=vec3(0);
  for(int j=0;j<2;j++){
    int axis=j+1;
    if(abs(rd[axis])<1e-12){if(ro[axis]<lower[j]||ro[axis]>upper[j])return false;}
    else{
      float a=(lower[j]-ro[axis])/rd[axis],b=(upper[j]-ro[axis])/rd[axis];
      float earliest=min(a,b);
      if(earliest>entry){entry=earliest;normal=vec3(0);normal[axis]=rd[axis]>0.0?-1.0:1.0;}
      leave=min(leave,max(a,b));
    }
  }
  return leave>=entry;
}

// The inverse image of [lower,upper] under the mirror triangle wave is TWO
// periodic intervals. Find their next occurrence inside the YZ time window.
// This checks all repetitions of this box, rather than replaying its mirrors.
float periodicXEntry(vec3 ro,vec3 rd,float lower,float upper,float entry){
  float e=mirrorExtent(),period=4.0*e,signX=sign(rd.x);
  float coordinate=signX*(ro.x+rd.x*entry),best=1e30;
  for(int family=0;family<2;family++){
    float lo=family==0?lower:2.0*e-upper;
    float hi=family==0?upper:2.0*e-lower;
    float begin=min(signX*lo,signX*hi),width=hi-lo;
    float phase=mod(coordinate-begin,period);
    float advance=phase<=width?0.0:period-phase;
    best=min(best,entry+advance/abs(rd.x));
  }
  return best;
}
float periodicState(vec3 ro,vec3 rd,float distance,bool ground,
                    out vec3 point,out vec3 direction){
  float period=2.0*mirrorExtent()/abs(rd.x),turns=floor(distance/period);
  // Ground wins an exact simultaneous floor/mirror contact.
  if(ground&&turns>0.0&&distance==turns*period)turns-=1.0;
  float parity=mod(turns,2.0)==0.0?1.0:-1.0;
  point=ro+rd*distance;
  point.x=parity*(ro.x+rd.x*(distance-turns*period));
  direction=vec3(rd.x*parity,rd.yz);
  return turns;
}

// Resolve a long optical path over a narrow group of Z rows. The candidate
// budget selects the algorithm; exceeding it falls back to the grid traversal.
// Every physical box in the traversed rows is tested. Mirrors add no copies.
bool resolvePeriodic(vec3 ro,vec3 rd,float remaining,float travelled,mat3 rot,
                     float weight,float bounces,float skipped,int queries){
  if(uMirrorFold==0||uMirrorMode!=2||ro.x*rd.x>=0.0||abs(rd.x)<1e-12)return false;
  float e=mirrorExtent(),end=remaining;bool ground=false;
  for(int j=0;j<2;j++){
    int axis=j+1;float lower=j==0?-.025:-e,upper=j==0?mirrorTop():e;
    if(rd[axis]!=0.0){
      float exit=( (rd[axis]>0.0?upper:lower)-ro[axis])/rd[axis];
      if(exit<end){end=exit;ground=j==0&&rd.y<0.0;}
    }
  }
  float crossings=end*abs(rd.x)/(2.0*e);
  if(crossings<4.0||crossings>=8388608.0)return false;
  float enter,leave;vec3 ignored;
  float halfGrid=float(uSide)*1.1;
  bool content=yzInterval(ro,rd,vec2(0,-halfGrid),vec2(sceneHeight(),halfGrid),end,enter,leave,ignored);
  int first=0,last=0;
  if(content){
    float a=ro.z+rd.z*enter,b=ro.z+rd.z*leave;
    int row0=clamp(int(floor((min(a,b)+halfGrid)/2.2)),0,uSide-1);
    int row1=clamp(int(floor((max(a,b)+halfGrid)/2.2)),0,uSide-1);
    first=row0*uSide;last=min((row1+1)*uSide,uCount);
    if(last-first>4096)return false;
  }
  float best=end,bestTone=0.0;vec3 bestNormal=vec3(0);int bestId=-1;
  for(int index=0;index<4096;index++){
    int id=first+index;if(id>=last)break;
    vec2 center=(vec2(id%uSide,id/uSide)-float(uSide-1)*.5)*2.2;
    float a,b;vec3 n;
    if(!yzInterval(ro,rd,vec2(0,center.y-.38),vec2(sceneHeight(),center.y+.38),best,a,b,n))continue;
    if(periodicXEntry(ro,rd,center.x-.38,center.x+.38,a)>b)continue;
    float height,tone;objectData(id,center,height,tone);
    if(height<=0.0||!yzInterval(ro,rd,vec2(0,center.y-.38),vec2(height,center.y+.38),best,a,b,n))continue;
    float hit=periodicXEntry(ro,rd,center.x-.38,center.x+.38,a);
    if(hit<=b&&hit<best){
      vec3 p,d;periodicState(ro,rd,hit,false,p,d);
      best=hit;bestId=id;bestTone=tone;
      bestNormal=hit>a?vec3(-sign(d.x),0,0):n;
    }
  }
  if(rd.y==0.0&&rd.z==0.0&&bestId<0){
    mirrorResult(vec4(0,0,0,1),-1,weight,bounces,skipped,queries);return true;
  }
  vec3 point,direction;
  float composed=periodicState(ro,rd,best,bestId<0&&ground,point,direction);
  if(composed>0.0&&uMirrorReflectance!=1.0)weight*=pow(uMirrorReflectance,composed);
  vec4 color;
  if(bestId>=0)color=shadeFromRay(rot*bestNormal,rot*point,bestTone,bestId,-rot*direction,travelled+best);
  else if(ground)color=reflectedGround(point,rot*direction,travelled+best,direction);
  else color=reflectedSky(rot*direction);
  mirrorResult(color,bestId,weight,bounces+composed,skipped+composed,queries);
  return true;
}

void observeMirrors(vec3 ro,vec3 rd,float remaining,float travelled,mat3 rot){
  float weight=1.0,bounces=0.0,skipped=0.0;
  bool first=true;
  gl_FragDepth=.999999;
  // The budget counts scene queries/explicit bounces, not compacted crossings.
  for(int query=0;query<65;query++){
    vec3 surfaceNormal;int kind;
    float surfaceT=opticalSurface(ro,rd,surfaceNormal,kind);
    float hitT,tone;vec3 normal;int id;
    int status=traceGrid(ro,rd,min(remaining,surfaceT),hitT,normal,tone,id);
    if(status<0){mirrorFailure();return;}
    if(status==1){
      vec3 world=rot*(ro+rd*hitT);
      if(first){vec4 clip=uVp*vec4(world,1);gl_FragDepth=(clip.z/clip.w)*.5+.5;}
      vec4 color=shadeFromRay(rot*normal,world,tone,id,-rot*rd,travelled+hitT);
      mirrorResult(color,id,weight,bounces,skipped,query+1);return;
    }
    if(kind==0||surfaceT>remaining){
      mirrorResult(reflectedSky(rot*rd),-1,weight,bounces,skipped,query+1);return;
    }
    vec3 hit=ro+rd*surfaceT;
    if(first){
      vec4 clip=uVp*vec4(rot*hit,1);gl_FragDepth=(clip.z/clip.w)*.5+.5;first=false;
    }
    travelled+=surfaceT;remaining-=surfaceT;
    if(kind==1&&uMirrorMode==2){
      mirrorResult(reflectedGround(hit,rot*rd,travelled,rd),-1,weight,bounces,skipped,query+1);return;
    }
    if(query>=uMirrorBudget){mirrorFailure();return;}
    ro=hit;
    // Snap to the rule-defined plane, avoiding repeated self-intersections.
    if(kind==2)ro.x=surfaceNormal.x*mirrorExtent();else ro.y=-.025;
    rd=reflect(rd,surfaceNormal);bounces+=1.0;weight*=uMirrorReflectance;
    // A full empty X crossing certifies this closed, non-emissive orbit.
    // Its least-radiance solution is black, also for a perfect lossless mirror.
    if(uMirrorMode==2&&query>=1&&rd.y==0.0&&rd.z==0.0){
      mirrorResult(vec4(0,0,0,1),-1,weight,bounces,skipped,query+1);return;
    }
    float composed=kind==2?foldEmptyCrossings(ro,rd,remaining,travelled):0.0;
    bounces+=composed;skipped+=composed;
    if(composed>0.0&&uMirrorReflectance!=1.0)weight*=pow(uMirrorReflectance,composed);
    if(resolvePeriodic(ro,rd,remaining,travelled,rot,weight,bounces,skipped,query+2))return;
  }
  mirrorFailure();
}
#endif

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
  observeMirrors(ro,rd,maximum,length(nearPoint-uEye),rot);
}
#else
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
  // With no exceptions, the exact law is bounded by
  // (.7 + 255/128) * 1.08 = 2.9075625. Round upward; edited columns
  // retain the full bound. Nothing below either bound is discarded.
  float maxHeight=(uEditCount==0?2.908:5.18)*(1.0+abs(uQuantumStrength));
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
      vec2 center=(vec2(id%uSide,id/uSide)-float(uSide-1)*.5)*2.2;
      if(columnMayHit(ro,rd,center)){
        float height,tone;objectData(id,center,height,tone);
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
#endif
