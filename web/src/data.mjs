export const schedulers = {
 v0:{name:'Thread batches',color:'#0866ff',description:'Creates bounded batches of new threads.'},
 v1:{name:'Shared FIFO',color:'#0daaa5',description:'One shared queue, protected by a mutex.'},
 v2:{name:'Local FIFO',color:'#9b59ef',description:'A separate FIFO queue for each worker.'},
 v3:{name:'Work stealing',color:'#ed922b',description:'Owner LIFO and thief FIFO deques, protected by mutexes.'},
 v4:{name:'Atomic stacks',color:'#101c40',description:'Lock-free stack operations; lifecycle and accounting still use locks.'},
 openmp:{name:'Dynamic scheduling',color:'#919bab',description:'External OpenMP baseline using dynamic parallel-for scheduling.'},
 serial:{name:'Serial',color:'#c44f75',description:'Sequential reference processing.'}
};
export const metrics={throughput:{label:'Throughput (tasks/sec)',short:'Throughput',unit:'tasks/s',better:'Higher is better'},p99_us:{label:'p99 response latency (µs)',short:'p99 response latency',unit:'µs',better:'Lower is better'},speedup:{label:'Speedup vs. serial',short:'Speedup',unit:'×',better:'Higher is better'},cpu_percent:{label:'Process CPU (%)',short:'Process CPU',unit:'%',better:'100% = one logical CPU'},efficiency:{label:'Parallel efficiency',short:'Parallel efficiency',unit:'',better:'Speedup ÷ workers'}};
function cells(text){
 const rows=[];let row=[],cell='',quote=false;
 for(let i=0;i<text.length;i++){const c=text[i];if(c==='"'){if(quote&&text[i+1]==='"'){cell+='"';i++;}else quote=!quote;}else if(c===','&&!quote){row.push(cell);cell='';}else if((c==='\n'||c==='\r')&&!quote){if(c==='\r'&&text[i+1]==='\n')i++;row.push(cell);if(row.some(Boolean))rows.push(row);row=[];cell='';}else cell+=c;}
 if(quote)throw Error('The CSV has an unclosed quoted field.');
 row.push(cell);if(row.some(Boolean))rows.push(row);return rows;
}
export function parseCSV(text){
 const [headers,...lines]=cells(text.replace(/^\uFEFF/,''));
 const required=['scheduler','threads','tasks','workload','mode','throughput','p99_us','completed','missing','duplicates','mismatches','lidar_points','fft_size','seed','noise','speed_hz','wheel_hz','temperature_hz','can_hz','lidar_hz','deadline_us'];
 if(!headers||required.some(x=>!headers.includes(x)))throw Error('This file is not a compatible vehicle_benchmark summary CSV.');
 if(!lines.length)throw Error('This CSV contains no measured runs.');
 return lines.map((line,i)=>{
  if(line.length!==headers.length)throw Error(`Row ${i+2} has the wrong number of columns.`);
  const r={id:i};headers.forEach((h,j)=>r[h]=['scheduler','workload','mode'].includes(h)?line[j]:line[j]===''?null:Number(line[j]));
  for(const k of required.filter(k=>!['scheduler','workload','mode'].includes(k))){if(!Number.isFinite(r[k])||r[k]<0)throw Error(`Row ${i+2} has an invalid ${k}.`);}
  if(r.threads<1||r.tasks<1||!Number.isInteger(r.threads))throw Error(`Row ${i+2} has invalid task or worker counts.`);
  if(!schedulers[r.scheduler]||!['batch','streaming'].includes(r.mode))throw Error(`Row ${i+2} has an unsupported scheduler or mode.`);
  r.errors=r.missing+r.duplicates+r.mismatches;
  r.scenario=scenarioKey(r);return r;
 });
}
export function scenarioKey(r){return [r.tasks,r.lidar_points,r.fft_size,r.seed,r.noise,r.speed_hz,r.wheel_hz,r.temperature_hz,r.can_hz,r.lidar_hz,r.deadline_us].join('|');}
export function aggregate(rows,metric){
 const groups=new Map();for(const r of rows){if(r.errors||!Number.isFinite(r[metric])||(metric==='cpu_percent'&&!r.cpu_available))continue;const key=`${r.scheduler}:${r.threads}`;if(!groups.has(key))groups.set(key,{scheduler:r.scheduler,workers:r.threads,values:[]});groups.get(key).values.push(r[metric]);}
 return [...groups.values()].map(g=>{const mean=g.values.reduce((a,b)=>a+b,0)/g.values.length;return {...g,mean,sd:g.values.length>1?Math.sqrt(g.values.reduce((a,b)=>a+(b-mean)**2,0)/(g.values.length-1)):0};});
}
export const number=(n,d=0)=>Number.isFinite(n)?n.toLocaleString('en-US',{maximumFractionDigits:d}):'—';
