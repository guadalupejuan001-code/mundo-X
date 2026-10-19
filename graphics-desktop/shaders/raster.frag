#version 300 es
precision highp float;
precision highp int;
// @COMMON@
in vec3 vNormal,vWorld;
in float vTone;
flat in int vId;
out vec4 outColor;
void main(){outColor=shade(vNormal,vWorld,vTone,vId);}
