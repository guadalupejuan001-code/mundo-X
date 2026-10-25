import {GRAVITATIONAL_CONSTANT} from './dynamics.js';
const $=id=>document.getElementById(id);
const format=(n,d=2)=>n.toLocaleString('es',{maximumFractionDigits:d,minimumFractionDigits:d});

export function labUI(lab,apply) {
  for(const [id,command] of Object.entries({'lab-magnetic':'LAB_MAGNETIC','lab-gravity':'LAB_GRAVITY','lab-both':'LAB_BOTH','lab-play':'LAB_PLAY','lab-reset':'LAB_RESET','lab-b-minus':'MAGNETIC_DOWN','lab-b-plus':'MAGNETIC_UP','lab-g-minus':'GRAVITY_DOWN','lab-g-plus':'GRAVITY_UP'}))$(id).onclick=()=>apply(command);
  return ()=>{
    for(const mode of ['magnetic','gravity','both'])$('lab-'+mode).setAttribute('aria-pressed',String(lab.mode===mode));
    $('lab-play').textContent=lab.running?'Pausar objetos':'Mover objetos';
    $('lab-b').textContent='×'+format(lab.magneticScale,1);
    $('lab-g').textContent='×'+format(lab.gravityScale,1);
    $('lab-status').textContent=lab.error||`${lab.particles.length} partículas virtuales · ${format(lab.time,1)} s simulados · ${lab.running?'en movimiento':'pausa'}`;
    $('lab-mass').textContent=`Fuente gravitatoria virtual: ${format(lab.mu*lab.gravityScale/GRAVITATIONAL_CONSTANT/1e6,2)} millones de kg equivalentes. Esfera permeable; paredes reflectantes.`;
  };
}

export function drawLab(lab,ctx,project,line) {
  if(!lab.particles.length)return;
  if(lab.mode!=='magnetic'&&lab.gravityScale>0){
    for(const p of lab.particles){
      const direction=lab.center.map((v,i)=>v-p.r[i]),length=Math.hypot(...direction);
      if(length>.0001)line(p.r,p.r.map((v,i)=>v+direction[i]/length*.018),'#b5e8a166',1);
    }
    const c=project(lab.center),radius=Math.max(3,lab.radius*c.scale);
    ctx.beginPath();ctx.arc(c.x,c.y,radius,0,Math.PI*2);ctx.fillStyle='#a7e38a20';ctx.fill();ctx.strokeStyle='#b5e8a1aa';ctx.stroke();
  }
  for(const p of [...lab.particles].sort((a,b)=>project(a.r).depth-project(b.r).depth)){
    ctx.beginPath();p.trail.forEach((r,i)=>{const q=project(r);if(i)ctx.lineTo(q.x,q.y);else ctx.moveTo(q.x,q.y);});
    ctx.strokeStyle='#ff657d88';ctx.lineWidth=1.5;ctx.stroke();
    const q=project(p.r);ctx.beginPath();ctx.arc(q.x,q.y,4,0,Math.PI*2);ctx.fillStyle='#ffe9ed';ctx.fill();ctx.strokeStyle='#ff4a50';ctx.stroke();
  }
}

export function physicalUI() {
  let latest=null,received=0;
  function update(){
    const elapsed=performance.now()-received;
    const active=latest&&latest.mode==='measuring'&&elapsed<3500;
    $('physical-status').textContent=active?(latest.emulator?'SENSORES DE EMULADOR':'LECTURAS DEL RELOJ'):latest?.mode==='stopped'?'Medición detenida':'Sin lecturas recientes';
    for(const [key,unit] of [['magnetic','µT'],['gravity','m/s²']]){
      const values=active&&latest[key]&&latest[key+'AgeMs']+elapsed<3500?latest[key]:null;
      $(key+'-live').textContent=values?format(Math.hypot(...values))+' '+unit:'—';
      $(key+'-vector').textContent=values?'XYZ: '+values.map(v=>format(v)).join(' · '):'Sin muestra válida reciente';
      $(key+'-accuracy').textContent=values?['Sin fiabilidad','Baja','Media','Alta'][latest[key+'Accuracy']]||'Sin precisión declarada':'';
    }
    $('physical-origin').textContent=active?`${latest.model} · ejes del reloj · no son posiciones del mapa`:'Activa MEDIR SENSORES en el mando del reloj y mantén ambas apps abiertas.';
  }
  window.MundoXPhysical={receive(packet){latest=packet;received=performance.now();update();},clear(){latest=null;update();}};
  setInterval(()=>{if(latest&&!document.hidden)update();},500);update();
}
