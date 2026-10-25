import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {parseCSV,makeDemo,toCSV,predict,rawField} from '../phone/src/main/assets/map/field.js';
const csv=readFileSync(new URL('../phone/src/main/assets/map/baseline.csv',import.meta.url),'utf8'),base=parseCSV(csv);
const near=(a,b,t=1e-7)=>assert.ok(Math.abs(a-b)<t,a+' != '+b);
function changed(fn){const lines=csv.split('\n');return lines.map((line,i)=>line.startsWith('TRAIN,')||line.startsWith('HOLDOUT,')?fn(line.split(','),i).join(','):line).join('\n');}
test('PR59 reference fit, holdout and prediction match native output',()=>{
 const f=base.frames[0];assert.equal(base.synthetic,true);assert.equal(base.temporal,false);assert.equal(f.model.trainCount,16);assert.equal(f.score.count,8);
 near(f.model.rmse,.01926259);near(f.score.rmse,.02260290);near(f.score.uniform,4.557637,1e-6);
 const expected=[38.00169384,-10.99929056,22.00082971,.12527816,-.06405382,.08803802];f.model.parameters.forEach((v,i)=>near(v,expected[i]));
 const q=predict(f.model,[0,0,.24],2);[28.93932713,-6.36576781,34.73781147].forEach((v,i)=>near(q[i],v));assert.equal(f.score.improves,true);
});
test('holdout data does not enter training fit',()=>{const result=parseCSV(changed(c=>{if(c[0]==='HOLDOUT')c[7]=String(Number(c[7])+50);return c;}));assert.deepEqual(result.frames[0].model.parameters,base.frames[0].model.parameters);assert.ok(result.frames[0].score.rmse>20);assert.equal(result.frames[0].score.improves,false);});
test('plain PR59 timestamps remain one static capture',()=>assert.equal(base.frames.length,1));
test('4D demo is synthetic and round trips with distinct fitted snapshots',()=>{const demo=makeDemo(base),round=parseCSV(toCSV(demo));assert.equal(round.temporal,true);assert.equal(round.synthetic,true);assert.equal(round.frames.length,9);assert.deepEqual(round.frames.map(f=>f.time),[0,2,4,6,8,10,12,14,16]);assert.notDeepEqual(round.frames[0].model.parameters,round.frames[1].model.parameters);assert.ok(round.frames.every(f=>f.score.improves));});
test('static CSV round trip preserves values',()=>{const round=parseCSV(toCSV(base));assert.deepEqual(round.frames[0].model.parameters,base.frames[0].model.parameters);});
test('unknown physical origin is not relabeled as real',()=>assert.equal(parseCSV(csv.split('\n').filter(l=>!l.startsWith('#')).join('\n')).synthetic,false));
test('duplicate train positions cannot add independent evidence',()=>{let first;assert.throws(()=>parseCSV(changed(c=>{if(c[0]==='TRAIN'){first??=c.slice(2,5);c.splice(2,3,...first);}return c;})),/posiciones repetidas/);});
test('holdout must be disjoint from train',()=>assert.throws(()=>parseCSV(changed(c=>{if(c[0]==='HOLDOUT')c.splice(2,3,'-.26','-.23','.17');return c;})),/posiciones repetidas/));
test('sub-resolution jitter cannot create a new site',()=>assert.throws(()=>parseCSV(changed(c=>{if(c[0]==='HOLDOUT')c.splice(2,3,'-.259','-.23','.17');return c;})),/demasiado próximas/));
test('minimum distinct sample counts enforced',()=>assert.throws(()=>parseCSV(csv.split('\n').filter(l=>!l.startsWith('HOLDOUT')).join('\n')),/4 y 128/));
test('numeric errors, out-of-domain values and unsafe frames rejected',()=>{
 for(const [index,value] of [[2,'NaN'],[2,''],[3,'7'],[4,'Infinity'],[5,'-1'],[6,'0'],[7,'200001'],[1,'<script>']])assert.throws(()=>parseCSV(changed(c=>{c[index]=value;return c;})));
});
test('coordinate singularity and out-of-bounds queries fail',()=>{const m=base.frames[0].model;assert.throws(()=>predict(m,[0,0,0],2));assert.throws(()=>predict(m,[2,0,.2],2));assert.throws(()=>predict(m,[0,0,.24],50));assert.throws(()=>predict(m,[0,0,.24],NaN));});
test('snapshots need explicit matched metadata',()=>{assert.throws(()=>parseCSV(csv.replace('bz_uT','bz_uT,snapshot')),/tanto snapshot/);const temporal=toCSV(makeDemo(base));assert.throws(()=>parseCSV(temporal.replace(/,t1,2/g,',t1,0')),/mismo snapshot_time_s/);assert.throws(()=>parseCSV(temporal.replace(',t0,0',',t0,40')),/±15/);});
test('frame/resolution mismatches rejected',()=>{let once=false;assert.throws(()=>parseCSV(changed(c=>{if(!once){c[1]='other';once=true;}return c;})),/compartir frame/);});
test('quoted header, BOM and CRLF accepted',()=>{const quoted=csv.replace('role,frame','\"role\",\"frame\"');assert.equal(parseCSV('\uFEFF'+quoted.replaceAll('\n','\r\n')).frames[0].rows.length,24);});
test('uniform field correctly fails improvement criterion',()=>{const flat=changed(c=>{c.splice(7,3,'38','-11','22');return c;});assert.equal(parseCSV(flat).frames[0].score.improves,false);});
test('known dipole is recovered without noise',()=>{const params=[40,-15,18,.2,.1,-.04];const exact=changed(c=>{const b=rawField(params,c.slice(2,5).map(Number));c.splice(7,3,...b.map(String));return c;});parseCSV(exact).frames[0].model.parameters.forEach((v,i)=>near(v,params[i],1e-10));});
test('oversized inputs rejected before fitting',()=>assert.throws(()=>parseCSV('a'.repeat(2000001)),/2 MB/));


test('editing dipole changes the simulated source, preserving baseline samples',()=>{const before=JSON.stringify(base);const original=makeDemo(base),strong=makeDemo(base,1.1);assert.equal(JSON.stringify(base),before);const a=original.frames[0].model.parameters,b=strong.frames[0].model.parameters;for(let i=3;i<6;i++)near(b[i]-a[i],base.frames[0].model.parameters[i]*.1,.0001);assert.notDeepEqual(strong.frames[0].rows[0].b,original.frames[0].rows[0].b);assert.equal(parseCSV(toCSV(strong)).synthetic,true);});
test('dipole factor is bounded',()=>{for(const v of [0,3.1,NaN,Infinity])assert.throws(()=>makeDemo(base,v));});
