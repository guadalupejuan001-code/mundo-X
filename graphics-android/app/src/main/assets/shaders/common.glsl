// ObjectSceneModel rules, with an optional explicitly bounded quantum control.
uniform int uCount,uSide,uEditCount,uSelected,uDebug,uPresentation;
uniform highp uvec4 uSeedBytes;
uniform highp isampler2D uEdits;
uniform float uTime,uFogDistance,uQuantumStrength,uPixelAngle;
uniform highp sampler2D uQuantum;
uniform int uQuantumCount;
uniform vec3 uEye;
uniform mat4 uVp;

highp uvec4 mxMulBytes(highp uvec4 a,highp uvec4 b){
  highp uvec4 r=uvec4(0u);
  for(int i=0;i<4;i++){highp uint carry=0u;
    for(int j=0;j<4;j++){if(i+j>=4)break;
      highp uint t=r[i+j]+a[i]*b[j]+carry;r[i+j]=t&255u;carry=t>>8u;}}
  return r;
}
highp uvec4 mxHashBytes(highp int id,highp uvec4 seed){
  highp uvec4 x=uvec4(id%256,(id/256)%256,(id/65536)%256,(id/16777216)%256);
  x=x^seed^uvec4(185u,121u,55u,158u);
  x=x^uvec4(x.z,x.w,0u,0u);x=mxMulBytes(x,uvec4(45u,53u,235u,127u));
  x=x^uvec4(((x.y>>7u)|(x.z<<1u))&255u,((x.z>>7u)|(x.w<<1u))&255u,x.w>>7u,0u);
  x=mxMulBytes(x,uvec4(139u,166u,108u,132u));return x^uvec4(x.z,x.w,0u,0u);
}
int editState(int id){
  int lo=0,hi=uEditCount-1,width=textureSize(uEdits,0).x;
  for(int j=0;j<21;j++){if(lo>hi)break;int mid=(lo+hi)/2;
    ivec2 item=texelFetch(uEdits,ivec2(mid%width,mid/width),0).rg;
    if(item.x==id)return item.y;if(item.x<id)lo=mid+1;else hi=mid-1;}
  return 0;
}
void objectData(int id,out vec2 center,out float height,out float tone){
  uvec4 h=mxHashBytes(id,uSeedBytes);
  float c=float(uSide-1)*.5;
  center=(vec2(id%uSide,id/uSide)-c)*2.2;
  int state=uEditCount>0?editState(id):0;
  height=state==7?0.0:.7+float(h.x)/128.0+float(state)*.35;
  height*=1.0+.08*sin(uTime*.8+center.x*.07+center.y*.06);
  int q=id%uQuantumCount,qwidth=textureSize(uQuantum,0).x;
  height*=1.0+uQuantumStrength*texelFetch(uQuantum,ivec2(q%qwidth,q/qwidth),0).r;
  tone=float(h.y)/255.0;
}
mat3 rotation(){float c=cos(uTime*.12),s=sin(uTime*.12);return mat3(c,0,-s,0,1,0,s,0,c);}
vec4 encodeId(int id){int n=id+1;return vec4(float(n&255),float((n>>8)&255),float((n>>16)&255),255.0)/255.0;}
// The COSMIC material is an opt-in presentation of the same hit and normal.
// It never changes objectData, traversal, encoded IDs or quantum expectations.
vec3 cosmicAccent(float tone){
  vec3 cyan=vec3(.20,.83,.94),violet=vec3(.62,.48,.95),gold=vec3(1.0,.72,.33);
  return tone<.48?mix(cyan,violet,tone/.48):mix(violet,gold,(tone-.48)/.52);
}
vec4 cosmicShade(vec3 normal,vec3 world,float tone,int id){
  vec3 n=normalize(normal),view=normalize(uEye-world);
  vec3 key=normalize(vec3(-.55,.82,.22)),fill=normalize(vec3(.8,.35,-.3));
  vec3 accent=cosmicAccent(tone);
  if(id==uSelected)accent=vec3(1.0,.86,.46);
  vec3 albedo=mix(vec3(.105,.16,.25),accent,.52);
  float diffuse=.42+.58*max(dot(n,key),0.0);
  vec3 color=albedo*diffuse+accent*max(dot(n,fill),0.0)*.12;
  color+=vec3(.60,.82,1.0)*pow(max(dot(reflect(-key,n),view),0.0),32.0)*.32;
  color+=accent*pow(1.0-max(dot(n,view),0.0),3.0)*.16;
  mat3 inverseRot=transpose(rotation());
  vec3 local=inverseRot*world,localNormal=inverseRot*n;
  vec2 center=(vec2(id%uSide,id/uSide)-float(uSide-1)*.5)*2.2;
  vec2 localXZ=local.xz-center;
  float footprint=max(.002,distance(uEye,world)*uPixelAngle);
  if(localNormal.y>.5){
    float edgeDistance=min(.38-abs(localXZ.x),.38-abs(localXZ.y));
    float edge=1.0-smoothstep(.008,.018+footprint*.7,edgeDistance);
    color=mix(color,accent*.83+vec3(.11),edge*.62);
  }else{
    float across=abs(localNormal.x)>.5?localXZ.y:localXZ.x;
    float column=abs(fract((across+.38)*5.0)-.5);
    float row=abs(fract(local.y*3.1)-.5);
    float aa=min(.20,footprint*3.1);
    float windows=(1.0-smoothstep(.16,.20+aa,column))*(1.0-smoothstep(.12,.20+aa,row));
    float occupied=step(.27,fract(tone*17.0+floor(local.y*3.1)*.381));
    // Subpixel facade detail fades continuously instead of shimmering at distance.
    float detail=1.0-smoothstep(.035,.15,footprint);
    color+=windows*occupied*detail*(accent*.38+vec3(.075,.07,.04));
    float edge=1.0-smoothstep(.008,.019+footprint*.7,.38-abs(across));
    color+=edge*accent*.15;
    color*=.80+.20*smoothstep(0.0,.55,local.y);
  }
  if(id==uSelected)color=mix(color,vec3(1.0,.86,.46),.35);
  float fogEnd=clamp(uFogDistance*.55,75.0,220.0);
  float fog=smoothstep(fogEnd*.24,fogEnd,distance(uEye,world))*.84;
  return vec4(clamp(mix(color,vec3(.105,.145,.235),fog),0.0,.97),1.0);
}
vec4 shade(vec3 normal,vec3 world,float tone,int id){
  if(uDebug==1)return encodeId(id);
  if(uPresentation==1)return cosmicShade(normal,world,tone,id);
  vec3 bone=vec3(.925,.902,.839),lime=vec3(.839,.929,.404);
  vec3 coral=vec3(.875,.506,.424),ink=vec3(.067,.086,.075);
  vec3 color=tone<.65?mix(bone,lime,tone*.55):mix(bone,coral,(tone-.65)/.35);
  if(id==uSelected)color=lime;
  float light=.32+.68*max(dot(normalize(normal),normalize(vec3(-.6,1,.3))),0.0);
  float fog=smoothstep(uFogDistance*.45,uFogDistance*1.5,distance(uEye,world))*.7;
  return vec4(mix(color*light,ink,fog),1);
}
