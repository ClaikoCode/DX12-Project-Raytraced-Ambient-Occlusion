# DX12 Project - Raytraced Ambient Occlusion

This repository contains a DX12 project, developed for the course **DV2551 3D-Programming III** at Blekinge Institute of Technology (BTH). 

I chose to implement Raytraced Ambient Occlusion (RTAO) with Temporal Accumulative Denoising for the project.

Ambient occlusion is a rendering technique that is used in nearly all 3D-rendered games created today and aims to produce highly detailed shadows that come from occluding global (ambient) light. **More info later**.

## How To Build

The project is opened using Visual Studio 2022 and is built using their internal CMake tool after it has finished configuring the project, which should happen automatically. After building the .exe file is created inside of the _out/_ folder, under the folder with the specified build mode (selected inside of Visual Studio), and is contained in the _Core/_ directory.

**NOTE:** The project has only been tested to be built inside VS 2022.

## Parameters

**All of the actions below require a recompile to take effect**.

To modify the samples per pixel that is used for the ambient occlusion, change the **NUM_SAMPLES** define macro to the desired amount. 

The accumulation pass can be skipped by changing the **sRenderPassOrder** vector at the top of the _DX12Renderer.cpp_ file. The camera will orbit around the scene if the accumulation pass is skipped.

By defining **TESTING** for the pre-processor, the scene will no long be randomized and the camera will always be static.
