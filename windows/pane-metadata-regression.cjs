// Dedicated -L servers only. Never attaches, types into or detaches user panes.
const { execFileSync } = require('node:child_process');
const { randomUUID } = require('node:crypto');
const assert = require('node:assert/strict');
const [exe, iterationsArg = '30'] = process.argv.slice(2);
if (!exe) throw new Error('Usage: node pane-metadata-regression.cjs TMUX_EXE [ITERATIONS]');
const iterations = Number(iterationsArg);
if (!Number.isInteger(iterations) || iterations < 1 || iterations > 1000) throw new Error('Iterations must be between 1 and 1000');
const label = 'subtitle-regression-' + randomUUID();
const run = args => execFileSync(exe, ['-L', label, ...args], {encoding:'utf8',timeout:15000,windowsHide:true});
const FS = '::TMUXHUB_FIELD::', RS = '::TMUXHUB_RECORD::';
const names = ['session_name','window_index','window_name','window_active','pane_index','pane_id','pane_active','pane_dead','pane_current_command','pane_current_path','pane_title','pane_width','pane_height','pane_in_mode','scroll_position','history_size','session_attached','session_activity','window_activity'];
const fmt = names.map(n => '#{' + n + '}').join(FS) + RS;
let started = false;
try {
    for (let i = 0; i < 9; i++) {run(['new-session','-d','-s','probe'+i,'powershell.exe -NoLogo -NoProfile -Command cmd.exe']); started = true;}
    const initial = run(['list-panes','-a','-F','#{pane_id}|#{pane_pid}']);
    let incomplete = 0, malformed = 0, min = Infinity, max = 0, totalMs = 0;
    for (let i = 0; i < iterations; i++) {
        const t = Date.now();
        const out = run(['list-panes','-a','-F',fmt]);
        const elapsed = Date.now()-t; min = Math.min(min,elapsed);max=Math.max(max,elapsed);totalMs+=elapsed;
        const records = out.split(RS); const tail = records.pop();
        const rows = records.filter(x=>x.trim()).map(x=>x.trim().split(FS));
        if (rows.length !== 9 || tail.trim()) incomplete++;
        malformed += rows.filter(p=>p.length!==19).length;
    }
    assert.equal(run(['list-panes','-a','-F','#{pane_id}|#{pane_pid}']), initial, 'test panes must survive every query');
    console.log(JSON.stringify({label,iterations,incomplete,malformed,minMs:min,maxMs:max,meanMs:Math.round(totalMs/iterations),panePidsUnchanged:true}));
    if (incomplete || malformed) process.exitCode=1;
} finally {
    if(started) run(['kill-server']); // exact isolated label, never the default endpoint
}
