import test from 'node:test';
import assert from 'node:assert/strict';
import {advanceParticle,gravityField,magneticRotation,FieldLab} from '../phone/src/main/assets/map/dynamics.js';
const zero=()=>[0,0,0],norm=v=>Math.hypot(...v),near=(a,b,e=1e-8)=>assert.ok(Math.abs(a-b)<e,`${a} != ${b}`);

test('without fields particles follow Newtonian inertial motion',()=>{
 let p={r:[1,2,3],v:[.1,-.2,.3],qm:1};
 for(let i=0;i<100;i++)p=advanceParticle(p,.01,zero,zero);
 p.r.forEach((x,i)=>near(x,[1.1,1.8,3.3][i]));assert.deepEqual(p.v,[.1,-.2,.3]);
});
test('magnetic force bends moving charge with analytic gyro direction and frequency',()=>{
 let p={r:[0,0,0],v:[1,0,0],qm:2};const dt=Math.PI/(2*2*10000);
 for(let i=0;i<10000;i++)p=advanceParticle(p,dt,()=>[0,0,1],zero);
 near(p.v[0],0,1e-7);near(p.v[1],-1,1e-7);near(p.r[0],.5,1e-7);near(p.r[1],-.5,1e-7);
});
test('magnetic-only integration conserves speed even across a changing field',()=>{
 let v=[.4,.2,-.6];const speed=norm(v);
 for(let i=0;i<50000;i++)v=magneticRotation(v,[Math.sin(i),.3,Math.cos(i)],1,.01);
 near(norm(v),speed,1e-10);
});
test('a stationary charge is not attracted by a magnetic field',()=>{
 const p=advanceParticle({r:[1,0,0],v:[0,0,0],qm:1},.1,()=>[0,0,1],zero);
 assert.deepEqual(p.r,[1,0,0]);assert.deepEqual(p.v,[0,0,0]);
});
test('negative magnetic factor reverses bending',()=>{
 const positive=magneticRotation([1,0,0],[0,0,1],1,.1),negative=magneticRotation([1,0,0],[0,0,-1],1,.1);
 near(positive[0],negative[0]);near(positive[1],-negative[1]);
});
test('spherical gravity has inverse square falloff, interior linearity and finite center',()=>{
 near(norm(gravityField([2,0,0],[0,0,0],1,.5)),.25);
 near(norm(gravityField([4,0,0],[0,0,0],1,.5)),.0625);
 near(norm(gravityField([.25,0,0],[0,0,0],1,.5)),2);
 assert.equal(norm(gravityField([0,0,0],[0,0,0],1,.5)),0);
 assert.ok(gravityField([2,0,0],[0,0,0],1,.5)[0]<0);
});
test('gravity-only circular orbit matches analytic return and conserves energy',()=>{
 let p={r:[1,0,0],v:[0,1,0],qm:0};const dt=2*Math.PI/10000,g=r=>gravityField(r,[0,0,0],1,.1);
 for(let i=0;i<10000;i++)p=advanceParticle(p,dt,zero,g);
 near(p.r[0],1,1e-6);near(p.r[1],0,1e-6);
 near(norm(p.v)**2/2-1/norm(p.r),-.5,1e-10);
});
test('lab modes independently disable their force',()=>{
 const lab=new FieldLab();lab.configure([-.26,-.23,.17],[.26,.23,.32]);
 lab.mode='magnetic';assert.equal(norm(lab.gravity([.1,0,.25])),0);
 lab.mode='gravity';lab.running=true;lab.step(.02,()=>{throw Error('Magnetic callback should not run');});assert.equal(lab.error,'');assert.ok(lab.time>0);
});
test('combined field trajectory remains bounded and finite across reflective walls',()=>{
 const lab=new FieldLab();lab.configure([-.26,-.23,.17],[.26,.23,.32]);lab.running=true;
 for(let i=0;i<1000;i++)lab.step(.02,()=>[38,-11,22]);
 assert.equal(lab.error,'');assert.ok(lab.time>19.99);
 for(const p of lab.particles){p.r.forEach((x,i)=>assert.ok(x>=lab.lo[i]&&x<=lab.hi[i]));assert.ok(p.v.every(Number.isFinite));assert.ok(p.trail.length<=40);}
});
test('pause, reset and large wall-clock gaps cannot advance hidden simulation time',()=>{
 const lab=new FieldLab();lab.configure([-.26,-.23,.17],[.26,.23,.32]);const start=lab.snapshot();
 lab.step(10,()=>[0,0,40]);assert.deepEqual(lab.snapshot(),start);
 lab.running=true;lab.step(10,()=>[0,0,40]);assert.ok(lab.time<=.05001);
 lab.reset();assert.deepEqual(lab.snapshot(),start);
});
test('invalid or excessive field stops instead of inventing a trajectory',()=>{
 for(const field of [[NaN,0,0],[600,0,0]]){const lab=new FieldLab();lab.configure([-.26,-.23,.17],[.26,.23,.32]);lab.running=true;lab.step(.02,()=>field);assert.equal(lab.running,false);assert.ok(lab.error);assert.equal(lab.time,0);}
});
test('factors reach zero, allow reversed B and reject out-of-range changes',()=>{
 const lab=new FieldLab();for(let i=0;i<40;i++)lab.adjust('magnetic',-.1);
 assert.equal(lab.magneticScale,-3);assert.equal(lab.adjust('magnetic',-.1),false);
 for(let i=0;i<10;i++)lab.adjust('gravity',-.1);assert.equal(lab.gravityScale,0);assert.equal(lab.adjust('gravity',-.1),false);
 assert.equal(lab.adjust('gravity',NaN),false);
});

test('a planar data set disables only the particle lab, without rejecting the map',()=>{const lab=new FieldLab();lab.configure([-.2,-.2,.2],[.2,.2,.2]);assert.equal(lab.particles.length,0);assert.equal(lab.running,false);assert.ok(lab.error);lab.reset();assert.equal(lab.particles.length,0);});
