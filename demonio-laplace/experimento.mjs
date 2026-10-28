#!/usr/bin/env node
/* Real execution against Mundo X XI; no fake outputs or independent engine. */
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
import {WIDTH,TICKS,INITIAL,EVENT_TICK,EVENT_SITE,ADDRESSES,worldXI,
        reversibleXI,quantumLimitXI,oracle} from './modelo.mjs';

const need=(ok,msg)=>{if(!ok)throw Error(msg);};
const sha=s=>createHash('sha256').update(s).digest('hex');
const [binArg,outArg]=process.argv.slice(2);
need(!!binArg&&!!outArg,'Uso: node demonio-laplace/experimento.mjs RUTA_BINARIO_XI RUTA_RESULTADOS');
const binary=resolve(binArg),out=resolve(outArg);
mkdirSync(out,{recursive:true});
function run(name,program){
  const file=join(out,name+'.xi');
  writeFileSync(file,program,'utf8');
  const result=spawnSync(binary,[file],{encoding:'utf8',timeout:120000,maxBuffer:12*1024*1024});
  need(!result.error&&result.status===0,'Error de XI en '+name+': '+String(result.stderr||result.error));
  const lines=result.stdout.split('\n').filter(Boolean);
  const rows=lines.map(line=>JSON.parse(line));
  return {rows,sha256:sha(program)};
}
function readWorld(world){
  const qubits=world.rows.filter(x=>x.type==='probz');
  need(qubits.length===WIDTH*(TICKS+1),'Numero de observaciones incorrecto');
  const states=[];
  for(let t=0;t<=TICKS;t++){
    let state=0;
    for(let i=0;i<WIDTH;i++){
      const q=qubits[t*WIDTH+i];
      need(q.address===ADDRESSES[i],'La direccion Ether observada no corresponde al sitio');
      need(q.probability_one===0||q.probability_one===1,'El mundo cerrado contiene indeterminacion');
      if(q.probability_one===1)state|=(1<<i);
    }
    states.push(state);
  }
  const core=world.rows.filter(x=>x.type==='stats');
  const memory=world.rows.filter(x=>x.type==='memory_stats');
  need(core.length===TICKS+2,'Faltan diagnosticos del nucleo');
  need(memory.length===TICKS+1,'Faltan diagnosticos de memoria inversa');
  need(core[0].background==='zero'&&core[0].active_qubits===0,'El universo no parte de un fondo implicito');
  need(core.every(s=>s.active_qubits<=WIDTH),'El calculo materializo mas qubits que las celdas');
  need(memory.every(s=>s.model==='inverse-ether-memory'&&s.overrides===0&&s.quantum_bindings===0),
       'La simulacion creo una memoria clasica o enlaces no pedidos');
  return {states,max_active_qubits:Math.max(...core.map(s=>s.active_qubits)),
          max_tableau_bytes:Math.max(...core.map(s=>s.tableau_bytes)),
          max_address_bytes:Math.max(...core.map(s=>s.address_bytes))};
}
function compare(a,b){
  need(a.length===b.length,'Comparacion sobre horizontes distintos');
  let matches=0,wrongBits=0,first=null;
  for(let t=0;t<a.length;t++){
    const mask=a[t]^b[t];
    if(mask===0){matches++;continue;}
    if(first===null)first=t;
    wrongBits+=mask.toString(2).split('1').length-1;
  }
  return {time_points:a.length,matching_points:matches,different_points:a.length-matches,
          incorrect_cell_observations:wrongBits,first_divergence:first};
}
// Una prediccion sellada ANTES de ejecutar el mundo con evento desconocido.
const priorXI=worldXI();
need(!priorXI.includes('Entrada externa'),'Se filtro el evento futuro al pronosticador');
const forecaster=run('01_prediccion_antes_del_evento',priorXI);
const predicted=readWorld(forecaster);
const baseline=compare(predicted.states,oracle());
need(baseline.different_points===0,'La prediccion XI no coincide con el oraculo matematico');

const observed=run('02_evolucion_real_con_evento',worldXI({intervention:true}));
const reality=readWorld(observed);
need(compare(reality.states,oracle({intervention:true})).different_points===0,
     'El mundo observado no coincide con la referencia independiente');
const unknown=compare(predicted.states,reality.states);
need(unknown.first_divergence===EVENT_TICK&&unknown.different_points>0,
     'El experimento no distingue un evento externo desconocido');

const informed=run('03_prediccion_con_evento_conocido',worldXI({intervention:true}));
const conditional=compare(readWorld(informed).states,reality.states);
need(conditional.different_points===0,'El evento conocido sigue siendo impredecible');

const reverse=run('04_reversibilidad',reversibleXI());
const afterReverse=reverse.rows.filter(x=>x.type==='stats').at(-1);
need(afterReverse?.active_qubits===0,'El estado no regreso al fondo y no pudo compactarse');

const quantum=run('05_limite_de_incertidumbre_cuantica',quantumLimitXI());
const q=quantum.rows.filter(x=>x.type==='probz');
need(q.length===2&&q.every(x=>x.probability_one===0.5),
     'La superposicion cuantica fue incorrectamente presentada como certeza');
need(quantum.rows.some(x=>x.type==='expectation'&&x.value===1),
     'La correlacion cuantica esperada no aparece');

const report={
  suite:'DEMONIO_LAPLACE_DIGITAL_ETHER_XI',
  base:'QuantumRuntime nativo + InfiniteCore + Inverse Ether Memory de Mundo X',
  mundo:'Anillo de 16 celdas, leyes locales binarias reversibles (capas alternantes CNOT)',
  estado_inicial:INITIAL,instantes_simulados:TICKS,fecha:'2026-10-27',
  universo_logico:{direccion_remota_digitos:ADDRESSES[15].length,
    qubits_materializados_al_inicio:0,max_qubits_materializados:predicted.max_active_qubits,
    pico_tableau_bytes:predicted.max_tableau_bytes,pico_direcciones_bytes:predicted.max_address_bytes},
  mundo_cerrado_vs_oraculo:baseline,
  evento_externo_desconocido:{instante:EVENT_TICK,celda:EVENT_SITE,...unknown},
  prediccion_con_evento_revelado:conditional,
  reversibilidad:{vuelve_al_fondo_y_libera_componentes:true},
  experimento_cuantico:{probabilidades_medidas_sin_colapso:q.map(x=>x.probability_one),correlacion_ZZ:1},
  hashes_programas:{previo:forecaster.sha256,real:observed.sha256,condicional:informed.sha256},
  trayectoria_antes:predicted.states.map(v=>v.toString(16).padStart(4,'0')),
  trayectoria_observada:reality.states.map(v=>v.toString(16).padStart(4,'0')),
  limites:{mundo_sintetico:true,predice_la_realidad:false,
           capacidad_de_prediccion_financiera_demostrada:false,hardware_cuantico_fisico:false}
};
writeFileSync(join(out,'resultado.json'),JSON.stringify(report,null,2)+'\n','utf8');
console.log(JSON.stringify({
  suite:report.suite,ciudad_digital:'mundo cerrado de 16 celdas',
  exactitud_cerrada:baseline.matching_points+'/'+baseline.time_points,
  divergencia_por_evento_no_observado:unknown.first_divergence,
  prediccion_condicional:conditional.matching_points+'/'+conditional.time_points,
  qubits_maximos:predicted.max_active_qubits,direccion_digitos:ADDRESSES[15].length,
  reversibilidad:true,superposicion_no_disfrazada:true,
  archivo:'resultado.json'
}));
