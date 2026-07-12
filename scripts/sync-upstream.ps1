param(
	[switch]$Finish
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$featureBranch = "feature/cjk-localization"
$originalLocation = Get-Location
$injectionActive = $false

function Assert-LastExitCode {
	param([string]$Action)
	if ($LASTEXITCODE -ne 0) {
		throw "$Action 失败，退出码: $LASTEXITCODE"
	}
}

function Get-GitPath {
	param([string]$Name)
	$path = & git rev-parse --git-path $Name
	Assert-LastExitCode "解析 Git 路径 $Name"
	return $path
}

function Assert-NoRebase {
	if ((Test-Path -LiteralPath (Get-GitPath "rebase-merge")) `
		-or (Test-Path -LiteralPath (Get-GitPath "rebase-apply"))) {
		throw "检测到未完成的 rebase。请由 Agent 解决冲突并执行 git rebase --continue。"
	}
}

function Assert-CleanTrackedTree {
	& git diff --quiet
	$worktreeStatus = $LASTEXITCODE
	& git diff --cached --quiet
	$indexStatus = $LASTEXITCODE
	if ($worktreeStatus -ne 0 -or $indexStatus -ne 0) {
		& git status --short
		throw "已跟踪文件存在修改。请由 Agent 判断、提交或恢复后再同步。"
	}

	$untracked = @(& git ls-files --others --exclude-standard)
	Assert-LastExitCode "检查未跟踪文件"
	if ($untracked.Count -gt 0) {
		Write-Warning "保留以下未跟踪文件；如与目标分支冲突，git switch 会安全停止:"
		$untracked | ForEach-Object { Write-Warning "  $_" }
	}
}

function Invoke-BuildValidation {
	$script:injectionActive = $true
	$restoreFailed = $false
	try {
		& python scripts/inject_hardcoded.py
		Assert-LastExitCode "注入中文"
		& cmake --build build/mingw --config Release
		Assert-LastExitCode "构建验证"
	}
	finally {
		if ($script:injectionActive) {
			& python scripts/inject_hardcoded.py --restore
			$restoreFailed = $LASTEXITCODE -ne 0
			$script:injectionActive = $false
		}
	}
	if ($restoreFailed) {
		throw "自动恢复失败，注入状态已保留，必须由 Agent 处理。"
	}
}

function Finish-Sync {
	param([string]$StateFile)

	if (-not (Test-Path -LiteralPath $StateFile)) {
		throw "缺少同步状态，不能安全执行验证和推送。"
	}
	Assert-NoRebase
	Assert-CleanTrackedTree
	if ((& git branch --show-current) -ne $featureBranch) {
		throw "验证和推送必须在 $featureBranch 分支执行。"
	}

	$syncState = @(Get-Content -LiteralPath $StateFile)
	if ($syncState.Count -ne 2) {
		throw "同步状态格式无效，请由 Agent 检查。"
	}
	$oldMaster = $syncState[0]
	$expectedFeature = $syncState[1]

	Write-Host "构建验证汉化分支..." -ForegroundColor Yellow
	Invoke-BuildValidation
	Assert-CleanTrackedTree

	Write-Host "原子推送 master 和 $featureBranch..." -ForegroundColor Yellow
	& git push --atomic `
		"--force-with-lease=refs/heads/${featureBranch}:$expectedFeature" `
		origin `
		master `
		"+${featureBranch}:${featureBranch}"
	Assert-LastExitCode "原子推送"

	Remove-Item -LiteralPath $StateFile
	Write-Host "=== 同步完成 ===" -ForegroundColor Green
	Write-Host "上游更新摘要:"
	& git log --oneline "$oldMaster..master"
	Assert-LastExitCode "生成上游更新摘要"
}

try {
	Set-Location $repoRoot
	$stateFile = Get-GitPath "sync-upstream-state"
	$windowsSsh = Join-Path $env:WINDIR "System32\OpenSSH\ssh.exe"
	if (-not (Test-Path -LiteralPath $windowsSsh)) {
		throw "找不到 Windows OpenSSH: $windowsSsh"
	}
	$env:GIT_SSH_COMMAND = $windowsSsh.Replace("\", "/")
	$env:PATH = "D:\msys\ucrt64\bin;$env:PATH"

	if ($Finish) {
		Finish-Sync $stateFile
		exit 0
	}

	Write-Host "=== 同步上游 endless-sky ===" -ForegroundColor Cyan
	Assert-NoRebase
	Assert-CleanTrackedTree
	if (Test-Path -LiteralPath $stateFile) {
		throw "存在未完成的同步状态，请由 Agent 检查后执行 scripts/sync-upstream.ps1 -Finish。"
	}
	if ((& git branch --show-current) -ne $featureBranch) {
		throw "请从 $featureBranch 分支启动同步。"
	}

	& git remote get-url upstream *> $null
	Assert-LastExitCode "检查 upstream 远端"
	& git remote get-url origin *> $null
	Assert-LastExitCode "检查 origin 远端"

	Write-Host "获取 upstream 和 origin..." -ForegroundColor Yellow
	& git fetch upstream --tags --force
	Assert-LastExitCode "获取 upstream"
	& git fetch origin
	Assert-LastExitCode "获取 origin"

	$oldMaster = & git rev-parse master
	Assert-LastExitCode "读取 master"
	$expectedFeature = & git rev-parse "refs/remotes/origin/$featureBranch"
	Assert-LastExitCode "读取 origin 汉化分支"
	$behindOrigin = [int](& git rev-list --count "$featureBranch..refs/remotes/origin/$featureBranch")
	Assert-LastExitCode "检查汉化分支远端差异"
	if ($behindOrigin -ne 0) {
		throw "$featureBranch 落后于 origin；请由 Agent 先整合远端提交。"
	}
	Set-Content -LiteralPath $stateFile -Value @($oldMaster, $expectedFeature) -Encoding utf8NoBOM

	Write-Host "更新本地 master..." -ForegroundColor Yellow
	& git switch master
	Assert-LastExitCode "切换 master"
	& git merge upstream/master --ff-only
	Assert-LastExitCode "快进 master"

	Write-Host "Rebase $featureBranch..." -ForegroundColor Yellow
	& git switch $featureBranch
	Assert-LastExitCode "切换汉化分支"
	& git rebase master
	if ($LASTEXITCODE -ne 0) {
		Write-Host "检测到冲突。无冲突提交已自动处理，当前 rebase 状态保持不变。" -ForegroundColor Red
		Write-Host "请由 Agent 解决冲突并循环执行 git rebase --continue；完成后运行:"
		Write-Host "  scripts/sync-upstream.ps1 -Finish"
		exit 2
	}

	Finish-Sync $stateFile
}
finally {
	Set-Location $originalLocation
}
