param(
	[string]$Repository = "",
	[string]$WorkflowName = "Windows MinGW",
	[string]$WorkflowPath = ".github/workflows/windows-mingw.yml",
	[string]$HeadSha = "",
	[string]$Branch = "",
	[string]$Output = "",
	[int]$TimeoutSeconds = 30,
	[int]$RunLimit = 100,
	[switch]$RequireGreen
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Root "dist\hosted-ci-audit.json"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

function Get-Sha256([string]$Path) {
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).
	    Hash.ToLowerInvariant()
}

function Get-GitHubRepositoryFromRemote {
	try {
		$url = (& git remote get-url origin 2>$null).Trim()
	} catch {
		return ""
	}
	if ($url -match '^https://github\.com/([^/]+/[^/.]+)(\.git)?$') {
		return $Matches[1]
	}
	if ($url -match '^git@github\.com:([^/]+/[^/.]+)(\.git)?$') {
		return $Matches[1]
	}
	return ""
}

function Invoke-GitHubApi([string]$Uri) {
	$headers = @{
		"User-Agent" = "tmux-win32-hosted-ci-audit"
		"Accept" = "application/vnd.github+json"
	}
	$token = $env:GH_TOKEN
	if ([string]::IsNullOrWhiteSpace($token)) {
		$token = $env:GITHUB_TOKEN
	}
	if (-not [string]::IsNullOrWhiteSpace($token)) {
		$headers["Authorization"] = "Bearer $token"
	}
	return Invoke-RestMethod -Uri $Uri -TimeoutSec $TimeoutSeconds `
	    -Headers $headers
}

function Test-GitHubTokenPresent {
	return (-not [string]::IsNullOrWhiteSpace($env:GH_TOKEN)) -or
	    (-not [string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN))
}

function Invoke-GitText([string[]]$Arguments) {
	$oldErrorActionPreference = $ErrorActionPreference
	try {
		$ErrorActionPreference = "Continue"
		$output = & git @Arguments 2>$null
		if ($LASTEXITCODE -ne 0) {
			return $null
		}
		return $output
	} finally {
		$ErrorActionPreference = $oldErrorActionPreference
	}
}

function Invoke-GitResult([string[]]$Arguments) {
	$oldErrorActionPreference = $ErrorActionPreference
	try {
		$ErrorActionPreference = "Continue"
		$output = & git @Arguments 2>$null
		return [pscustomobject]@{
		    Succeeded = ($LASTEXITCODE -eq 0)
		    Lines = @($output)
		}
	} finally {
		$ErrorActionPreference = $oldErrorActionPreference
	}
}

function Add-QueryParameter([string]$Uri, [string]$Name, [string]$Value) {
	if ([string]::IsNullOrWhiteSpace($Value)) {
		return $Uri
	}
	$separator = $(if ($Uri.Contains("?")) { "&" } else { "?" })
	return ("{0}{1}{2}={3}" -f $Uri, $separator,
	    [Uri]::EscapeDataString($Name),
	    [Uri]::EscapeDataString($Value))
}

function Get-ResponseHeader([object]$Response, [string]$Name) {
	if ($null -eq $Response -or $null -eq $Response.Headers) {
		return ""
	}
	try {
		$value = $Response.Headers[$Name]
		if ($null -eq $value) {
			return ""
		}
		if ($value -is [array]) {
			return [string]$value[0]
		}
		return [string]$value
	} catch {
		return ""
	}
}

function Get-ExceptionDetail([System.Management.Automation.ErrorRecord]$ErrorRecord) {
	$response = $ErrorRecord.Exception.Response
	$message = [string]$ErrorRecord.Exception.Message
	if ($response -ne $null -and
	    $response.PSObject.Properties.Name -contains "StatusCode") {
		$detail = ("HTTP {0} {1}" -f [int]$response.StatusCode,
		    $response.StatusDescription)
		$remaining = Get-ResponseHeader $response "X-RateLimit-Remaining"
		$reset = Get-ResponseHeader $response "X-RateLimit-Reset"
		if ($remaining -eq "0" -or $message -like "*rate limit*") {
			$detail += "; GitHub API rate limit reached"
			if (-not (Test-GitHubTokenPresent)) {
				$detail += "; set GH_TOKEN or GITHUB_TOKEN for authenticated requests"
			}
			if (-not [string]::IsNullOrWhiteSpace($reset)) {
				$detail += "; reset_epoch=$reset"
			}
		}
		return $detail
	}
	return $message
}

if ([string]::IsNullOrWhiteSpace($Repository)) {
	$Repository = Get-GitHubRepositoryFromRemote
}
if ([string]::IsNullOrWhiteSpace($Repository)) {
	throw "could not determine GitHub repository from origin remote"
}
if ([string]::IsNullOrWhiteSpace($HeadSha)) {
	$HeadSha = $env:GITHUB_SHA
}
if ([string]::IsNullOrWhiteSpace($Branch)) {
	$Branch = (Invoke-GitText @("branch", "--show-current") | Select-Object -First 1)
}
if ($RunLimit -lt 1) {
	$RunLimit = 1
}
if ($RunLimit -gt 100) {
	$RunLimit = 100
}
$authenticated = Test-GitHubTokenPresent
$localWorkflowPath = ""
if (-not [string]::IsNullOrWhiteSpace($WorkflowPath)) {
	$localWorkflowPath = $WorkflowPath.Replace("/",
	    [string][System.IO.Path]::DirectorySeparatorChar)
	if (-not [System.IO.Path]::IsPathRooted($localWorkflowPath)) {
		$localWorkflowPath = Join-Path $Root $localWorkflowPath
	}
	$localWorkflowPath = [System.IO.Path]::GetFullPath($localWorkflowPath)
}
$localWorkflowExists = -not [string]::IsNullOrWhiteSpace($localWorkflowPath) -and
    (Test-Path -LiteralPath $localWorkflowPath -PathType Leaf)
$localWorkflowSha256 = $(if ($localWorkflowExists) {
    Get-Sha256 $localWorkflowPath
} else { "" })
$remoteName = "origin"
$remoteBranchChecked = $false
$remoteBranchExists = $false
$remoteBranchHeadSha = ""
$remoteBranchDetail = ""
if (-not [string]::IsNullOrWhiteSpace($Branch)) {
	$remoteBranchChecked = $true
	$remoteBranch = Invoke-GitResult @(
	    "ls-remote", "--heads", $remoteName, $Branch)
	if (-not $remoteBranch.Succeeded) {
		$remoteBranchDetail =
		    "could not query remote branch '$remoteName/$Branch'"
	} elseif ($remoteBranch.Lines.Count -eq 0) {
		$remoteBranchDetail =
		    "branch '$Branch' is not present on remote '$remoteName'"
	} else {
		$remoteBranchExists = $true
		$remoteBranchLine = [string]($remoteBranch.Lines |
		    Select-Object -First 1)
		$remoteBranchHeadSha = ($remoteBranchLine -split "\s+")[0]
		$remoteBranchDetail =
		    "remote '$remoteName/$Branch' head=$remoteBranchHeadSha"
	}
}

$status = "failed"
$workflow = $null
$runs = @()
$candidateRuns = @()
$greenRun = $null
$detail = ""

if ($remoteBranchChecked -and -not $remoteBranchExists) {
	$status = "branch_not_pushed"
	$detail = $remoteBranchDetail
} elseif ($remoteBranchExists -and
    -not [string]::IsNullOrWhiteSpace($HeadSha) -and
    $remoteBranchHeadSha -ne $HeadSha) {
	$status = "remote_head_mismatch"
	$detail = ("remote branch head {0} does not match local head {1}" -f `
	    $remoteBranchHeadSha, $HeadSha)
} else {
	try {
	$workflowResponse = Invoke-GitHubApi `
	    "https://api.github.com/repos/$Repository/actions/workflows"
	$workflows = @($workflowResponse.workflows)
	$workflow = @($workflows | Where-Object {
	    $_.name -eq $WorkflowName -or $_.path -eq $WorkflowPath
	} | Select-Object -First 1)
	if ($workflow.Count -eq 0) {
		$status = "missing_workflow"
		$detail = "workflow not found by name '$WorkflowName' or path '$WorkflowPath'"
		if ($localWorkflowExists) {
			$detail += "; local workflow file exists but is not visible in repository '$Repository'"
		}
	} else {
		$workflow = $workflow[0]
		$runsUri = "https://api.github.com/repos/$Repository/actions/workflows/$($workflow.id)/runs?per_page=$RunLimit"
		$runsUri = Add-QueryParameter $runsUri "branch" $Branch
		$runResponse = Invoke-GitHubApi $runsUri
		$runs = @($runResponse.workflow_runs)
		$candidateRuns = $runs
		if (-not [string]::IsNullOrWhiteSpace($HeadSha)) {
			$candidateRuns = @($candidateRuns | Where-Object {
			    $_.head_sha -eq $HeadSha
			})
		}
		$greenRun = @($candidateRuns | Where-Object {
		    $_.status -eq "completed" -and $_.conclusion -eq "success"
		} | Select-Object -First 1)
		if ($greenRun.Count -gt 0) {
			$greenRun = $greenRun[0]
			$status = "passed"
			$detail = $greenRun.html_url
		} elseif ($candidateRuns.Count -eq 0 -and
		    -not [string]::IsNullOrWhiteSpace($HeadSha)) {
			$status = "no_run_for_head"
			$detail = ("workflow exists but no run for head SHA {0} was found in the latest {1} runs" -f `
			    $HeadSha, $RunLimit)
		} elseif (-not [string]::IsNullOrWhiteSpace($HeadSha)) {
			$status = "no_green_run_for_head"
			$detail = ("workflow has runs for head SHA {0}, but no successful completed run was found" -f `
			    $HeadSha)
		} else {
			$status = "no_green_run"
			$detail = ("workflow exists but no successful completed run was found in the latest {0} runs" -f `
			    $RunLimit)
		}
	}
} catch {
	$status = "blocked"
	$detail = Get-ExceptionDetail $_
}
}

