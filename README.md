# DX12 Project - Raytraced Ambient Occlusion

This repository contains a DX12 project, developed for the course **DV2551 3D-Programming III** at Blekinge Institute of Technology (BTH). 

I chose to implement Raytraced Ambient Occlusion (RTAO) with Temporal Accumulative Denoising for the project.

Ambient Occlusion is a rendering technique that is used in nearly all 3D-rendered games created today and aims to produce highly detailed shadows that come from occluding global (ambient) light. **More info later**.

## How To Build

The project is opened using Visual Studio 2022 and is built using their internal CMake tool after it has finished configuring the project, which should happen automatically. After building the .exe file is created inside of the _out/_ folder, under the folder with the specified build mode (selected inside of Visual Studio), and is contained in the _Core/_ directory.

**NOTE:** The project has only been tested to be built inside VS 2022.
