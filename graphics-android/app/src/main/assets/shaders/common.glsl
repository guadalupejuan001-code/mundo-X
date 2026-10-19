// ObjectSceneModel rules, with an optional explicitly bounded quantum control.
uniform int uCount,uSide,uEditCount,uSelected,uDebug;
uniform highp uvec4 uSeedBytes;
uniform highp isampler2D uEdits;
uniform float uTime,uFogDistance,uQuantumStrength;
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
vec4 shade(vec3 normal,vec3 world,float tone,int id){
  if(uDebug==1)return encodeId(id);
  vec3 bone=vec3(.925,.902,.839),lime=vec3(.839,.929,.404);
  vec3 coral=vec3(.875,.506,.424),ink=vec3(.067,.086,.075);
  vec3 color=tone<.65?mix(bone,lime,tone*.55):mix(bone,coral,(tone-.65)/.35);
  if(id==uSelected)color=lime;
  float light=.32+.68*max(dot(normalize(normal),normalize(vec3(-.6,1,.3))),0.0);
  float fog=smoothstep(uFogDistance*.45,uFogDistance*1.5,distance(uEye,world))*.7;
  return vec4(mix(color*light,ink,fog),1);
}
