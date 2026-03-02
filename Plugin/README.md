# Starfield Animation Framework (SAF) - Plugin

SAF is the next generation animation framework for Starfield, evolved from the Native Animation Framework (NAF). Built with the updated **CommonLibSF by libxe**, SAF provides enhanced performance, stability, and compatibility with modern Starfield modding practices.

## What's New in SAF?

- **Updated CommonLibSF**: Built on libxe's latest CommonLibSF for better game compatibility
- **Improved Performance**: Optimized animation processing and memory management
- **Enhanced API**: Cleaner, more maintainable codebase with SAF naming conventions
- **Better Stability**: Improved error handling and crash prevention
- **Modern C++ Standards**: Updated to use latest C++ features and best practices

## Project Structure

```
SAF_Plugin/
├── src/
│   ├── API/              # External and internal API definitions
│   ├── Commands/         # Console commands and BetterConsole integration
│   ├── Papyrus/          # Papyrus script integration
│   ├── RE/               # Reverse-engineered game structures
│   ├── Settings/         # Configuration and settings management
│   ├── Tasks/            # Background tasks and event listeners
│   ├── Util/             # Utility functions and helpers
│   ├── PCH.h             # Precompiled header
│   └── main.cpp          # Plugin entry point
├── cmake/                # CMake configuration files
├── dist/                 # Distribution files and rules
├── CMakeLists.txt        # Main CMake configuration
└── vcpkg.json           # Dependency management
```

## Key Changes from NAF

All NAF references have been systematically replaced with SAF:

- `NAF` namespace → `SAF` namespace
- `NAFAPI_*` → `SAFAPI_*`
- `NAFCommand` → `SAFCommand`
- `NAFScript` → `SAFScript`
- Plugin name: `NativeAnimationFrameworkSF` → `StarfieldAnimationFramework`

## Dependencies

- **CommonLibSF** (libxe's updated version)
- **SFSE** (Starfield Script Extender)
- **MCF** (Modern Command Framework) - For console commands
- **spdlog** - Logging
- **fastgltf** - GLTF model loading
- **ozz-animation** - Animation processing
- **SAF-Common** - Shared animation framework code

### Console Commands via MCF

SAF integrates with **Modern Command Framework (MCF)** for console functionality. MCF must be installed separately:

1. Install `ModernCommandFramework.dll` to `Data/SFSE/Plugins/`
2. SAF will automatically register its commands with MCF
3. Use the `saf` command in console

**Example:**
```
> saf play "animations/walk.gltf"
> saf stop
```

See `SAF_MCF_Integration.md` for complete integration details.

## Building

### Prerequisites

- Visual Studio 2022 or later
- CMake 3.21+
- vcpkg for dependency management
- SFSE SDK

### Build Steps

1. Ensure all git submodules are initialized:
   ```bash
   git submodule update --init --recursive
   ```

2. Update vcpkg dependencies:
   ```bash
   vcpkg install
   ```

3. Generate build files:
   ```bash
   cmake --preset vs2022-windows
   ```

4. Build the project:
   ```bash
   cmake --build build --config Release
   ```

The built DLL will be automatically copied to your Starfield installation directory (configurable in CMakeLists.txt).

## API Documentation

### SAFAPI Functions

SAF provides a C-compatible API for external mods and tools:

- `SAFAPI_GetFeatureLevel()` - Get current API version
- `SAFAPI_PlayAnimationFromGLTF()` - Play animation from GLTF file
- `SAFAPI_AttachClipGenerator()` - Attach clip-based generator
- `SAFAPI_AttachCustomGenerator()` - Attach custom animation generator
- `SAFAPI_VisitGraph()` - Visit and inspect animation graph
- `SAFAPI_DetachGenerator()` - Remove active generator

### Papyrus Integration

SAF exposes functionality to Papyrus scripts through the `SAF` script:

```papyrus
; Example usage
SAF.PlayAnimation(actorRef, "Data/Animations/MyAnim.gltf", 0.5)
```

## Configuration

SAF uses TOML configuration files located in `Data/SFSE/Plugins/`:

- `ExampleConfig.toml` - Example configuration template
- Custom configuration options for animation behavior
- Performance tuning settings

## Migration from NAF

If you're migrating from NAF to SAF:

1. Replace NAF plugin DLL with SAF DLL
2. Update any Papyrus scripts to use `SAF` instead of `NAF`
3. Update external mod integrations to use `SAFAPI_*` functions
4. Review and update configuration files

## Contributing

Contributions are welcome! Please ensure:

- Code follows existing style conventions
- All NAF references are properly converted to SAF
- Changes maintain compatibility with CommonLibSF by libxe
- Tests pass and no regressions are introduced

## Credits

- **Original NAF**: Deweh (Snapdragon)
- **SAF Development**: SAF Team
- **CommonLibSF**: libxe and contributors
- **Animation Libraries**: ozz-animation, fastgltf

## License

SAF inherits the license from NAF. See the LICENSE file for details.

## Support

For issues, questions, or feature requests:

- GitHub Issues: [Project Repository]
- Discord: [Community Server]
- Nexus Mods: [Mod Page]

---

**Note**: SAF is designed to be a drop-in replacement for NAF with enhanced features and stability. Always backup your saves before installing any mods.
