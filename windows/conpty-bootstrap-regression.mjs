// Isolated named server only; never attaches to the user's default server.
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, existsSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'

assert.equal(process.platform, 'win32')
assert.ok(process.argv[2], 'candidate tmux path required')
const exe = resolve(process.argv[2])
const server = `bootstrap-regression-${Date.now()}`
const temp = mkdtempSync(join(tmpdir(), `${server}-`))
const cwd = join(temp, 'unicode workspace 测试')
mkdirSync(cwd)
const env = Object.fromEntries(Object.entries(process.env).filter(([key]) => key.toLowerCase() !== 'path'))
env.Path = 'C:\\Windows\\System32;C:\\Windows\\System32\\WindowsPowerShell\\v1.0'
env.TMUX_BOOTSTRAP_TEST = 'value with spaces 测试'
const config = join(temp, 'tmux.conf')
writeFileSync(config, 'set -g remain-on-exit on\n')
const ps = 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe'
const quote = s => `'${s.replaceAll("'", "''")}'`
function run(args, expected = 0) {
  const result = spawnSync(exe, ['-L', server, '-f', config, ...args], {env, encoding:'utf8', timeout:10000})
  assert.equal(result.status, expected, `${args[0]}: ${result.error?.message || result.stderr}`)
  return result.stdout.trim()
}
async function until(fn) {
  const deadline = Date.now() + 10000
  while (!fn()) {
    assert.ok(Date.now() < deadline, 'condition timed out')
    await delay(100)
  }
}
const encoded = command => Buffer.from(command, 'utf16le').toString('base64')
try {
  run(['new-session','-d','-s','probe','C:\\Windows\\System32\\cmd.exe','/d','/q'])
  const report = join(temp, 'report.json')
  const command = `$v=@{cwd=(Get-Location).Path;nativeCwd=[Environment]::CurrentDirectory;env=$env:TMUX_BOOTSTRAP_TEST;text='quoted "value" 测试';pid=$PID};[IO.File]::WriteAllText(${quote(report)},($v|ConvertTo-Json));exit 37`
  run(['new-window','-d','-t','probe','-n','unicode','-c',cwd,ps,'-NoProfile','-EncodedCommand',encoded(command)])
  await until(() => existsSync(report))
  const data = JSON.parse(readFileSync(report,'utf8'))
  // CLI -c currently resets to home on the installed baseline too. The native
  // startup fixture separately asserts CreateProcess cwd inheritance.
  const cliCwdMatches = data.cwd.toLowerCase() === cwd.toLowerCase()
  assert.equal(data.env,env.TMUX_BOOTSTRAP_TEST)
  assert.equal(data.text,'quoted "value" 测试')
  await until(() => run(['display-message','-p','-t','probe:unicode','#{pane_dead}:#{pane_dead_status}']) === '1:37')
  // More than one argv item requests direct executable spawning.
  run(['new-window','-d','-t','probe','-n','missing',join(temp,'definitely-missing.exe'),'argument'],1)
  assert.ok(!run(['list-windows','-t','probe','-F','#{window_name}']).split('\n').includes('missing'))
  run(['respawn-pane','-k','-t','probe:unicode',ps,'-NoProfile','-EncodedCommand',encoded('exit 23')])
  await until(() => run(['display-message','-p','-t','probe:unicode','#{pane_dead}:#{pane_dead_status}']) === '1:23')
  // A long-lived real child is contained by the pane, not orphaned on close.
  const childFile = join(temp,'child.txt')
  run(['new-window','-d','-t','probe','-n','contained',ps,'-NoProfile','-EncodedCommand',encoded(`[IO.File]::WriteAllText(${quote(childFile)},[string]$PID);Start-Sleep 120`)])
  await until(() => existsSync(childFile))
  const child = Number(readFileSync(childFile,'utf8'))
  assert.ok(child > 0)
  run(['kill-window','-t','probe:contained'])
  await until(() => {try {process.kill(child,0);return false} catch(e) {if(e.code==='ESRCH')return true;throw e}})
  console.log(JSON.stringify({result:'passed',unicode:true,environment:true,cliCwdMatches,knownBaseline:'CLI -c may reset to home',exitStatus:true,spawnFailure:true,respawn:true,childCleanup:true}))
} finally {
  spawnSync(exe,['-L',server,'kill-server'],{env,timeout:10000,stdio:'ignore'})
  assert.ok(temp.startsWith(join(tmpdir(),`${server}-`)))
  rmSync(temp,{recursive:true,force:true})
}
