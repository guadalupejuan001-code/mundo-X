#version 300 es
precision highp float;
uniform int uPoints;
in vec4 vColor;
out vec4 outColor;
void main(){
    float a=vColor.a;
    if(uPoints==1){
        vec2 p=gl_PointCoord*2.0-1.0;float r=dot(p,p);
        if(r>1.0)discard;
        a*=exp(-r*4.5);
    }
    outColor=vec4(vColor.rgb,a);
}
