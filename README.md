<img align="center" src="github/logo.png">

# Arcane2DEngine
A game engine focused on 2D games for Windows OS. Done to be simple.
Currently under development, the test code relies on main.cpp when option Debug is set.

# Examples
There are subprojects under examples folder of FlappyBird, Pong and Physics bodies test, which should handle more than 150 at 60 fps bodies without problem.

# Features
Done: Objects as rectangles, collision, images and text rendering, scenes, inputs, events.
Under development: Sound, animation, cinematics and scene editor with ImGui.

# Dependencies
OPENGL
GLFW
GLEW
STB
SPDLOG
IMGUI
INIFILE-CPP

# Build
Currently only Visual Studio 2022 solution. To build and run the test code under main.cpp use Debug option, to build the examples first slect EngineOnly Release or Debug option, build the Arcane2DEngine after do the build of the example.
