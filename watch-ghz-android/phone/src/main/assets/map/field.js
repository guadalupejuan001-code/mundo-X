// Numerical visualization of the dipole model in mundo-X PR #59.
// This browser implementation does not run QuantumRuntime or acquire sensors.
export const COLUMNS=['role','frame','x_m','y_m','z_m','time_s','resolution_m','bx_uT','by_uT','bz_uT'];
const fail=m=>{throw new Error(m);};
const finite=(v,name)=>{if(typeof v!=='string'||!v.trim()||!/^[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?$/.test(v.trim()))fail(`Valor numérico inválido: ${name}.`);const n=Number(v);if(!Number.isFinite(n))fail(`Valor no finito: ${name}.`);return n;};
const norm=v=>Math.hypot(...v);
const dist2=(a,b)=>a.reduce((s,x,i)=>s+(x-b[i])**2,0);
export function basis(r){const r2=r.reduce((s,x)=>s+x*x,0),r3=r2**1.5,r5=r2*r3;return r.map((x,i)=>[...r.map((_,j)=>i===j?1:0),...r.map((y,j)=>3*x*y/r5-(i===j?1/r3:0))]);}
export function rawField(parameters,r){return basis(r).map(row=>row.reduce((s,x,i)=>s+x*parameters[i],0));}
function csvCells(line){const result=[];let value='',quoted=false;for(let i=0;i<line.length;i++){const c=line[i];if(c==='"'){if(quoted&&line[i+1]==='"'){value+='"';i++;}else quoted=!quoted;}else if(c===','&&!quoted){result.push(value.trim());value='';}else value+=c;}if(quoted)fail('Comillas CSV sin cerrar.');result.push(value.trim());return result;}
export function parseCSV(text){
 if(typeof text!=='string'||text.length>2_000_000)fail('El CSV debe ocupar como máximo 2 MB.');
 const lines=text.replace(/^\uFEFF/,'').split(/\r?\n/);const content=lines.map((line,i)=>({line:line.trim(),number:i+1})).filter(v=>v.line&&!v.line.startsWith('#'));
 if(content.length<2)fail('El archivo no contiene muestras.');if(content.length>8193)fail('Máximo 8192 muestras por archivo.');
 const headers=csvCells(content.shift().line);if(new Set(headers).size!==headers.length)fail('Hay columnas duplicadas.');
 if(COLUMNS.some(k=>!headers.includes(k)))fail('Faltan columnas. Descarga el CSV de ejemplo para ver el formato.');
 if(headers.some(k=>![...COLUMNS,'snapshot','snapshot_time_s'].includes(k)))fail('Columna desconocida. Se aceptan las 10 columnas de la #59 y snapshot, snapshot_time_s.');
 const temporal=headers.includes('snapshot');if(temporal!==headers.includes('snapshot_time_s'))fail('Para 4D incluye tanto snapshot como snapshot_time_s.');
 const groups=new Map();let commonFrame,commonResolution;
 for(const {line,number} of content){
  try{
   const cells=csvCells(line);if(cells.length!==headers.length)fail('Número de columnas incorrecto.');const o=Object.fromEntries(headers.map((k,i)=>[k,cells[i]]));
   if(!['TRAIN','HOLDOUT'].includes(o.role))fail('role debe ser TRAIN o HOLDOUT.');if(!/^[A-Za-z0-9_-]{1,32}$/.test(o.frame))fail('frame debe tener 1–32 letras, números, guiones o guiones bajos.');
   const r=['x_m','y_m','z_m'].map(k=>finite(o[k],k)), b=['bx_uT','by_uT','bz_uT'].map(k=>finite(o[k],k)),t=finite(o.time_s,'time_s'),resolution=finite(o.resolution_m,'resolution_m');
   if(r.some(x=>Math.abs(x)>6)||norm(r)<.05)fail('Posición fuera del dominio: ±6 m por eje y al menos 0,05 m del origen.');
   if(b.some(x=>Math.abs(x)>200000))fail('Campo fuera del límite de ±200000 µT por eje.');if(t<0||t>1e10)fail('time_s fuera de rango.');if(resolution<.001||resolution>1)fail('resolution_m debe estar entre 0,001 y 1 m.');
   if(commonFrame!==undefined&&(commonFrame!==o.frame||Math.abs(commonResolution-resolution)>1e-12))fail('Todas las capturas deben compartir frame y resolución.');commonFrame=o.frame;commonResolution=resolution;
   const key=temporal?o.snapshot:'static';if(!/^[A-Za-z0-9_-]{1,32}$/.test(key))fail('snapshot inválido.');const at=temporal?finite(o.snapshot_time_s,'snapshot_time_s'):null;if(temporal&&(at<0||at>1e10||Math.abs(t-at)>15))fail('Cada lectura debe quedar a ±15 s del tiempo de su captura.');
   if(!groups.has(key)){if(groups.size>=32)fail('Máximo 32 capturas por archivo.');groups.set(key,{id:key,time:at,rows:[],frame:o.frame,resolution});}
   const group=groups.get(key);if(group.time!==at)fail('Una captura tiene varios snapshot_time_s.');
   group.rows.push({role:o.role,r,b,t,resolution,frame:o.frame,index:group.rows.length,line:number});if(group.rows.length>256)fail('Máximo 256 muestras por captura.');
  }catch(e){fail(`Línea ${number}: ${e.message}`);}
 }
 const frames=[...groups.values()];for(const f of frames){if(!temporal)f.time=Math.min(...f.rows.map(p=>p.t));try{f.model=fit(f.rows);f.score=score(f.model,f.rows.filter(p=>p.role==='HOLDOUT'));}catch(e){fail(`Captura ${f.id}: ${e.message}`);}}
 frames.sort((a,b)=>a.time-b.time);for(let i=1;i<frames.length;i++)if(frames[i].time===frames[i-1].time)fail('Dos capturas no pueden tener el mismo snapshot_time_s.');
 return {frames,temporal:temporal&&frames.length>1,synthetic:lines.some(x=>/^#.*(?:SYNTHETIC|SINTÉTIC)/i.test(x)),frame:commonFrame,resolution:commonResolution};
}
export function fit(rows){
 const train=rows.filter(r=>r.role==='TRAIN'),test=rows.filter(r=>r.role==='HOLDOUT');if(train.length<8||train.length>128)fail('Se necesitan entre 8 y 128 posiciones TRAIN.');if(test.length<4||test.length>128)fail('Se necesitan entre 4 y 128 posiciones HOLDOUT.');
 const resolution=rows[0].resolution;
 for(let i=0;i<rows.length;i++)for(let j=0;j<i;j++)if(dist2(rows[i].r,rows[j].r)<resolution**2*(1-1e-12))fail('Hay posiciones repetidas o demasiado próximas. TRAIN y HOLDOUT deben ocupar lugares distintos.');
 const gram=Array.from({length:6},()=>Array(6).fill(0)),rhs=Array(6).fill(0),mean=[0,0,0],min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];
 for(const p of train){p.r.forEach((x,i)=>{min[i]=Math.min(min[i],x);max[i]=Math.max(max[i],x);mean[i]+=p.b[i]/train.length;});basis(p.r).forEach((row,i)=>row.forEach((x,j)=>{rhs[j]+=x*p.b[i];row.forEach((y,k)=>gram[j][k]+=x*y);}));}
 const scale=gram.map((r,i)=>Math.sqrt(r[i]));if(scale.some(v=>!Number.isFinite(v)||v<1e-12))fail('Geometría insuficiente para ajustar el modelo.');
 const a=gram.map((r,i)=>[...r.map((x,j)=>x/(scale[i]*scale[j])),rhs[i]/scale[i]]);
 for(let p=0;p<6;p++){let best=p;for(let r=p+1;r<6;r++)if(Math.abs(a[r][p])>Math.abs(a[best][p]))best=r;if(Math.abs(a[best][p])<1e-10)fail('Las posiciones no permiten identificar el dipolo.');[a[p],a[best]]=[a[best],a[p]];for(let r=p+1;r<6;r++){const factor=a[r][p]/a[p][p];for(let j=p;j<7;j++)a[r][j]-=factor*a[p][j];}}
 const parameters=Array(6).fill(0);for(let i=5;i>=0;i--){let v=a[i][6];for(let j=i+1;j<6;j++)v-=a[i][j]*parameters[j]*scale[j];parameters[i]=v/a[i][i]/scale[i];}if(parameters.some(v=>!Number.isFinite(v)))fail('El ajuste no es numéricamente estable.');
 const model={parameters,mean,min,max,resolution,minTime:Math.min(...train.map(p=>p.t)),maxTime:Math.max(...train.map(p=>p.t)),trainCount:train.length};model.rmse=Math.sqrt(train.reduce((s,p)=>s+dist2(p.b,rawField(parameters,p.r)),0)/(3*train.length));return model;
}
export function predict(model,r,time){if(r.length!==3||r.some(v=>!Number.isFinite(v)||Math.abs(v)>6)||norm(r)<.05)fail('Posición de consulta inválida.');if(!Number.isFinite(time)||time<0||time>1e10||time<model.minTime-15||time>model.maxTime+15)fail('Consulta fuera de la ventana temporal.');if(r.some((x,i)=>x<model.min[i]-Math.max(.025,.1*(model.max[i]-model.min[i]))||x>model.max[i]+Math.max(.025,.1*(model.max[i]-model.min[i]))))fail('Consulta fuera del volumen calibrado.');return rawField(model.parameters,r);}
export function score(model,rows){let residual=0,baseline=0;for(const p of rows){residual+=dist2(p.b,predict(model,p.r,p.t));baseline+=dist2(p.b,model.mean);}const rmse=Math.sqrt(residual/(3*rows.length)),uniform=Math.sqrt(baseline/(3*rows.length));return {rmse,uniform,count:rows.length,improves:rmse<.8*uniform&&uniform-rmse>.15};}
export function makeDemo(base,strength=1){if(!Number.isFinite(strength)||strength<.1||strength>3)throw new Error("Factor de dipolo fuera de rango");const frames=[];for(let i=0;i<9;i++){const phase=i/8*Math.PI*2,parameters=[38,-11,22,.125+.045*Math.sin(phase),-.064+.04*(Math.cos(phase)-1),.088+.025*Math.sin(phase*2)];parameters.splice(3,3,...parameters.slice(3).map(v=>v*strength));const rows=base.frames[0].rows.map((p,j)=>({...p,r:[...p.r],b:rawField(parameters,p.r).map((x,k)=>x+.027*Math.sin((j+1)*7+(k+1)*3+i)),t:i*2,index:j}));const model=fit(rows);frames.push({id:`t${i}`,time:i*2,rows,model,score:score(model,rows.filter(p=>p.role==='HOLDOUT')),frame:base.frame,resolution:base.resolution});}return {...base,frames,temporal:true,synthetic:true};}
export function toCSV(data){const temporal=data.temporal;const lines=[data.synthetic?'# SYNTHETIC TEST DATA - not recorded by a real watch.':'# Imported data - physical origin not independently verified.',[...COLUMNS,...(temporal?['snapshot','snapshot_time_s']:[])].join(',')];for(const f of data.frames)for(const p of f.rows)lines.push([p.role,p.frame,...p.r,p.t,p.resolution,...p.b,...(temporal?[f.id,f.time]:[])].join(','));return lines.join('\n')+'\n';}
