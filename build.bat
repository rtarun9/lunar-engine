:: This file is just to make the build / run process easier.

:: NOTE: Uncomment the below line if this is the first time the build.bat script is being run.
:: cmake -S . -B build
cmake --build build
.\build\Debug\lunar-engine.exe
