const assert = require('node:assert/strict');
const cp = require('node:child_process');
const { randomUUID } = require('node:crypto');
const { resolve } = require('node:path');
const pty = require(process.argv[2]);
const tmux = resolve(process.argv[3]);
const socket = 'nested-keys-' + randomUUID();
const sleep = ms => new Promise(done => setTimeout(done, ms));
const useConptyDll = process.argv[4] === 'bundled';
const esc = '\x1b';

function command(...args) {
  return cp.execFileSync(tmux, ['-L', socket, ...args], { encoding: 'utf8', windowsHide: true });
}

async function until(check) {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    const result = check();
    if (result) return result;
    await sleep(100);
  }
  throw Error('Timed out waiting for the pane or attached terminal');
}

const source = `
[Console]::WriteLine('NESTED_READY')
for ($n = 0; $n -lt 5; ++$n) {
  $key = [Console]::ReadKey($true)
  [Console]::WriteLine(('NESTED {0} {1:X2} {2}' -f $key.Key,[int]$key.KeyChar,[int]$key.Modifiers))
}
Start-Sleep -Seconds 30`;
const powershell = 'powershell.exe -NoProfile -EncodedCommand ' + Buffer.from(source,'utf16le').toString('base64');
const capture = () => command('capture-pane','-p','-t','probe:0.0');
const clients = () => command('list-clients');
const event = (vk, scan, character, control) => [1, 0].map(down => `${esc}[${vk};${scan};${character};${down};${control};1_`).join('');

async function verify() {
  let terminal;
  try {
    command('-f','NUL','new-session','-d','-s','probe',powershell);
    await until(() => capture().includes('NESTED_READY'));
    terminal = pty.spawn(tmux, ['-L',socket,'attach','-t','probe'],
      { name:'xterm-256color',cols:100,rows:30,useConpty:true,useConptyDll });
    let output = '';
    terminal.onData(chunk => { output += chunk; });
    await until(() => clients().includes('probe') && output.includes('NESTED_READY'));
    terminal.write(event(13,28,13,0) + event(13,28,13,8) + event(74,36,10,8) +
      `${esc}[13;5u` + event(13,28,13,16));
    const read = await until(() => {
      const lines = capture().match(/^NESTED .+$/gm) || [];
      return lines.length >= 5 ? lines : null;
    });
    assert.deepEqual(read, ['NESTED Enter 0D 0','NESTED Enter 0D 4','NESTED J 0A 4',
      'NESTED Enter 0A 4','NESTED Enter 0D 2']);
    console.log(`${useConptyDll ? 'Bundled' : 'System'} ConPTY nested attach and CSI-u input verified`);
  } finally {
    try { command('kill-server'); } catch {}
    if (terminal) terminal.kill();
  }
}
verify().catch(error => { console.error(error); process.exitCode = 1; });
