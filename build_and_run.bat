@echo off
REM Setup Visual Studio environment variables
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64

echo.
echo Compiling the project...
cl /EHsc /W3 /I. /Imodels /Icore /Iutils main.cpp core\dac.cpp core\dp.cpp core\Edp.cpp core\heft.cpp utils\dag_generator.cpp utils\metrics.cpp /Fe:HeftProject.exe

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================
    echo Build Failed!
    echo ========================
    pause
    exit /b %ERRORLEVEL%
)

REM Clean up the generated .obj files so they don't clutter the folder
del *.obj >nul 2>&1

echo.
echo ========================
echo Build Succeeded! Running the application...
echo ========================
echo.

start "Heft Project Output" cmd /c "HeftProject.exe & echo. & pause"
