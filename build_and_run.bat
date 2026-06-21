@echo off
setlocal

rem -------------------------------------------------------------------------
rem Build CyberBiology (Release) and launch it on success.
rem -------------------------------------------------------------------------

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "CONFIG=Release"
set "EXE=%BUILD%\bin\%CONFIG%\CyberBiology.exe"

rem Configure the build tree if it does not exist yet.
if not exist "%BUILD%\CMakeCache.txt" (
    echo [build_and_run] Configuring CMake project...
    cmake -S "%ROOT%." -B "%BUILD%"
    if errorlevel 1 goto :fail
)

echo [build_and_run] Building %CONFIG%...
cmake --build "%BUILD%" --config %CONFIG%
if errorlevel 1 goto :fail

if not exist "%EXE%" (
    echo [build_and_run] ERROR: executable not found at "%EXE%"
    goto :fail
)

echo [build_and_run] Launching CyberBiology...
pushd "%BUILD%\bin\%CONFIG%"
CyberBiology.exe
popd

endlocal
exit /b 0

:fail
echo [build_and_run] Build failed.
endlocal
exit /b 1
