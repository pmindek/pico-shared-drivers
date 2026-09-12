@echo off
setlocal enabledelayedexpansion

rem ============================================================
rem  update_fonts.bat
rem
rem  Regenerates every font header in this directory from the PNGs in
rem  fonts\ (see generate_fonts.py), commits the result here, then
rem  pulls that commit into the bats and "bat gyros" repos' lib\shared
rem  submodule, rebuilds each to make sure nothing broke, and commits
rem  the submodule bump there too. Refuses to commit a project's bump
rem  if its build fails, so a broken state is never silently accepted.
rem
rem  Just edit/add a font PNG in fonts\, then double-click this file
rem  (or run it from a terminal) - that's the whole workflow.
rem
rem  Adding a brand NEW font still needs a couple of manual lines in
rem  ssd1306.h/.c to expose it as a named ssd1306_font_t (see
rem  README.md) - this script only regenerates/commits glyph data; it
rem  still stages a new font's raw PNG+header for you, ready for that
rem  manual wiring step, but can't do the wiring itself.
rem ============================================================

rem --- Adjust these if your local checkouts ever move. ---
set BATS_REPO=C:\Pi\apps\bats
set GYROS_REPO=C:\Pi\apps\bat gyros
set NINJA=C:\Users\2mind\.pico-sdk\ninja\v1.12.1\ninja.exe

set SCRIPT_DIR=%~dp0
set SHARED_REPO=%SCRIPT_DIR%..

echo === Regenerating font headers ===
pushd "%SCRIPT_DIR%"

python -c "import PIL" 2>nul
if errorlevel 1 (
    echo Installing Pillow ^(needed by generate_fonts.py^)...
    python -m pip install --quiet Pillow
)

python generate_fonts.py
if errorlevel 1 (
    echo.
    echo generate_fonts.py failed - see errors above. Nothing committed.
    popd
    exit /b 1
)
popd

echo.
echo === Staging changed fonts ===
pushd "%SHARED_REPO%"
for %%F in (ssd1306\fonts\*.png) do (
    git add "ssd1306\fonts\%%~nxF" "ssd1306\%%~nF.h"
)

git diff --cached --quiet
if not errorlevel 1 (
    echo No font changes to commit - fonts already up to date.
    popd
    exit /b 0
)

git commit -m "Update font glyph data (regenerated via generate_fonts.py)"
if errorlevel 1 (
    echo git commit failed - see errors above.
    popd
    exit /b 1
)
popd

echo.
echo === Propagating to bats ===
call :propagate "%BATS_REPO%" fire
if errorlevel 1 exit /b 1

echo.
echo === Propagating to bat gyros ===
call :propagate "%GYROS_REPO%" gyros
if errorlevel 1 exit /b 1

echo.
echo Done - fonts updated everywhere.
exit /b 0

:propagate
set PROJ_DIR=%~1
set TARGET=%~2

git -C "%PROJ_DIR%\lib\shared" pull origin master
if errorlevel 1 (
    echo Failed to pull shared drivers into "%PROJ_DIR%".
    exit /b 1
)

echo Building %TARGET% to verify...
pushd "%PROJ_DIR%\build"
"%NINJA%" %TARGET%
set BUILD_RESULT=%errorlevel%
popd

if not %BUILD_RESULT%==0 (
    echo Build failed for %TARGET% - NOT committing the submodule bump.
    echo Fix the build in "%PROJ_DIR%", then commit lib\shared there yourself.
    exit /b 1
)

git -C "%PROJ_DIR%" add lib\shared
git -C "%PROJ_DIR%" diff --cached --quiet -- lib\shared
if not errorlevel 1 (
    echo "%PROJ_DIR%": submodule pointer unchanged, nothing to commit.
    exit /b 0
)

git -C "%PROJ_DIR%" commit -m "Bump shared drivers: font update"
if errorlevel 1 (
    echo git commit failed in "%PROJ_DIR%".
    exit /b 1
)
exit /b 0
