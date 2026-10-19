#version 300 es
precision highp float;
precision highp int;
// @COMMON@
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aTransform;
uniform int uExplicit;
out vec3 vNormal,vWorld;
out float vTone;
flat out int vId;
void main(){
  int id=gl_InstanceID;vec2 center;float height,tone;
  if(uExplicit==1){
    center=aTransform.xy;height=aTransform.z;tone=aTransform.w;
    height*=1.0+.08*sin(uTime*.8+center.x*.07+center.y*.06);
    int q=id%uQuantumCount,qwidth=textureSize(uQuantum,0).x;
    height*=1.0+uQuantumStrength*texelFetch(uQuantum,ivec2(q%qwidth,q/qwidth),0).r;
  }else objectData(id,center,height,tone);
  vId=id;vTone=tone;
  if(height<=0.0){gl_Position=vec4(2,2,2,1);vNormal=vec3(0,1,0);vWorld=vec3(0);return;}
  vec3 p=aPosition*vec3(.76,height,.76)+vec3(center.x,height*.5,center.y);
  mat3 rot=rotation();vWorld=rot*p;vNormal=rot*aNormal;
  gl_Position=uVp*vec4(vWorld,1);
}
