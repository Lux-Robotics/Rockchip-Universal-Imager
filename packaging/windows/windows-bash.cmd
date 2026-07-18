@echo off
REM GitHub Actions custom shell for Windows self-hosted runners.
REM Usage in workflow (after checkout):
REM   shell: cmd /c "packaging\windows\windows-bash.cmd" "{0}"
REM Lives next to bootstrap-build-deps.ps1 under packaging/windows/.
REM
REM Discovers bash without hardcoding a single install layout:
REM   1) MSYS2_BASH   - full path to bash.exe
REM   2) MSYS2_ROOT   - root dir containing usr\bin\bash.exe
REM   3) Common locations on %SystemDrive% and fixed letters
REM   4) Git for Windows bash (fallback)
REM   5) bash on PATH
REM
REM {0} is the temp script path Actions generates for the step.

setlocal EnableExtensions

set "BASH_EXE="

if defined MSYS2_BASH if exist "%MSYS2_BASH%" set "BASH_EXE=%MSYS2_BASH%"

if not defined BASH_EXE if defined MSYS2_ROOT (
  if exist "%MSYS2_ROOT%\usr\bin\bash.exe" set "BASH_EXE=%MSYS2_ROOT%\usr\bin\bash.exe"
)

if not defined BASH_EXE if exist "%SystemDrive%\msys64\usr\bin\bash.exe" (
  set "BASH_EXE=%SystemDrive%\msys64\usr\bin\bash.exe"
)

REM Common alternate install roots (drive-letter independent via SystemDrive first)
if not defined BASH_EXE if exist "C:\msys64\usr\bin\bash.exe" set "BASH_EXE=C:\msys64\usr\bin\bash.exe"
if not defined BASH_EXE if exist "D:\msys64\usr\bin\bash.exe" set "BASH_EXE=D:\msys64\usr\bin\bash.exe"
if not defined BASH_EXE if exist "E:\msys64\usr\bin\bash.exe" set "BASH_EXE=E:\msys64\usr\bin\bash.exe"

REM Git for Windows (Actions default location when installed system-wide)
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
  echo Set machine env MSYS2_ROOT ^(e.g. C:\msys64^) or MSYS2_BASH ^(full path to bash.exe^). 1>&2
  echo Or install MSYS2 / Git for Windows. 1>&2
  exit /b 1
)

REM Prefer a predictable MinGW/MSYS tool PATH for the child without requiring login shells.
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

REM Cargo (Tauri workflows) - user profile install
if exist "%USERPROFILE%\.cargo\bin" set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"

echo windows-bash: using "%BASH_EXE%"
"%BASH_EXE%" --noprofile --norc -e "%~1"
exit /b %ERRORLEVEL%
