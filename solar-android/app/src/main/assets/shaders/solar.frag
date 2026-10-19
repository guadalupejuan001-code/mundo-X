#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;

// Analytic spherical observer derived from COSMIC X's eye.frag camera-ray path.
// Positions arrive relative to a double precision CPU camera. No state vector,
// orbital constants, or planetary geometry is modified by quantum observables.
const int MAX_BODIES=24;
const float PI=3.141592653589793;
uniform vec2 uResolution;
uniform vec3 uForward,uRight,uUp;
uniform float uAspect,uTanHalfFov,uNear,uFar,uTime;
uniform int uCount;
uniform vec4 uBodies[MAX_BODIES]; // camera-relative centre, display radius
uniform vec4 uAppearance[MAX_BODIES]; // spin, obliquity, inner ring, outer ring
uniform vec4 uColors[MAX_BODIES];
uniform vec3 uAxes[MAX_BODIES];
uniform vec4 uAtmosphere[MAX_BODIES]; // top/radius, scale height/radius, visual opacity, class
uniform vec3 uAtmosphereColors[MAX_BODIES];
uniform vec4 uEntry; // body (-1 outside entry), altitude km, mean radius km, active
uniform int uAtmosphereEnabled;
uniform int uLayers[MAX_BODIES];
uniform sampler2DArray uMaps;
uniform vec4 uQ0,uQ1;
uniform float uCoupling;
out vec4 outColor;