$summary = [pscustomobject]@{
	GeneratedUtc = [DateTime]::UtcNow.ToString("o")
	Repository = $Repository
	WorkflowName = $WorkflowName
	WorkflowPath = $WorkflowPath
	HeadSha = $HeadSha
	Branch = $Branch
	RunLimit = $RunLimit
	Authenticated = $authenticated
	LocalWorkflowPath = $localWorkflowPath
	LocalWorkflowExists = $localWorkflowExists
	LocalWorkflowSha256 = $localWorkflowSha256
	RemoteName = $remoteName
	RemoteBranchChecked = $remoteBranchChecked
	RemoteBranchExists = $remoteBranchExists
	RemoteBranchHeadSha = $remoteBranchHeadSha
	RemoteBranchDetail = $remoteBranchDetail
	Status = $status
	Detail = $detail
	Workflow = $(if ($null -ne $workflow -and $workflow.Count -ne 0) {
	    [pscustomobject]@{
		Id = $workflow.id
		Name = $workflow.name
		Path = $workflow.path
		State = $workflow.state
		Url = $workflow.html_url
	    }
	} else { $null })
	GreenRun = $(if ($null -ne $greenRun -and $greenRun.Count -ne 0) {
	    [pscustomobject]@{
		Id = $greenRun.id
		Name = $greenRun.name
		Status = $greenRun.status
		Conclusion = $greenRun.conclusion
		HeadBranch = $greenRun.head_branch
		HeadSha = $greenRun.head_sha
		Url = $greenRun.html_url
		CreatedAt = $greenRun.created_at
		UpdatedAt = $greenRun.updated_at
	    }
	} else { $null })
	ObservedRuns = @($runs | Select-Object -First 5 | ForEach-Object {
	    [pscustomobject]@{
		Id = $_.id
		Status = $_.status
		Conclusion = $_.conclusion
		HeadBranch = $_.head_branch
		HeadSha = $_.head_sha
		Url = $_.html_url
	    }
	})
}

$outputDirectory = Split-Path -Parent $Output
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $Output -Encoding ascii

Write-Host "hosted_ci_audit=$Output"
Write-Host "status=$status"
if ($status -eq "passed") {
	Write-Host "hosted_ci_run_url=$($greenRun.html_url)"
	if (-not [string]::IsNullOrWhiteSpace($HeadSha)) {
		Write-Host "hosted_ci_head_sha=$HeadSha"
	}
} else {
	Write-Host "detail=$detail"
}
if ($RequireGreen -and $status -ne "passed") {
	exit 1
}
