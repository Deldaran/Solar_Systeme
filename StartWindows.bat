@echo off
REM ════════════════════════════════════════
REM  StartWindows.bat — Build & Lance Solar System (Windows)
REM ════════════════════════════════════════
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%build

echo.
echo ╔══════════════════════════════════════╗
echo ║   Solar System — Build (Windows)    ║
echo ╚══════════════════════════════════════╝
echo.

REM ── Vérification cmake ──────────────────
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] cmake introuvable.
    echo Telecharge-le sur https://cmake.org/download/ et ajoute-le au PATH.
    pause
    exit /b 1
)

REM ── Vérification git ────────────────────
where git >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] git introuvable.
    echo Telecharge-le sur https://git-scm.com/download/win
    pause
    exit /b 1
)

REM ── Nombre de cœurs ─────────────────────
set /a CORES=%NUMBER_OF_PROCESSORS%
if "%CORES%"=="" set CORES=4

REM ── Configuration CMake ──────────────────
echo [1/2] Configuration CMake...
cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" ^
      -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo [ERROR] La configuration CMake a echoue.
    pause
    exit /b 1
)

REM ── Compilation ─────────────────────────
echo.
echo [2/2] Compilation (%CORES% threads)...
cmake --build "%BUILD_DIR%" --config Release --parallel %CORES%
if %ERRORLEVEL% neq 0 (
    echo [ERROR] La compilation a echoue.
    pause
    exit /b 1
)

REM ── Lancement ───────────────────────────
echo.
echo   Build termine — lancement...
echo.
"%BUILD_DIR%\Release\SolarSystem.exe"
if %ERRORLEVEL% neq 0 (
    REM Essai sans sous-dossier Release (Makefile generators)
    "%BUILD_DIR%\SolarSystem.exe"
)

pause