float hash(float n){return fract(sin(n)*43758.5453123);}
float hash3(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}
float noise(vec3 p){
    vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(mix(hash3(i),hash3(i+vec3(1,0,0)),f.x),
                   mix(hash3(i+vec3(0,1,0)),hash3(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(hash3(i+vec3(0,0,1)),hash3(i+vec3(1,0,1)),f.x),
                   mix(hash3(i+vec3(0,1,1)),hash3(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float fbm(vec3 p){
    float v=0.0,w=0.5;
    for(int i=0;i<4;i++){v+=w*noise(p);p=p*2.07+vec3(13.2,7.4,3.1);w*=0.5;}
    return v;
}
// A bounded, inexpensive noise field for close visual detail and cloud volumes.
// These functions add apparent relief; they are not measured elevation data.
float detailHash(vec3 p){p=fract(p*vec3(.1031,.1030,.0973));p+=dot(p,p.yxz+33.33);return fract((p.x+p.y)*p.z);}
float detailNoise(vec3 p){
    vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(mix(detailHash(i),detailHash(i+vec3(1,0,0)),f.x),mix(detailHash(i+vec3(0,1,0)),detailHash(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(detailHash(i+vec3(0,0,1)),detailHash(i+vec3(1,0,1)),f.x),mix(detailHash(i+vec3(0,1,1)),detailHash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float detailFbm(vec3 p){return detailNoise(p)*.57+detailNoise(p*2.03+7.1)*.28+detailNoise(p*4.11+19.7)*.15;}
bool entering(int id){return uEntry.w>.5&&int(uEntry.x+.5)==id;}
bool atmosphereFor(int id){return uAtmosphereEnabled==1||(uEntry.w>.5&&!entering(id));}
vec3 rotZ(vec3 p,float a){float c=cos(a),s=sin(a);return vec3(c*p.x-s*p.y,s*p.x+c*p.y,p.z);}
vec3 rotY(vec3 p,float a){float c=cos(a),s=sin(a);return vec3(c*p.x+s*p.z,p.y,-s*p.x+c*p.z);}
vec3 localNormal(vec3 n,int id){
    vec3 pole=uAxes[id];
    vec3 reference=abs(pole.z)>.98?vec3(0,1,0):vec3(0,0,1);
    vec3 x=normalize(cross(pole,reference)),z=cross(x,pole);
    return rotY(vec3(dot(n,x),dot(n,pole),dot(n,z)),-uAppearance[id].x);
}
vec3 terrainNormal(vec3 n,int id,float ocean){
    if(!entering(id)||id==0||uAtmosphere[id].w==5.0)return n;
    float radiusKm=max(uEntry.z,.01),frequency=radiusKm/1.8;
    vec3 p=localNormal(n,id);
    float footprint=length(fwidth(p))*frequency;
    float visible=(1.0-smoothstep(.25,1.2,footprint))*(1.0-smoothstep(90.0,180.0,uEntry.y));
    if(visible<.002)return n;
    vec3 tangent=normalize(cross(n,abs(n.y)>.9?vec3(0,0,1):vec3(0,1,0))),bitangent=cross(n,tangent);
    vec3 tx=localNormal(tangent,id),ty=localNormal(bitangent,id);
    float e=.07/max(1.0,frequency);
    float h=detailFbm(p*frequency);
    vec2 slope=vec2(detailFbm((p+tx*e)*frequency)-h,detailFbm((p+ty*e)*frequency)-h)/e;
    float amplitude=.30/radiusKm;
    if(id==3)amplitude*=1.0-ocean*.97;
    return normalize(n-(tangent*slope.x+bitangent*slope.y)*amplitude*visible);
}

float sphere(vec3 rd,vec3 centre,float radius){
    float b=dot(centre,rd);
    // Perpendicular-distance form avoids b*b-(dot(c,c)-r*r) cancellation.
    vec3 closest=centre-rd*b;
    float h=radius*radius-dot(closest,closest);
    if(h<0.0)return -1.0;
    h=sqrt(h);float t=b-h;
    return t>uNear?t:(b+h>uNear?b+h:-1.0);
}
vec3 sky(vec3 rd){
    vec3 col=vec3(0.0010,0.0018,0.0045);
    float milky=pow(max(0.0,1.0-abs(dot(rd,normalize(vec3(.23,.89,.39))))),20.0);
    col+=milky*vec3(.006,.009,.017)*(0.35+fbm(rd*14.0));
    vec2 uv=vec2(atan(rd.z,rd.x)/(2.0*PI)+.5,asin(clamp(rd.y,-1.0,1.0))/PI+.5);
    for(int level=0;level<2;level++){
        vec2 g=uv*vec2(level==0?1800.0:850.0,level==0?900.0:425.0);
        vec2 cell=floor(g),f=fract(g);
        float h=hash(dot(cell,vec2(37.17,91.73))+float(level)*47.0);
        vec2 offset=vec2(hash(h*177.0),hash(h*391.0));
        vec2 delta=f-offset;
        float pixel=max(length(fwidth(g))*.42,.045);
        float star=exp(-dot(delta,delta)/(pixel*pixel));
        star*=step(level==0?.9975:.996,h)*(level==0?.85:1.8);
        vec3 tint=mix(vec3(.62,.77,1.0),vec3(1.0,.81,.58),hash(h*419.1));
        col+=star*tint;
    }
    return col;
}
vec3 surface(vec3 n,int id,out float ocean,out float clouds){
    vec3 p=localNormal(n,id);
    float latitude=p.y,lon=atan(p.z,p.x);
    // North-up NASA maps advance east to the right. Local +Y spin advances
    // +X toward -Z, so geographic east is -atan(z,x). PIA07782's labelled
    // System-III west longitudes likewise decrease left-to-right (180..0..180).
    vec2 uv=vec2(.5-lon/(2.0*PI),.5-asin(clamp(latitude,-1.0,1.0))/PI);
    ocean=0.0;clouds=0.0;
    vec3 c=uColors[id].rgb;
    if(uLayers[id]>=0)c=texture(uMaps,vec3(uv,float(uLayers[id]))).rgb;
    float f=fbm(p*5.8);
    if(id==0){
        float granule=fbm(p*90.0+vec3(uTime*.018,0,0));
        float cells=noise(p*270.0);
        float spots=pow(smoothstep(.51,.71,fbm(p*8.0)),3.0)*.73;
        vec3 photosphere=mix(vec3(1.0,.18,.009),vec3(1.0,.67,.18),smoothstep(.25,.70,granule));
        return photosphere*(1.0+granule*1.1+cells*.22)*(1.0-spots);
    }
    if(id==3){
        if(uLayers[id]<0){
            float land=smoothstep(.465,.5,fbm(p*3.5));
            c=mix(vec3(.017,.13,.31),mix(vec3(.10,.23,.10),vec3(.46,.38,.20),f),land);
            c=mix(c,vec3(.88,.94,1.0),smoothstep(.83,.98,abs(latitude)+f*.13));
        }
        ocean=1.0-smoothstep(-.025,.08,c.r-c.b*.63);
        vec3 cp=rotY(p,uTime*.00035);
        float cloudNoise=fbm(cp*8.4+vec3(0,noise(cp*4.0)*2.0,0));
        clouds=atmosphereFor(id)&&!entering(id)?smoothstep(.51,.69,cloudNoise)*.73:0.0;
        c=mix(c,vec3(.95,.98,1.0),clouds);
    }else if(id==2){
        float swirls=fbm(vec3(p.x*4.0,latitude*15.0+sin(lon*3.0)*.9,p.z*4.0));
        c=mix(vec3(.62,.39,.14),vec3(.97,.84,.54),swirls*.75+.25);
        if(entering(id)&&uEntry.y<40.0)c=mix(vec3(.28,.17,.085),vec3(.52,.34,.16),detailFbm(p*31.0));
    }else if(id==4){
        if(uLayers[id]<0){
            c=mix(vec3(.20,.072,.029),vec3(.71,.28,.11),smoothstep(.25,.65,f));
            c=mix(c,vec3(.74,.61,.48),smoothstep(.61,.8,fbm(p*17.0))*.6);
            c=mix(c,vec3(.92,.93,.87),smoothstep(.935,.98,abs(latitude)+f*.04));
        }
    }else if(id==5||id==6){
        if(uLayers[id]<0){
            float swirl=sin(lon*9.0+latitude*16.0)*.02+fbm(p*13.0)*.05;
            float band=sin((latitude+swirl)*55.0)*.5+.5;
            if(id==5){
                c=mix(vec3(.51,.28,.15),vec3(.95,.86,.66),band*.6+f*.4);
                vec2 delta=vec2((lon+.7)/.24,(latitude+.34)/.09);float spot=exp(-dot(delta,delta));
                c=mix(c,vec3(.69,.27,.12),spot*.85);
            }else c=mix(vec3(.56,.45,.29),vec3(.91,.83,.60),band*.18+f*.42+.34);
        }
    }else if(id==7){
        c=mix(vec3(.28,.62,.68),vec3(.55,.84,.87),.45+latitude*.12+f*.12);
    }else if(id==8){
        c=mix(vec3(.03,.15,.50),vec3(.21,.48,.90),.32+.20*sin(latitude*21.0+f*.4)+f*.22);
        float cloud=smoothstep(.73,.89,fbm(vec3(p.x*8.0,p.y*34.0,p.z*8.0)));
        c=mix(c,vec3(.65,.79,.92),cloud*.45);
    }else if(id!=3 && uLayers[id]<0){
        float small=fbm(p*45.0);
        c*=.58+f*.54+small*.22;
        if(id==1)c=mix(c,vec3(.31,.28,.23),.28);
        if(id>=10){
            float fracture=pow(1.0-abs(sin(p.x*49.0+noise(p*12.0)*6.0)),17.0);
            c*=1.0-fracture*.22;
        }
    }
    if(entering(id)&&id!=0&&uAtmosphere[id].w!=5.0){
        float frequency=max(2.0,uEntry.z/3.5);
        float footprint=length(fwidth(p))*frequency;
        float amount=(1.0-smoothstep(.3,1.5,footprint))*(1.0-smoothstep(90.0,180.0,uEntry.y));
        float ridges=detailFbm(p*frequency),grain=detailNoise(p*frequency*7.0);
        float material=.80+ridges*.32+(grain-.5)*.08*(1.0-smoothstep(.05,.25,footprint));
        c*=mix(1.0,material,amount*(id==3?1.0-ocean*.95:1.0));
    }
    return pow(max(c,vec3(0.0)),vec3(2.2));
}
float eclipse(vec3 point,int self){
    vec3 s=uBodies[0].xyz-point;
    float maximum=length(s);vec3 l=s/maximum;
    float result=1.0;
    for(int j=1;j<MAX_BODIES;j++){
        if(j>=uCount)break;
        if(j==self)continue;
        vec3 c=uBodies[j].xyz-point;float along=dot(c,l);
        if(along<=0.0||along>=maximum)continue;
        float radial=length(c-l*along);
        float penumbra=uBodies[0].w*along/maximum;
        result*=smoothstep(max(0.0,uBodies[j].w-penumbra),uBodies[j].w+max(penumbra,.00001),radial);
    }
    return max(.018,result);
}
float ringOpacity(float radial,int id){
    vec4 a=uAppearance[id];float t=(radial-a.z)/(a.w-a.z);
    if(t<0.0||t>1.0)return 0.0;
    float edge=smoothstep(0.0,.018,t)*(1.0-smoothstep(.985,1.0,t));
    float footprint=max(fwidth(radial),.00001);
    float fine=1.0-smoothstep(.5,2.8,footprint*617.0);
    float medium=1.0-smoothstep(.5,2.8,footprint*172.0);
    float bands=.55+.21*sin(radial*172.0)*medium+.12*sin(radial*617.0)*fine;
    // Cassini division 117,507..122,340 km, divided by Saturn mean radius 58,232 km.
    float cassini=id==6?1.0-.89*smoothstep(2.013,2.022,radial)*(1.0-smoothstep(2.093,2.105,radial)):1.0;
    return edge*bands*cassini;
}
vec3 atmosphereColor(int id){
    return atmosphereFor(id)&&uAtmosphere[id].z>0.0?uAtmosphereColors[id]:vec3(0.0);
}
float cloudDensity(vec3 point,float height,int id){
    float scale=max(uAtmosphere[id].y,.000001),h=height/scale;
    float profile=0.0;
    if(id==3)profile=smoothstep(.09,.30,h)*(1.0-smoothstep(1.0,1.6,h));
    else if(id==2)profile=smoothstep(2.5,3.0,h)*(1.0-smoothstep(4.2,4.8,h))*2.0;
    else if(id==14)profile=smoothstep(.25,.65,h)*(1.0-smoothstep(1.1,1.6,h))*.14;
    else if(uAtmosphere[id].w==5.0){float layer=(h-.5)/2.1;profile=exp(-layer*layer)*2.7;}
    else return 0.0;
    if(profile<.001)return 0.0;
    vec3 p=localNormal(point,id);
    vec3 flow=p/max(scale*1.4,.00001)+vec3(uTime*.045,0,uTime*.025);
    float shape=detailNoise(flow)*.65+detailNoise(flow*2.02+7.7)*.35;
    if(id==3){
        float cover=detailNoise(normalize(p)*9.0+vec3(.0,uTime*.0008,0));
        shape=smoothstep(.45,.66,shape*.45+cover*.55);
    }else if(id==2)shape=.65+.35*shape;
    else shape=.20+.80*shape;
    return profile*shape;
}
// Single-scattering visual approximation, integrated through a spherical
// density shell. Includes inside views; no MHD, weather or pressure simulation.
void atmosphereVolume(vec3 rd,int id,float maximum,bool background,inout vec3 color){
    vec4 env=uAtmosphere[id];if(env.x<=0.0||env.y<=0.0||env.z<=0.0)return;
    float radius=uBodies[id].w;vec3 ro=-uBodies[id].xyz/radius;
    float b=dot(ro,rd),shell=1.0+env.x;
    vec3 perpendicular=ro-rd*b;float discriminant=shell*shell-dot(perpendicular,perpendicular);
    if(discriminant<=0.0)return;
    float halfChord=sqrt(discriminant),begin=max(uNear/radius,-b-halfChord),end=min(maximum/radius,-b+halfChord);
    if(end<=begin)return;
    float scale=max(env.y,.000001),eyeHeight=length(ro)-1.0;
    float densityHere=exp(clamp(-eyeHeight/scale,-20.0,8.0));
    float coefficient=env.w==3.0?.24:env.w==2.0?.10:env.w==4.0?1.2:.65;
    if(entering(id)){
        float visibility=10.0*scale/max(.01,densityHere*env.z*coefficient);
        end=min(end,begin+min(env.x*3.0,max(scale*.003,visibility)));
    }
    bool star=env.w==6.0;
    vec3 light=star?vec3(1,0,0):normalize(uBodies[0].xyz-uBodies[id].xyz);
    float angle=max(0.0,dot(rd,light));
    float cosine=dot(rd,light);float phase=.65+.45*cosine*cosine+pow(angle,24.0)*.4;
    vec3 accumulated=vec3(0);float transmission=1.0,daylight=0.0;
    int steps=entering(id)?10:6;
    for(int sampleIndex=0;sampleIndex<10;sampleIndex++){
        if(sampleIndex>=steps)break;
        float a=float(sampleIndex)/float(steps),z=float(sampleIndex+1)/float(steps);
        // More samples close to the observer, where local detail matters.
        if(entering(id)){a*=a;z*=z;}
        float segment=(end-begin)*(z-a),t=begin+(end-begin)*(a+z)*.5;
        vec3 point=ro+rd*t;float height=length(point)-1.0;
        float density=exp(clamp(-height/scale,-20.0,8.0));
        float cloud=star?0.0:cloudDensity(point,height,id);
        float haze=density*env.z*coefficient,sigma=(haze+cloud*1.6)/scale;
        float extinction=1.0-exp(-min(35.0,sigma*segment));
        float day=star?1.0:smoothstep(-.12,.20,dot(normalize(point),light));daylight=max(daylight,day);
        vec3 hazeColor=uAtmosphereColors[id]*(.025+day*.95)*phase;
        vec3 cloudColor=mix(vec3(.82,.85,.89),uAtmosphereColors[id],env.w==5.0?.66:env.w==4.0?.45:.04);
        if(env.w==5.0){float bands=.90+.10*sin(localNormal(normalize(point),id).y*65.0);cloudColor*=bands;}
        cloudColor*=.035+day*.92;
        vec3 source=mix(hazeColor,cloudColor,cloud*1.6/max(.00001,haze+cloud*1.6));
        if(star)source*=.75+detailNoise(point*90.0+uTime*.015)*.5;
        accumulated+=transmission*extinction*source;transmission*=1.0-extinction;
        if(transmission<.003)break;
    }
    // Exposure to a daytime sky hides the decorative deep-space star field.
    if(background&&entering(id)&&!star)color*=1.0-daylight*smoothstep(.005,.12,1.0-transmission)*.998;
    color=color*transmission+accumulated;
}
void main(){
    vec2 ndc=2.0*gl_FragCoord.xy/uResolution-1.0;
    vec3 rd=normalize(uForward+ndc.x*uAspect*uTanHalfFov*uRight+ndc.y*uTanHalfFov*uUp);
    vec3 color=vec3(0);
    float closest=uFar;int id=-1;
    for(int j=0;j<MAX_BODIES;j++){
        if(j>=uCount)break;
        // A gas giant's reference radius is not a solid surface. During entry,
        // only its participating cloud/haze volume may occlude the view.
        if(entering(j)&&uAtmosphere[j].w==5.0)continue;
        float t=sphere(rd,uBodies[j].xyz,uBodies[j].w);
        if(t>0.0&&t<closest){closest=t;id=j;}
    }
    if(id<0)color=sky(rd);
    gl_FragDepth=1.0;
    if(id>=0){
        vec3 point=rd*closest;
        vec3 normal=normalize(point-uBodies[id].xyz);
        float ocean,clouds;vec3 albedo=surface(normal,id,ocean,clouds);
        if(id==0){
            float limb=pow(max(0.0,dot(normal,-rd)),.23);
            color=albedo*(.61+.39*limb);
        }else{
            normal=terrainNormal(normal,id,ocean);
            vec3 light=normalize(uBodies[0].xyz-point);
            float nl=dot(normal,light),visibility=eclipse(point,id);
            float diffuse=max(nl,0.0)*visibility;
            vec3 ambient=albedo*.012;
            color=albedo*(diffuse*1.2)+ambient;
            if(id==3){
                vec3 halfVector=normalize(light-rd);
                color+=vec3(.91,.95,1.0)*pow(max(0.0,dot(normal,halfVector)),105.0)*ocean*(1.0-clouds)*diffuse*.75;
            }
            float rim=pow(1.0-max(0.0,dot(normal,-rd)),3.5);
            if(!entering(id))color+=atmosphereColor(id)*rim*(.10+.35*smoothstep(-.22,.55,nl))*visibility;
            // A planet casts its shadow onto its own rings, and rings dim its day side.
            if(uAppearance[id].w>0.0){
                vec3 axis=uAxes[id];
                float den=dot(light,axis);
                if(abs(den)>.0001){
                    float d=-dot(point-uBodies[id].xyz,axis)/den;
                    if(d>0.001){
                        float rr=length(point+light*d-uBodies[id].xyz)/uBodies[id].w;
                        color*=1.0-ringOpacity(rr,id)*.73;
                    }
                }
            }
        }
        float viewZ=dot(point,uForward);
        gl_FragDepth=clamp(uFar/(uFar-uNear)-(uFar*uNear/(uFar-uNear))/max(viewZ,uNear),0.0,1.0);
    }
    // Analytic rings avoid polygon seams and remain stable during close flybys.
    for(int j=1;j<MAX_BODIES;j++){
        if(j>=uCount)break;
        if(uAppearance[j].w<=0.0)continue;
        vec3 axis=uAxes[j];
        float den=dot(rd,axis);if(abs(den)<.00001)continue;
        float t=dot(uBodies[j].xyz,axis)/den;
        if(t<=uNear||t>=closest)continue;
        vec3 p=rd*t-uBodies[j].xyz;
        float rr=length(p)/uBodies[j].w;
        float opacity=ringOpacity(rr,j);if(opacity<=.003)continue;
        vec3 light=normalize(uBodies[0].xyz-rd*t);
        float b=dot(-p,light);float shadow=1.0;
        if(b>0.0){float q=length(p+light*b)/uBodies[j].w;shadow=smoothstep(.96,1.04,q);}
        float tint=.55+.2*sin(rr*31.0)+.15*sin(rr*113.0);
        vec3 ring=mix(vec3(.18,.13,.078),vec3(.72,.62,.41),tint);
        ring*=.32+.83*abs(dot(axis,light));ring*=.12+.88*shadow;
        if(j==7||j==8)ring*=vec3(.44,.62,.80);
        color=mix(color,ring,opacity*.91);
        if(opacity>.35){
            float viewZ=t*dot(rd,uForward);
            gl_FragDepth=clamp(uFar/(uFar-uNear)-(uFar*uNear/(uFar-uNear))/max(viewZ,uNear),0.0,1.0);
        }
    }
    // Read-only expectation values shape visible corona tracers, never ephemerides.
    vec3 sun=uBodies[0].xyz;float along=dot(sun,rd);
    if(along>0.0&&(id==0||along<closest)){
        vec3 off=rd*along-sun;float radius=length(off)/uBodies[0].w;
        if(radius>1.0&&radius<10.0){
            float angle=atan(dot(off,uUp),dot(off,uRight));
            float qPhase=(dot(uQ0,vec4(.19,.31,.47,.71))+dot(uQ1,vec4(.23,.41,.61,.83)))*uCoupling;
            float rays=.6+.22*sin(angle*15.0+qPhase*3.0)+.16*sin(angle*39.0-uTime*.03+uQ1.x*uCoupling*2.0);
            float halo=exp(-(radius-1.0)*3.5)*.20+exp(-(radius-1.0)*.8)*.006*rays;
            color+=vec3(1.0,.36,.055)*halo;
        }
    }
    if(uAtmosphereEnabled==1){
        for(int j=0;j<MAX_BODIES;j++){
            if(j>=uCount)break;
            // Entry resolves the local medium; distant bodies keep their
            // orbital shading without paying for another local ray march.
            if(uEntry.w>.5&&!entering(j))continue;
            atmosphereVolume(rd,j,closest,id<0,color);
        }
    }
    color=vec3(1.0)-exp(-max(color,vec3(0.0))*1.3);
    color=pow(color,vec3(1.0/2.2));
    float vignette=1.0-.14*pow(length(ndc*.59),2.0);
    outColor=vec4(color*vignette,1.0);
}
