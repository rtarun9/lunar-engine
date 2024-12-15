:: This file is just to make the build / run process easier.

:: Shader compilation.
dxc -HV 2021 -T cs_6_0 -E cs_main -spirv -fspv-target-env="vulkan1.3" shaders\gradient.comp.hlsl -Fo shaders/gradient.comp.spv


:: NOTE: Uncomment the below line if this is the first time the build.bat script is being run.
:: cmake -S . -B build
cmake --build build
.\build\Debug\lunar-engine.exe
