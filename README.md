# DX12 Project - Raytraced Ambient Occlusion

This repository contains a DX12 project, developed for the course **DV2551 3D-Programming III** at Blekinge Institute of Technology (BTH). The course gave students free range to implement any graphical technique that utilizes the newer features of modern graphics APIs.

I chose to implement Raytraced Ambient Occlusion (RTAO) with Temporal Accumulative Denoising for the project.

Ambient occlusion is a rendering technique that is used in nearly all 3D-rendered games created today and aims to produce highly detailed shadows that come from occluding global (ambient) light. Personally, I've been insipired by several ray tracing techniques because of how much they can improve the quality of a render in real time. Ambient occlusion is also a problem that lends itself well to ray tracing solutions and produces visually appealing results without depending much on other complex systems. For these reasons, I felt that it was a perfect choice to build my project around.

## Results

The image below is taken from a presentation explaining how I implemented the technique.

![image](https://github.com/user-attachments/assets/3eb7945d-029c-471b-8866-899ac88e3d82)


## How To Build

The project is opened using Visual Studio 2022 and is built using their internal CMake tool after it has finished configuring the project, which should happen automatically. After building the .exe file is created inside of the _out/_ folder, under the folder with the specified build mode (selected inside of Visual Studio), and is contained in the _Core/_ directory.

**NOTE:** The project has only been tested to be built inside VS 2022.

## Parameters

**All of the actions below require a recompile to take effect**.

To modify the samples per pixel that is used for the ambient occlusion, change the **NUM_SAMPLES** define macro to the desired amount. 

The accumulation pass can be skipped by changing the **sRenderPassOrder** vector at the top of the _DX12Renderer.cpp_ file. The camera will orbit around the scene if the accumulation pass is skipped.

By defining **TESTING** for the pre-processor, the scene will no long be randomized and the camera will always be static.
