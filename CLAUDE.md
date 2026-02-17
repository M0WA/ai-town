# AI Town - 3D City Simulator

## Project Overview
AI Town is a "Sim City" style 3D city simulator built for desktop platforms (Linux/Windows). The project emphasizes realistic graphics, procedurally generated terrain, and immersive audio.

## Technical Stack

### Core Technologies
- **Language**: Object-oriented C++
- **Graphics Engine**: Irrlicht 3D Engine
- **Audio Engine**: OpenSoft AL (OpenAL Soft)
- **Platform**: Cross-platform desktop (Linux/Windows)

### Build System
- CMake (recommended for cross-platform C++ projects)
- Platform-specific build tools (GCC/Clang on Linux, MSVC on Windows)

### ⚠️ Tech Stack Requirements
**The technical stack is fixed and must not be changed.** All development must use:
- C++ (object-oriented)
- Irrlicht 3D Engine for graphics
- OpenSoft AL for audio
- Cross-platform compatibility (Linux/Windows only)

Do not suggest alternative engines, languages, or platforms.

## Project Structure

### Key Components
1. **Terrain Generation**: Procedural terrain generation system
2. **Graphics Rendering**: Irrlicht-based 3D rendering pipeline
3. **Audio System**: OpenSoft AL integration for game sounds and music
4. **Game Logic**: City simulation mechanics (traffic, economy, balance)
5. **UI/UX**: User interface for city management

### Planned Development Areas
- **Game Design**: Gameplay balance, traffic systems, economy simulation
- **Graphics**: 3D models, 2D textures, Irrlicht engine integration
- **Sound**: Game music, sound effects, OpenSoft AL integration
- **Testing**: C++ unit tests, integration tests
- **CI/CD**: GitHub Actions pipelines

## Team Structure & Specializations

### Game Design
- **Senior Game Designer** (`gamedesign-lookandfeel`): Specialized in gameplay and feel of 3D city simulators - balance, traffic, economy, etc.
- **Senior UI/UX Designer** (`gamedesign-ux`): Specialized in UI/UX of 3D city simulators

### Graphics
- **Senior 3D Model Artist** (`graphics-artist-3d-model`): Specialized in 3D models for 3D city simulators
- **Senior 2D Texture Artist** (`graphics-artist-2d-texture`): Specialized in 2D textures for 3D city simulators
- **Senior C++ Developer** (`graphics-dev-irrlicht`): Specialized in 3D Irrlicht engine

### Sound
- **Senior Sound Artist** (`sound-artist-opensoftal`): Specialized in game music and sounds for city simulators
- **Senior C++ Developer** (`sound-dev-opensoftal`): Specialized in OpenSoft AL and audio

### Testing
- **Senior C++ Test Engineer** (`test-dev-cpp`): Specialized in C++ best practices for testing

### CI/CD
- **Senior GitHub Pipeline Engineer** (`cicd-dev-github`): Specialized in GitHub Actions and continuous integration/deployment

### Team Collaboration
**All agents can delegate to each other if necessary.** While each role has specific expertise, cross-functional collaboration is encouraged when tasks require multiple specializations or when expertise from another domain would be beneficial.

## Development Guidelines

### Code Style
- Follow modern C++ best practices (C++11 or later)
- Object-oriented design principles
- Clear separation of concerns (rendering, logic, audio)

### Dependencies
- Irrlicht 3D Engine
- OpenAL Soft (OpenSoft AL)
- Platform-specific build tools

### Cross-Platform Considerations
- Use CMake for build configuration
- Avoid platform-specific APIs where possible
- Test on both Linux and Windows regularly

## File Exclusions
The following files should be ignored when analyzing the codebase:
- `epic.txt` - Project planning and team structure document

## Key Design Goals
1. **Realistic Graphics**: High-quality 3D rendering with detailed textures
2. **Generated Terrain**: Procedural generation for varied landscapes
3. **Performance**: Smooth gameplay on desktop hardware
4. **Simulation Depth**: Meaningful city management mechanics
5. **Cross-Platform**: Consistent experience on Linux and Windows

## Getting Started
(To be populated as project structure develops)

### Building
```bash
# Linux
cmake -B build -S .
cmake --build build

# Windows
cmake -B build -S . -G "Visual Studio 17 2022"
cmake --build build --config Release
```

### Running
```bash
# Execute the built binary
./build/aitown
```

## Notes for AI Assistants
- This is a C++ project using Irrlicht and OpenAL
- Focus on cross-platform compatibility
- Prioritize performance and code quality
- Follow object-oriented design patterns
- When suggesting code, ensure it's compatible with both Linux and Windows
