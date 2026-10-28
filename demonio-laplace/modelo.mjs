/*
 * Demonio de Laplace digital: mundo de 16 celdas binarias reversibles.
 * Las leyes se ejecutan en el QuantumRuntime XI original de Mundo X.
 * El estado base es |0> en todo el dominio; solo activamos modificaciones.
 * Este archivo genera instrucciones XI, NO implementa otro simulador cuántico.
 */
export const WIDTH=16;
export const TICKS=32;
export const INITIAL=0xA142;
export const EVENT_TICK=9;
export const EVENT_SITE=3;
const FAR='1'+'0'.repeat(100);
export const ADDRESSES=Array.from({length:WIDTH},(_,i)=>i<8?''+i:FAR+(i-8));
export function bit(value,i){return (value>>>i)&1;}
export function flip(value,i){return value^(1<<i);}
const x=i=>'x '+ADDRESSES[i];
const cx=(i,j)=>'cx '+ADDRESSES[i]+' '+ADDRESSES[j];
export function laws(){
  const out=[];
  for(let i=0;i<WIDTH;i+=2)out.push(cx(i,(i+1)%WIDTH));
  for(let i=1;i<WIDTH;i+=2)out.push(cx(i,(i+1)%WIDTH));
  return out;
}
export function worldXI({intervention=false}={}){
  const out=['# Universo finito con fondo Ether implicito, no copia densa.','background zero','stats'];
  for(let i=0;i<WIDTH;i++)if(bit(INITIAL,i))out.push(x(i));
  for(let t=0;t<=TICKS;t++){
    if(t>0){
      if(intervention&&t===EVENT_TICK)out.push('# Entrada externa no disponible al predictor',x(EVENT_SITE));
      out.push(...laws());
    }
    out.push('# Observar solamente despues de la evolucion t='+t);
    for(const address of ADDRESSES)out.push('probz '+address);
    out.push('stats','mem_stats');
  }
  return out.join('\n')+'\n';
}
export function reversibleXI(){
  const out=['background zero','stats'];
  for(let i=0;i<WIDTH;i++)if(bit(INITIAL,i))out.push(x(i));
  const sequence=laws();
  out.push(...sequence,...sequence.slice().reverse());
  for(let i=0;i<WIDTH;i++)out.push('assert probz '+ADDRESSES[i]+' '+bit(INITIAL,i));
  for(let i=0;i<WIDTH;i++)if(bit(INITIAL,i))out.push(x(i));
  out.push('compact','stats');
  return out.join('\n')+'\n';
}
export function quantumLimitXI(){
  return ['background zero','h '+ADDRESSES[0],cx(0,15),'probz '+ADDRESSES[0],
          'probz '+ADDRESSES[15],'expect Z:'+ADDRESSES[0]+' Z:'+ADDRESSES[15],
          'stats'].join('\n')+'\n';
}
/* Independent classical Boolean oracle for the reversible circuit.
 * This does not generate the actual forecast: XI is the predictive engine.
 */
export function oracle({intervention=false}={}){
  let state=INITIAL;const trajectory=[state];
  for(let t=1;t<=TICKS;t++){
    if(intervention&&t===EVENT_TICK)state=flip(state,EVENT_SITE);
    for(let i=0;i<WIDTH;i+=2)if(bit(state,i))state=flip(state,(i+1)%WIDTH);
    for(let i=1;i<WIDTH;i+=2)if(bit(state,i))state=flip(state,(i+1)%WIDTH);
    trajectory.push(state);
  }
  return trajectory;
}
