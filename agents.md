# Agent Guidance

## Build environment

Visual Studio 2026 is installed and builds this solution successfully. Treat the VS 2026 IDE or its associated developer-command environment as the authoritative local build environment for this repository.

Do not infer that C++ builds are unavailable merely because a separate Build Tools installation lacks Visual C++ targets. When a build is needed, use the installed Visual Studio 2026 environment and report the exact configuration and result.

## Suggested local commands

The following paths and command are examples verified on one development machine only. They are suggested starting points, not portable requirements: they may be absent or different on another filesystem or computer.

```powershell
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat'
$project = 'C:\Projects\FileManager\src\vcxproj\salamand.vcxproj'
$cmd = 'call "' + $vsDevCmd + '" -arch=x64 -host_arch=x64 && msbuild "' + $project + '" /t:Build /p:Configuration=Debug /p:Platform=x64 /m'
& $env:ComSpec /d /s /c $cmd
```

This uses the Visual Studio developer environment before invoking MSBuild, rather than relying on a standalone Build Tools installation.
