@echo off
rem Builds build\tangerine_orbit.lv2 and the test programs using Visual Studio's x64 C compiler.
rem Test programs go in build-test, so build\ holds only the bundle for the LV2 parser to scan.
setlocal
set "ROOT=%~dp0"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo Could not find vcvars64.bat. Edit VCVARS in build.bat to match your Visual Studio install.
    exit /b 1
)
call "%VCVARS%" >nul || exit /b 1

set "OUT=%ROOT%build"
set "BUNDLE=%OUT%\tangerine_orbit.lv2"
set "TESTS=%ROOT%build-test"
if not exist "%BUNDLE%" mkdir "%BUNDLE%"
if not exist "%TESTS%" mkdir "%TESTS%"
pushd "%TESTS%" || exit /b 1

rem /MT links the C runtime statically, so hosts don't need a redistributable installed.
cl /nologo /std:c11 /O2 /fp:precise /MT /W4 /LD "%ROOT%src\plugin.c" /Fo"%TESTS%\\" /Fe"%BUNDLE%\tangerine_orbit.dll" /link /NOLOGO /NOIMPLIB /NOEXP
if errorlevel 1 (popd & exit /b 1)
del /q "%BUNDLE%\tangerine_orbit.lib" "%BUNDLE%\tangerine_orbit.exp" 2>nul

rem The GUI: a Win32 window drawn by its own software renderer.
cl /nologo /std:c11 /O2 /MT /W4 /LD "%ROOT%src\ui\ui_lv2.c" /Fo"%TESTS%\\" /Fe"%BUNDLE%\tangerine_orbit_ui.dll" /link /NOLOGO /NOIMPLIB /NOEXP user32.lib gdi32.lib
if errorlevel 1 (popd & exit /b 1)
del /q "%BUNDLE%\tangerine_orbit_ui.lib" "%BUNDLE%\tangerine_orbit_ui.exp" 2>nul

copy /y "%ROOT%bundle\*.ttl" "%BUNDLE%\" >nul || (popd & exit /b 1)

cl /nologo /std:c11 /O2 /fp:precise /MT /W3 "%ROOT%test\test_dsp.c" /Fo"%TESTS%\\" /Fe"%TESTS%\test_dsp.exe" /link /NOLOGO
if errorlevel 1 (popd & exit /b 1)

cl /nologo /std:c11 /O2 /MT /W3 "%ROOT%test\ui_render.c" /Fo"%TESTS%\\" /Fe"%TESTS%\ui_render.exe" /link /NOLOGO
if errorlevel 1 (popd & exit /b 1)

cl /nologo /std:c11 /O2 /MT /W3 "%ROOT%test\ui_check.c" /Fo"%TESTS%\\" /Fe"%TESTS%\ui_check.exe" /link /NOLOGO user32.lib gdi32.lib
if errorlevel 1 (popd & exit /b 1)

cl /nologo /std:c11 /O1 /MT "%ROOT%test\lilv_check.c" /Fo"%TESTS%\\" /Fe"%TESTS%\lilv_check.exe" /link /NOLOGO
if errorlevel 1 (popd & exit /b 1)

popd
echo Built %BUNDLE%
