// Classical test particles in prescribed fields. All internal units are SI.
// The sources do not react to particles; there is no particle-particle force.
export const GRAVITATIONAL_CONSTANT = 6.67430e-11;
const cross = (a,b) => [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]];
const add = (a,b,f=1) => a.map((v,i)=>v+f*b[i]);
const finite = a => Array.isArray(a)&&a.length===3&&a.every(Number.isFinite);

export function gravityField(position,center,mu,radius) {
  if(!finite(position)||!finite(center)||!Number.isFinite(mu)||mu<0||!Number.isFinite(radius)||radius<=0)throw new Error('Fuente gravitatoria inválida.');
  const r=position.map((x,i)=>x-center[i]);
  // Exact Newtonian field of a uniform-density transparent sphere.
  const denominator=Math.max(Math.hypot(...r),radius)**3;
  return r.map(x=>-mu*x/denominator);
}

export function magneticRotation(velocity,tesla,chargeMass,dt) {
  const t=tesla.map(b=>chargeMass*b*dt/2),s=t.map(x=>2*x/(1+t.reduce((n,v)=>n+v*v,0)));
  return add(velocity,cross(add(velocity,cross(velocity,t)),s));
}

export function advanceParticle(particle,dt,magnetic,gravity) {
  if(!Number.isFinite(dt)||dt<=0||!finite(particle.r)||!finite(particle.v)||!Number.isFinite(particle.qm))throw new Error('Partícula o paso inválido.');
  let v=add(particle.v,gravity(particle.r),dt/2);
  v=magneticRotation(v,magnetic(particle.r),particle.qm,dt/2);
  const r=add(particle.r,v,dt);
  v=magneticRotation(v,magnetic(r),particle.qm,dt/2);
  v=add(v,gravity(r),dt/2);
  if(!finite(r)||!finite(v))throw new Error('Integración no finita; simulación detenida.');
  return {r,v,qm:particle.qm};
}

export class FieldLab {
  constructor(){this.mode='both';this.magneticScale=1;this.gravityScale=1;this.running=false;this.mu=2e-4;this.time=0;this.particles=[];this.error='';this.remainder=0;}
  configure(lo,hi){
    if(!finite(lo)||!finite(hi)||hi.some((v,i)=>v<lo[i]))throw new Error('Volumen de simulación inválido.');
    if(this.lo&&lo.every((v,i)=>v===this.lo[i]&&hi[i]===this.hi[i]))return;
    this.lo=[...lo];this.hi=[...hi];this.center=lo.map((v,i)=>(v+hi[i])/2);
    this.available=hi.every((v,i)=>v-lo[i]>=.001);
    if(!this.available){this.running=false;this.particles=[];this.error='El laboratorio necesita un volumen con extensión en los tres ejes. El CSV sigue disponible.';return;}
    this.radius=Math.min(...hi.map((v,i)=>v-lo[i]))*.2;this.reset();
  }
  reset(){
    if(this.available===false)return;
    this.running=false;this.time=0;this.remainder=0;this.error='';
    if(!this.lo)return;
    this.particles=Array.from({length:12},(_,i)=>{
      const angle=i*Math.PI/6;
      const r=[this.center[0]+.30*(this.hi[0]-this.lo[0])*Math.cos(angle),this.center[1]+.30*(this.hi[1]-this.lo[1])*Math.sin(angle),this.center[2]+(i%3-1)*.12*(this.hi[2]-this.lo[2])];
      return {r,v:[-.04*Math.sin(angle),.04*Math.cos(angle),.008*(i%2?1:-1)],qm:1e5,trail:[[...r]]};
    });
  }
  gravity(r){return gravityField(r,this.center,this.mode==='magnetic'?0:this.mu*this.gravityScale,this.radius);}
  adjust(kind,delta){
    if(!['magnetic','gravity'].includes(kind)||!Number.isFinite(delta))return false;
    const key=kind+'Scale',min=kind==='magnetic'?-3:0;
    const next=Math.max(min,Math.min(3,Math.round((this[key]+delta)*10)/10));
    if(next===this[key])return false;this[key]=next;return true;
  }
  step(elapsed,readMicrotesla){
    if(!this.running||!Number.isFinite(elapsed)||elapsed<=0)return;
    const dt=1/480;this.remainder+=Math.min(elapsed,.05);
    const magnetic=r=>{
      if(this.mode==='gravity')return [0,0,0];
      const b=readMicrotesla(r);
      if(!finite(b)||Math.hypot(...b)>500)throw new Error('Campo fuera del rango del laboratorio (500 µT antes del factor).');
      return b.map(v=>v*1e-6*this.magneticScale);
    };
    try{
      while(this.remainder+1e-12>=dt){
        const next=this.particles.map(p=>{
          const q=advanceParticle(p,dt,magnetic,r=>this.gravity(r));
          for(let i=0;i<3;i++){
            if(q.r[i]<this.lo[i]){q.r[i]=2*this.lo[i]-q.r[i];q.v[i]=Math.abs(q.v[i]);}
            if(q.r[i]>this.hi[i]){q.r[i]=2*this.hi[i]-q.r[i];q.v[i]=-Math.abs(q.v[i]);}
            if(q.r[i]<this.lo[i]||q.r[i]>this.hi[i])throw new Error('Paso excesivo; simulación detenida.');
          }
          return {...q,trail:p.trail};
        });
        this.particles=next;this.time+=dt;this.remainder-=dt;
      }
      for(const p of this.particles){p.trail.push([...p.r]);if(p.trail.length>40)p.trail.shift();}
    }catch(error){this.running=false;this.remainder=0;this.error=error.message;}
  }
  snapshot(){return {mode:this.mode,magnetic:this.magneticScale,gravity:this.gravityScale,running:this.running,time:this.time,particles:this.particles.map(p=>({r:[...p.r],v:[...p.v]})),error:this.error};}
}
