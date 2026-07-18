@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM GitHub Actions custom shell for Windows self-hosted runners.
REM
REM Workflow usage (MUST be absolute — relative paths resolve from System32):
REM   shell: "\"${{ github.workspace }}\\packaging\\windows\\windows-bash.cmd\" \"{0}\""
REM
REM Do NOT use:
REM   shell: packaging\windows\windows-bash.cmd "{0}"
REM     -> looks under C:\WINDOWS\system32\packaging\...
REM   shell: cmd /c "this.cmd" "{0}"
REM     -> cmd /c drops the script path argument
REM
REM Discovers bash via MSYS2_BASH / MSYS2_ROOT / common paths / Git / PATH.

set "SCRIPT=%~1"
if not defined SCRIPT (
  echo ERROR: windows-bash.cmd: no script path argument from Actions. 1>&2
  echo Expected shell with absolute path via github.workspace + "{0}" 1>&2
  exit /b 1
)

REM Normalize forward slashes (Actions sometimes emits mixed separators)
set "SCRIPT=%SCRIPT:/=\%"

if not exist "%SCRIPT%" (
  echo ERROR: windows-bash.cmd: script not found: "%SCRIPT%" 1>&2
  exit /b 1
)

set "BASH_EXE="

if defined MSYS2_BASH if exist "%MSYS2_BASH%" set "BASH_EXE=%MSYS2_BASH%"

if not defined BASH_EXE if defined MSYS2_ROOT (
  if exist "%MSYS2_ROOT%\usr\bin\bash.exe" set "BASH_EXE=%MSYS2_ROOT%\usr\bin\bash.exe"
)

if not defined BASH_EXE if exist "%SystemDrive%\msys64\usr\bin\bash.exe" (
  set "BASH_EXE=%SystemDrive%\msys64\usr\bin\bash.exe"
)

if not defined BASH_EXE if exist "C:\msys64\usr\bin\bash.exe" set "BASH_EXE=C:\msys64\usr\bin\bash.exe"
if not defined BASH_EXE if exist "D:\msys64\usr\bin\bash.exe" set "BASH_EXE=D:\msys64\usr\bin\bash.exe"
if not defined BASH_EXE if exist "E:\msys64\usr\bin\bash.exe" set "BASH_EXE=E:\msys64\usr\bin\bash.exe"

if not defined BASH_EXE if exist "%ProgramFiles%\Git\bin\bash.exe" (
  set "BASH_EXE=%ProgramFiles%\Git\bin\bash.exe"
)
if not defined BASH_EXE if exist "%ProgramFiles%\Git\usr\bin\bash.exe" (
  set "BASH_EXE=%ProgramFiles%\Git\usr\bin\bash.exe"
)
if not defined BASH_EXE if exist "%LocalAppData%\Programs\Git\bin\bash.exe" (
  set "BASH_EXE=%LocalAppData%\Programs\Git\bin\bash.exe"
)

if not defined BASH_EXE (
  where bash.exe >nul 2>&1
  if not errorlevel 1 (
    for /f "delims=" %%I in ('where bash.exe') do (
      set "BASH_EXE=%%I"
      goto :have_bash
    )
  )
)

:have_bash
if not defined BASH_EXE (
  echo ERROR: Could not find bash.exe for this Windows runner. 1>&2
  echo Set machine env MSYS2_ROOT or MSYS2_BASH. 1>&2
  exit /b 1
)

if defined MSYS2_ROOT (
  set "PATH=%MSYS2_ROOT%\mingw64\bin;%MSYS2_ROOT%\usr\bin;%PATH%"
) else if exist "%SystemDrive%\msys64\usr\bin" (
  set "PATH=%SystemDrive%\msys64\mingw64\bin;%SystemDrive%\msys64\usr\bin;%PATH%"
)

if defined LLVM_MINGW_ROOT if exist "%LLVM_MINGW_ROOT%\bin" (
  set "PATH=%LLVM_MINGW_ROOT%\bin;%PATH%"
) else if exist "%SystemDrive%\llvm-mingw\bin" (
  set "PATH=%SystemDrive%\llvm-mingw\bin;%PATH%"
)

if exist "%USERPROFILE%\.cargo\bin" set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"

echo windows-bash: bash="%BASH_EXE%"
echo windows-bash: script="%SCRIPT%"
"%BASH_EXE%" --noprofile --norc -e "%SCRIPT%"
exit /b %ERRORLEVEL%
