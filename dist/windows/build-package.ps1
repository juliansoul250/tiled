# Build on a disposable Windows runner; no signing keys or user settings are used.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$Root = (Resolve-Path "$PSScriptRoot/../..").Path
$Kit = (Resolve-Path $env:QT_ROOT_DIR).Path
$Qmake = Join-Path $Kit 'bin/qmake.exe'
$Deploy = Join-Path $Kit 'bin/windeployqt.exe'
if ((& $Qmake -query QT_VERSION).Trim() -ne '6.11.3') { throw '必须使用完整 Qt 6.11.3 x64 套件' }
$Work = Join-Path $env:RUNNER_TEMP ('tiled-windows-' + [guid]::NewGuid())
$Stage = Join-Path $Work 'production'
$Diagnostic = Join-Path $Work 'diagnostic'
$Results = Join-Path $Root 'windows-package-results'
New-Item -ItemType Directory -Force $Work,$Results | Out-Null
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$VS = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $VS) { throw '未找到 MSVC x64 工具链' }
$Toolset = (Get-Content "$VS/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt").Trim()
$Compiler = "$VS/VC/Tools/MSVC/$Toolset/bin/Hostx64/x64/cl.exe"
$RedistVersion = (Get-Content "$VS/VC/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt").Trim()
$env:TILED_VC_REDIST_DIR = "$VS/VC/Redist/MSVC/$RedistVersion/x64/Microsoft.VC143.CRT"
if (-not (Test-Path "$env:TILED_VC_REDIST_DIR/vcruntime140.dll")) { throw '缺少微软允许分发的 x64 CRT 目录' }
qbs setup-toolchains --type msvc $Compiler tiled-win-msvc
qbs setup-qt $Qmake tiled-win-6113
qbs config profiles.tiled-win-6113.baseProfile tiled-win-msvc
python "$Root/tests/check_chinese_translation.py"
python "$Root/tests/check_unity_importer.py"
python "$Root/tests/test_windows_package_audit.py"
$Common = @('-f', "$Root/tiled.qbs", 'profile:tiled-win-6113', 'config:release', 'qbs.installPrefix:', 'products.python.condition:false')
# Same Qt kit tests, including actual qwindows MainWindow / dialogs / 582-file transfer.
qbs build @Common -d "$Work/diagnostic-build" 'projects.Tiled.nativeWidgetTests:true' -p tiled,test_unityui,test_unitytransfer,test_layers,test_uistyle
foreach ($Test in @('test_unitytransfer','test_layers','test_uistyle')) {
    qbs run @Common -d "$Work/diagnostic-build" 'projects.Tiled.nativeWidgetTests:true' -p $Test -- -o "$Results/$Test.txt,txt"
}
qbs install @Common -d "$Work/diagnostic-build" --install-root $Diagnostic 'projects.Tiled.nativeWidgetTests:true' -p tiled,test_unityui
# A wholly separate build ensures test executables and diagnostic flags never ship.
qbs install @Common -d "$Work/production-build" --install-root $Stage 'projects.Tiled.nativeWidgetTests:false'
$Help = (& $Deploy --help | Out-String)
$NoPatch = if ($Help -match '--no-patchqt\b') { '--no-patchqt' } elseif ($Help -match '--nopatchqt\b') { '--nopatchqt' } else { throw 'Qt 部署工具不支持保留原始 DLL 校验' }
function Deploy-App([string]$Directory, [bool]$IsDiagnostic) {
    $Targets = @(Get-ChildItem $Directory -Filter '*.exe' | ForEach-Object FullName)
    & $Deploy --release $NoPatch --no-compiler-runtime --no-translations --dir $Directory @Targets 2>&1 | Tee-Object "$Results/deploy-$IsDiagnostic.log"
    Copy-Item "$env:TILED_VC_REDIST_DIR/*.dll" $Directory
    New-Item -ItemType Directory -Force "$Directory/translations" | Out-Null
    Copy-Item "$Kit/translations/*_zh_CN.qm" "$Directory/translations/"
    Set-Content -Encoding utf8 "$Directory/qt.conf" "[Paths]`nPrefix=.`nPlugins=.`nTranslations=translations"
    Copy-Item "$Root/AUTHORS","$Root/COPYING","$Root/LICENSE.*" $Directory
    New-Item -ItemType Directory -Force "$Directory/licenses/Qt" | Out-Null
    Copy-Item "$Kit/licenses/*" "$Directory/licenses/Qt/" -Recurse
    Copy-Item "$Root/docs/development/windows-portable-zh_CN.md" "$Directory/使用说明.md"
    # Preserve symbols separately, not as part of the portable application.
    $Symbols = Join-Path $Work ('symbols-' + $IsDiagnostic)
    foreach ($File in @(Get-ChildItem $Directory -Recurse -File | Where-Object { $_.Extension -in '.pdb','.lib','.exp','.ilk' })) {
        $Relative = [IO.Path]::GetRelativePath($Directory, $File.FullName)
        $Destination = Join-Path $Symbols $Relative
        New-Item -ItemType Directory -Force (Split-Path $Destination) | Out-Null
        Move-Item $File.FullName $Destination
    }
}
Deploy-App $Diagnostic $true
Deploy-App $Stage $false
# Built-in portable settings mode keeps preferences with this distribution.
Set-Content -Encoding utf8 "$Stage/tiled.ini" '[General]'
python "$Root/dist/windows/verify_package.py" $Stage $Kit $Root
# Actual packaged editor code/resource layout; diagnostic QtTest remains outside ZIP.
python "$Root/tests/windows_app_smoke.py" --diagnostic $Diagnostic --results $Results
$Zip = Join-Path $Root 'Tiled-Windows-x64-Qt6.11.3-unsigned.zip'
Compress-Archive -Path "$Stage/*" -DestinationPath $Zip -CompressionLevel Optimal
$Relocated = Join-Path $env:RUNNER_TEMP ('Tiled 中文便携包 ' + [guid]::NewGuid())
Expand-Archive -LiteralPath $Zip -DestinationPath $Relocated
python "$Root/dist/windows/verify_package.py" $Relocated $Kit $Root --verify-only
python "$Root/tests/windows_app_smoke.py" --app $Relocated --results $Results --sample "$Root/examples/desert.tmx" --seconds 900
# The bytes tested above are exactly the archive uploaded; never rebuild after validation.
$Hash = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -Encoding ascii "$Zip.sha256" "$Hash  $([IO.Path]::GetFileName($Zip))"
Write-Host "Windows 中文便携包已验证（未签名）：$Zip"
