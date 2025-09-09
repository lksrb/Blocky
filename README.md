# Blocky
Blocky is a 3D voxel-based game. Build and destroy blocks.

## Controls
Tutorial is present when you open the game.

## Specifications

Rendering API: DirectX 12

Language: C++ 20

Platform: Windows
## Getting started
Visual Studio 2022 is recommended for browing the source code. Debug and Release configurations are build using the MSVC toolset.

<ins>**1. Downloading the repository**</ins>

Start by cloning this repository using `git clone https://www.github.com/lksrb/Blocky`.

<ins>**2. Setting up the project**</ins>

Navigate to `Scripts` folder and run [Win32-GenerateSolution.bat](https://github.com/lksrb/Blocky/blob/main/Scripts/Win32-GenerateSolution.bat). 
This script will generate ```Blocky.sln``` into the root folder.

<ins>**3. Browsing the code**</ins>

Open ```Blocky.sln``` in the root folder using Visual Studio 2022 to browse code.

## Resources
Following features of Blocky game were made possible by external resources:

- Bloom shader implementation: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
- Lighting techniques like Phong or Blinn-Phong: https://learnopengl.com/Advanced-Lighting/Advanced-Lighting
-Slab method for fast raycasting: https://tavianator.com/2022/ray_box_boundary.html

