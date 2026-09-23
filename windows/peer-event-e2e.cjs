// Isolated native attach/detach test. No default socket or user session is used.
// Usage: node peer-event-e2e.cjs <tmux.exe> <node-pty module directory>
const cp = require('node:child_process');
const assert = require('node:assert/strict');
const path = require('node:path');
const bin = path.resolve(process.argv[2]);
const pty = require(path.resolve(process.argv[3]));
const label = `codex-peer-e2e-${process.pid}-${Date.now()}`;
const env = {...process.env}; delete env.TMUX;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const clients = new Set();
const timings = [];
const switchTimings = [];
const cancellation = {requested:0, cancelled:0, completedBeforeCancel:0};
let serverPid;
let started=false;
function run(args, cancelMs = 0) {
  return new Promise((resolve, reject) => {
    const start = Date.now();
    let cancelled=false;
    if(cancelMs) cancellation.requested++;
    const child = cp.execFile(bin, ['-L',label,'-f','NUL',...args],
      {env,windowsHide:true,timeout:5000}, (err,stdout,stderr) => {
        if (cancelMs) {
          if (cancelled && err?.killed) { cancellation.cancelled++; return resolve(); }
          if (!err) { cancellation.completedBeforeCancel++; return resolve(); }
        }
        if (err) return reject(new Error(`${args[0]}: ${err.message}; ${stderr}`));
        timings.push(Date.now()-start); resolve(stdout.trim());
      });
    if (cancelMs) {
      const timer = setTimeout(()=>{cancelled=child.kill();},cancelMs);
      child.once('exit',()=>clearTimeout(timer));
    }
  });
}
async function until(check, name) {
  const end=Date.now()+5000;
  while(Date.now()<end) { if(await check()) return; await sleep(30); }
  throw new Error(`Timeout: ${name}`);
}
async function attach(session, index) {
  const start=Date.now();
  const term=pty.spawn(bin,['-L',label,'-f','NUL','attach-session','-t',session],
    {env,cwd:process.cwd(),name:'xterm-256color',cols:100,rows:28});
  clients.add(term); let output='',exited=false;
  term.onData(s=>{output=(output+s).slice(-32768);});
  term.onExit(()=>{exited=true;clients.delete(term);});
  await until(()=>output.length>0,`attach ${index} first output`);
  const marker=`PEER_E2E_${index}_OK`;
  term.write(`echo ${marker}\r`);
  // Query actual pane contents as well as the attached terminal stream.
  await until(async()=> (await run(['capture-pane','-p','-t',session,'-S','-40']))
    .split(/\r?\n/).some(line=>line.trim()===marker),`input/output ${index}`);
  await until(()=>output.includes(marker),`attached output ${index}`);
  switchTimings.push(Date.now()-start);
  if(index%2) process.kill(term.pid); else await run(['detach-client','-s',session]);
  await until(()=>exited,`detach ${index}`);
}
(async()=>{
 let result;
 try {
  started=true;
  await run(['new-session','-d','-s','one','powershell.exe -NoLogo -NoProfile -NoExit']);
  await run(['new-session','-d','-s','two','powershell.exe -NoLogo -NoProfile -NoExit']);
  serverPid=Number(await run(['display-message','-p','#{pid}']));
  const panes=await run(['list-panes','-a','-t','one','-F','#{session_name}:#{pane_pid}:#{pane_dead}']);
  assert(panes.split(/\r?\n/).length===2 && panes.split(/\r?\n/).every(s=>s.endsWith(':0')));
  console.log(JSON.stringify({label,serverPid,panes}));
  for(let i=0;i<30;i++) {
    await attach(i%2?'one':'two',i);
    if(i%3===0) await Promise.all([run(['list-sessions'],15),run(['list-panes','-a','-t','one'],35)]);
  }
  assert.equal(await run(['list-panes','-a','-t','one','-F','#{session_name}:#{pane_pid}:#{pane_dead}']),panes);
  await until(async()=> (await run(['list-clients'])).length===0,'client cleanup');
  timings.sort((a,b)=>a-b);
  switchTimings.sort((a,b)=>a-b);
  assert.equal(cancellation.requested,20);
  assert(cancellation.cancelled>0,'cancellation path must actually be exercised');
  result={result:'PASS',attachInputDetachCycles:30,cancellation,
    panePidsPreserved:true,commands:timings.length,medianMs:timings[Math.floor(timings.length/2)],
    p95Ms:timings[Math.floor(timings.length*.95)],maxMs:timings.at(-1),
    attachThroughVerifiedInputMedianMs:switchTimings[15],
    attachThroughVerifiedInputMaxMs:switchTimings.at(-1)};
 } finally {
  for(const term of clients) try {term.kill();} catch{}
  if(started) await run(['kill-server']);
 }
 console.log(JSON.stringify(result));
// node-pty workers can keep Node alive after all owned sessions are gone.
})().then(()=>process.exit(0),e=>{console.error(e.message);process.exit(1);});
