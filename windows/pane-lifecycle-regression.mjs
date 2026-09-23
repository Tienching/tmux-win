// Exercise natural pane exit without ever touching the default/user server.
import assert from 'node:assert/strict'
import { randomUUID } from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { mkdtempSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'

assert.equal(process.platform, 'win32')
assert.ok(process.argv[2], 'candidate tmux path required')
const exe = resolve(process.argv[2])
const iterations = Number(process.argv[3] || 40)
assert.ok(Number.isInteger(iterations) && iterations > 0 && iterations <= 1000)
const server = `pane-lifecycle-${randomUUID()}`
const evidence = mkdtempSync(join(tmpdir(), `${server}-`))
// Debug logs record IDENTIFY_ENVIRON: never inherit arbitrary user/CI secrets.
const allowed = new Set(['systemroot', 'windir', 'comspec', 'temp', 'tmp',
  'localappdata', 'appdata', 'userprofile', 'homedrive', 'homepath', 'username'])
const env = Object.fromEntries(Object.entries(process.env).filter(([key]) => allowed.has(key.toLowerCase())))
const system = process.env.SystemRoot || 'C:\\Windows'
env.Path = [join(system, 'System32'), join(system, 'System32', 'WindowsPowerShell', 'v1.0')].join(';')
const shell = [join(system, 'System32', 'cmd.exe'), '/d', '/q']
let failed = false
let started = false
function run(args) {
  const result = spawnSync(exe, ['-vv', '-L', server, '-f', 'NUL', ...args], {
    cwd: evidence, env, encoding: 'utf8', timeout: 10000, windowsHide: true,
  })
  assert.equal(result.status, 0, `${args.join(' ')}: ${result.error?.message || result.stderr}`)
  return result.stdout.trim()
}
async function until(fn, label) {
  const deadline = Date.now() + 10000
  while (!fn()) {
    assert.ok(Date.now() < deadline, label)
    await delay(20)
  }
}
const windows = () => run(['list-windows', '-t', 'probe', '-F', '#{window_name}']).split(/\r?\n/)
async function gone(name) {
  await until(() => !windows().includes(name), `${name} did not exit`)
  // Cross at least two 10ms poll deadlines after the pane has been freed.
  await delay(30)
}
console.log(JSON.stringify({ server, evidence, iterations }))
try {
  run(['new-session', '-d', '-s', 'probe', '-n', 'anchor', ...shell])
  started = true
  const identity = run(['display-message', '-p', '-t', 'probe:anchor', '#{pid}:#{pane_id}:#{pane_pid}'])
  for (let i = 0; i < iterations; i++) {
    run(['new-window', '-d', '-t', 'probe', '-n', 'swap', ...shell])
    run(['split-window', '-h', '-t', 'probe:swap.0', ...shell])
    run(['swap-pane', '-s', 'probe:swap.0', '-t', 'probe:swap.1'])
    const panes = run(['list-panes', '-t', 'probe:swap', '-F', '#{pane_id}']).split(/\r?\n/)
    // Stable IDs remain correct if the first exit renumbers the second pane.
    for (const pane of panes) run(['send-keys', '-t', pane, 'exit', 'Enter'])
    await gone('swap')

    run(['new-window', '-d', '-t', 'probe', '-n', 'natural', ...shell])
    run(['send-keys', '-t', 'probe:natural', 'exit', 'Enter'])
    await gone('natural')

    run(['new-window', '-d', '-t', 'probe', '-n', 'retained', ...shell])
    run(['set-option', '-w', '-t', 'probe:retained', 'remain-on-exit', 'on'])
    run(['send-keys', '-t', 'probe:retained', 'exit', 'Enter'])
    await until(() => run(['display-message', '-p', '-t', 'probe:retained', '#{pane_dead}']) === '1', 'pane not retained')
    await until(() => Number(run(['display-message', '-p', '-t', 'probe:retained', '#{pane_dead_time}'])) > 0, 'retained pane cleanup not complete')
    if (i % 2 === 0) {
      run(['respawn-pane', '-t', 'probe:retained', ...shell])
      assert.equal(run(['display-message', '-p', '-t', 'probe:retained', '#{pane_dead}']), '0')
      run(['set-option', '-w', '-t', 'probe:retained', 'remain-on-exit', 'off'])
      run(['send-keys', '-t', 'probe:retained', 'exit', 'Enter'])
    } else {
      // PTY was already cleared: final pane destruction must still cancel its timer.
      run(['kill-window', '-t', 'probe:retained'])
    }
    await gone('retained')
    assert.equal(run(['display-message', '-p', '-t', 'probe:anchor', '#{pid}:#{pane_id}:#{pane_pid}']), identity, 'server or anchor pane changed')
    if ((i + 1) % 10 === 0) console.log(`pane lifecycle ${i + 1}/${iterations} PASS`)
  }
  console.log('Windows pane lifecycle regression passed')
} catch (error) {
  failed = true
  console.error(error.stack)
} finally {
  // Try even if new-session failed after creating its server. The UUID label
  // cannot target an existing user server; absence is fine if start failed.
  try { run(['kill-server']) } catch (error) {
    if (started) { failed = true; console.error(`owned server cleanup: ${error.message}`) }
  }
}
// Retain bounded, test-only diagnostics even on failure; no credential data.
process.exitCode = failed ? 1 : 0
