@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Davidian
rem Builds build\tangerine_orbit.lv2 (plugin + GUI) and build\tangerine_orbit.vst3 with
rem Visual Studio's x64 C compiler.
rem   build.bat         build both bundles
rem   build.bat test    also build and run the tests in build-test\
rem   build.bat dist    also zip the bundles with the docs into build\dist\ (set VERSION first)
rem Sets up the Visual Studio environment with vswhere if cl.exe is not on PATH.
rem Other platforms (and MinGW): use the Makefile.
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto :have_cl
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Could not find Visual Studio. Install it with the C++ workload, or run from a Developer Command Prompt.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:have_cl

set "BUNDLE=%~dp0build\tangerine_orbit.lv2"
set "TESTS=%~dp0build-test"
set "VST3=%~dp0build\tangerine_orbit.vst3"
if not exist "%BUNDLE%" mkdir "%BUNDLE%"
if not exist "%TESTS%" mkdir "%TESTS%"
if not exist "%VST3%\Contents\x86_64-win" mkdir "%VST3%\Contents\x86_64-win"
if not exist "%VST3%\Contents\Resources" mkdir "%VST3%\Contents\Resources"

rem /MT links the C runtime statically, so hosts don't need a redistributable installed.
rem /fp:precise keeps the NaN guard and isfinite() checks that /fp:fast may remove.
cl /nologo /std:c11 /O2 /fp:precise /MT /W4 /LD src\plugin.c /Fo"%TESTS%\\" /Fe"%BUNDLE%\tangerine_orbit.dll" /link /NOLOGO /NOIMPLIB /NOEXP || exit /b 1

rem The GUI: a Win32 window drawn by its own software renderer.
cl /nologo /std:c11 /O2 /MT /W4 /LD src\ui\ui_lv2.c /Fo"%TESTS%\\" /Fe"%BUNDLE%\tangerine_orbit_ui.dll" /link /NOLOGO /NOIMPLIB /NOEXP user32.lib gdi32.lib || exit /b 1
del /q "%BUNDLE%\*.lib" "%BUNDLE%\*.exp" 2>nul

copy /y bundle\tangerine_orbit.ttl "%BUNDLE%\" >nul || exit /b 1
powershell -NoProfile -Command "(Get-Content bundle\manifest.ttl.in) -replace '@LIB_EXT@','.dll' -replace '@UI_CLASS@','ui:WindowsUI' | Set-Content -Encoding ascii '%BUNDLE%\manifest.ttl'" || exit /b 1
rem Licence, notices and the AI usage statement travel with the bundle.
for %%F in (LICENSE NOTICE.md AI_USAGE.md) do copy /y %%F "%BUNDLE%\" >nul || exit /b 1

rem The VST3: one DLL with the engine, the parameters and the same GUI, in a VST3 bundle folder.
cl /nologo /std:c11 /O2 /fp:precise /MT /W4 /LD src\vst3\vst3.c /Fo"%TESTS%\\" /Fe"%VST3%\Contents\x86_64-win\tangerine_orbit.vst3" /link /NOLOGO /NOIMPLIB /NOEXP user32.lib gdi32.lib || exit /b 1
del /q "%VST3%\Contents\x86_64-win\*.lib" "%VST3%\Contents\x86_64-win\*.exp" 2>nul
for %%F in (LICENSE NOTICE.md AI_USAGE.md) do copy /y %%F "%VST3%\Contents\Resources\" >nul || exit /b 1

if "%1"=="test" goto :test
if "%1"=="dist" goto :dist
goto :done

:test
cl /nologo /std:c11 /O2 /fp:precise /MT /W3 test\test_dsp.c /Fo"%TESTS%\\" /Fe"%TESTS%\test_dsp.exe" /link /NOLOGO || exit /b 1
cl /nologo /std:c11 /O2 /MT /W3 test\ui_render.c /Fo"%TESTS%\\" /Fe"%TESTS%\ui_render.exe" /link /NOLOGO || exit /b 1
cl /nologo /std:c11 /O2 /MT /W3 test\ui_check.c /Fo"%TESTS%\\" /Fe"%TESTS%\ui_check.exe" /link /NOLOGO user32.lib gdi32.lib || exit /b 1
rem lilv_check needs lilv-0.dll from an Ardour install at run time; run it by hand.
cl /nologo /std:c11 /O1 /MT test\lilv_check.c /Fo"%TESTS%\\" /Fe"%TESTS%\lilv_check.exe" /link /NOLOGO || exit /b 1
cl /nologo /std:c11 /O2 /MT /W3 test\vst3_check.c /Fo"%TESTS%\\" /Fe"%TESTS%\vst3_check.exe" /link /NOLOGO user32.lib gdi32.lib || exit /b 1
"%TESTS%\test_dsp.exe" "%BUNDLE%" || exit /b 1
"%TESTS%\ui_render.exe" "%TESTS%\panel.bmp" || exit /b 1
"%TESTS%\ui_check.exe" "%BUNDLE%" "%TESTS%\ui_window.bmp" || exit /b 1
"%TESTS%\vst3_check.exe" "%VST3%" "%TESTS%\vst3_window.bmp" || exit /b 1
goto :done

:dist
if "%VERSION%"=="" set "VERSION=dev"
set "DISTNAME=davidian-tangerine-orbit-%VERSION%-windows-x64"
set "DISTDIR=%~dp0build\dist"
if exist "%DISTDIR%\%DISTNAME%" rmdir /s /q "%DISTDIR%\%DISTNAME%"
mkdir "%DISTDIR%\%DISTNAME%" || exit /b 1
xcopy /e /i /q "%BUNDLE%" "%DISTDIR%\%DISTNAME%\tangerine_orbit.lv2" >nul || exit /b 1
xcopy /e /i /q "%VST3%" "%DISTDIR%\%DISTNAME%\tangerine_orbit.vst3" >nul || exit /b 1
for %%F in (README.md LICENSE NOTICE.md AI_USAGE.md) do copy /y %%F "%DISTDIR%\%DISTNAME%\" >nul || exit /b 1
powershell -NoProfile -Command "Compress-Archive -Force -Path '%DISTDIR%\%DISTNAME%' -DestinationPath '%DISTDIR%\%DISTNAME%.zip'" || exit /b 1
echo Packaged %DISTDIR%\%DISTNAME%.zip

:done
echo Built %BUNDLE% and %VST3%
echo Davidian Tangerine Orbit is free software under GPL-3.0-or-later. It was made with generative AI ^(Claude Code^); see AI_USAGE.md.
