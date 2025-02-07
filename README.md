# MinimalVulkanAppInC - Dynamic Vertex Buffer

This repository contains the source code for a minimal Vulkan app written in C. The app demonstrates basic Vulkan concepts and renders a few quads with a checkerboard texture on a yellow background.

It uses a "dynamic" vertex buffer to demonstrate drawing data that changes every frame.

**Warning**: Before building the app, make sure to adjust the `vki` and `vkl` variables in the `build.bat` file to reflect the path where you installed the Vulkan SDK on your system.

## Shader Compilation
Before running the app, make sure to compile the shaders using `glslc`:

```bash
glslc shader.vert -o vert.spv
glslc shader.frag -o frag.spv
```

You'll need these .spv files for the Vulkan pipeline.

## Tutorial Series
This code is part of a tutorial series. Check out the full tutorial on [Vulkan Tutorials in C](https://rafael-abreu-english.blogspot.com/2025/01/vulkan-tutorial.html).

The implementation of this app specifically begins at [this post](https://rafael-abreu-english.blogspot.com/2025/02/vulkan-tutorial-in-c-013-dynamic-vertex.html).