@echo off
echo Building Exploration Horizon...
if not exist build mkdir build
cd build
cmake .. -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    cd ..
    exit /b %ERRORLEVEL%
)
echo Building project with MSBuild...
cmake --build . --config Release -- /m:1 /p:CL_MPCount=1
if %ERRORLEVEL% NEQ 0 (
    echo Release build failed, trying Debug build...
    cmake --build . --config Debug -- /m:1 /p:CL_MPCount=1
)
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    cd ..
    exit /b %ERRORLEVEL%
)
cd ..
echo Build successful! Launching executable...
if exist build\Release\ExplorationHorizon.exe (
    build\Release\ExplorationHorizon.exe
) else if exist build\Debug\ExplorationHorizon.exe (
    build\Debug\ExplorationHorizon.exe
)


