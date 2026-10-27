WREngine

A personal Vulkan-based rendering framework for graphics learning, rendering architecture exploration, and modern rendering system experimentation.

⚠ This project is source-available for learning and research only.
Commercial use is prohibited without permission.

Features
Vulkan rendering backend
Abstract Rendering Hardware Interface (RHI)
Shader compilation pipeline
Material & shader permutation system
Render graph exploration
Modern rendering architecture experiments
Requirements
Environment
C++20
Python 3.10+
CMake 3.25+
Visual Studio 2022 (MSVC)
Dependencies

Install:

Vulkan SDK
Git

Ensure the following tools are available in PATH:

python --version
cmake --version
Build
Step 1: Clone Repository
git clone https://github.com/mou-wj/WREngine.git
cd WREngine
Step 2: Run Setup Script

Before configuring with CMake, run the Python setup script.

This step generates required files, prepares dependencies, and configures build prerequisites.

python BuildDeps.py

This step is required.
CMake configuration may fail if skipped.

Step 3: Configure Project
cmake -S . -B build

Or for Visual Studio 2022:

cmake -S . -B build -G "Visual Studio 17 2022"
Step 4: Build

Debug:

cmake --build out --config Debug

Release:

cmake --build out --config Release

Run

`Langcher.exe` starts Editor by default. Use `Langcher.exe -app=Application` for the original application, or `-help` to list apps. App modules and startup dependencies are declared in `src/Lancher/apps.json`, copied to the runtime directory during CMake configure. See [Editor architecture](Doc/architecture/app/editor.md).

Editor starts without a project or sample models. Use Project > New/Open to select a project directory, Import resources into its Content folder, then drag a GLB/glTF card into the viewport to create a static mesh node. Node properties appear on the right; clicking a material opens its floating editor. Project > Close clears the workspace. Scene placement is not yet serialized.

Notes
The Python setup step is mandatory.
If dependencies are updated, rerun:
python BuildDeps.py
Delete the out/ folder if CMake cache becomes invalid.
License

This project is licensed under the PLSL v1.0
(Personal Learning Source License).

Personal learning and research are allowed.

Commercial use is prohibited without permission.
