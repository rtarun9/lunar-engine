:: This file is just to make the build / run process easier.

:: Shader compilation.
glslangValidator -e cs_main -o shaders/gradient.comp.spv -V -D shaders/gradient.comp


:: NOTE: Uncomment the below line if this is the first time the build.bat script is being run.
:: cmake -S . -B build
cmake --build build
.\build\Debug\lunar-engine.exe
