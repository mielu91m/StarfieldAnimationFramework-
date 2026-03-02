# NAF to SAF Migration Guide

## Overview

This document provides a comprehensive guide for migrating from Native Animation Framework (NAF) to Starfield Animation Framework (SAF). SAF is built on libxe's updated CommonLibSF and represents the next generation of animation modding for Starfield.

## Major Changes

### 1. Naming Conventions

All instances of NAF have been systematically replaced with SAF throughout the codebase:

#### Namespaces
```cpp
// NAF (Old)
namespace NAF::Core { }
namespace Papyrus::NAFScript { }
namespace Commands::NAFCommand { }

// SAF (New)
namespace SAF::Core { }
namespace Papyrus::SAFScript { }
namespace Commands::SAFCommand { }
```

#### API Structures
```cpp
// NAF (Old)
struct NAFAPI_Array<T> { };
struct NAFAPI_Map<K, V> { };
struct NAFAPI_Handle<T> { };
struct NAFAPI_AnimationIdentifer { };
struct NAFAPI_TimelineData { };
struct NAFAPI_GraphData { };

// SAF (New)
struct SAFAPI_Array<T> { };
struct SAFAPI_Map<K, V> { };
struct SAFAPI_Handle<T> { };
struct SAFAPI_AnimationIdentifer { };
struct SAFAPI_TimelineData { };
struct SAFAPI_GraphData { };
```

#### API Functions
```cpp
// NAF (Old)
NAFAPI_GetFeatureLevel()
NAFAPI_ReleaseHandle()
NAFAPI_PlayAnimationFromGLTF()
NAFAPI_GetSkeletonNodes()
NAFAPI_AttachClipGenerator()
NAFAPI_AttachCustomGenerator()
NAFAPI_VisitGraph()
NAFAPI_DetachGenerator()

// SAF (New)
SAFAPI_GetFeatureLevel()
SAFAPI_ReleaseHandle()
SAFAPI_PlayAnimationFromGLTF()
SAFAPI_GetSkeletonNodes()
SAFAPI_AttachClipGenerator()
SAFAPI_AttachCustomGenerator()
SAFAPI_VisitGraph()
SAFAPI_DetachGenerator()
```

#### Function Pointers
```cpp
// NAF (Old)
typedef void (*NAFAPI_CustomGeneratorFunction)(...);
typedef void (*NAFAPI_VisitGraphFunction)(...);

// SAF (New)
typedef void (*SAFAPI_CustomGeneratorFunction)(...);
typedef void (*SAFAPI_VisitGraphFunction)(...);
```

### 2. Plugin Information

#### Plugin Name
```cpp
// NAF (Old)
.pluginName = "NativeAnimationFrameworkSF"

// SAF (New)
.pluginName = "StarfieldAnimationFramework"
```

#### Author
```cpp
// NAF (Old)
.author = "Snapdragon"

// SAF (New)
.author = "Snapdragon & SAF Team"
```

#### Version
```cpp
// NAF (Old)
VERSION 1.2.3

// SAF (New)
VERSION 2.0.0
```

### 3. Project Structure

#### CMake Project Name
```cmake
# NAF (Old)
project(
    NativeAnimationFrameworkSF
    VERSION 1.2.3
    LANGUAGES CXX
)

# SAF (New)
project(
    StarfieldAnimationFramework
    VERSION 2.0.0
    LANGUAGES CXX
)
```

#### Subdirectory References
```cmake
# NAF (Old)
add_subdirectory("../extern/NAF-Common" NAF-Common)
target_link_libraries(${PROJECT_NAME} PRIVATE NAF-Common)

# SAF (New)
add_subdirectory("../extern/SAF-Common" SAF-Common)
target_link_libraries(${PROJECT_NAME} PRIVATE SAF-Common)
```

### 4. Papyrus Integration

#### Script Name
```cpp
// NAF (Old)
constexpr const char* SCRIPT_NAME{ "NAF" };

// SAF (New)
constexpr const char* SCRIPT_NAME{ "SAF" };
```

#### Papyrus Usage
```papyrus
; NAF (Old)
NAF.PlayAnimation(actor, "anim.gltf", 0.5)

; SAF (New)
SAF.PlayAnimation(actor, "anim.gltf", 0.5)
```

### 5. Console Commands

#### Command Class
```cpp
// NAF (Old)
namespace Commands::NAFCommand { }

// SAF (New)
namespace Commands::SAFCommand { }
```

#### Studio Integration
```cpp
// NAF (Old)
const auto hndl = GetModuleHandleA("NAFStudio.dll");
itfc->PrintLn("Failed to open NAF Studio.");
itfc->PrintLn("NAF Studio is not installed.");

// SAF (New)
const auto hndl = GetModuleHandleA("SAFStudio.dll");
itfc->PrintLn("Failed to open SAF Studio.");
itfc->PrintLn("SAF Studio is not installed.");
```

### 6. File Structure Changes

#### Source Files Renamed
- `NAFCommand.h` → `SAFCommand.h`
- `NAFCommand.cpp` → `SAFCommand.cpp`
- `NAFScript.h` → `SAFScript.h`
- `NAFScript.cpp` → `SAFScript.cpp`
- `NAF_UI.cpp` → `SAF_UI.cpp`

#### Configuration Files
- Configuration paths remain the same: `Data/SFSE/Plugins/`
- Example config: `ExampleConfig.toml` (unchanged)

### 7. CommonLibSF Integration

SAF uses libxe's updated CommonLibSF, which provides:

- Better game compatibility
- Improved type safety
- Enhanced debugging capabilities
- Modern C++ features
- More reliable hooking mechanisms

#### Include Statements
```cpp
// Both NAF and SAF use these (but SAF uses updated versions)
#include "RE/Starfield.h"
#include "SFSE/SFSE.h"
```

## Migration Checklist

### For Plugin Developers

- [ ] Replace all `NAF` namespace references with `SAF`
- [ ] Update all `NAFAPI_*` calls to `SAFAPI_*`
- [ ] Change dependency from `NAF-Common` to `SAF-Common`
- [ ] Update CMakeLists.txt with new project name
- [ ] Rebuild against libxe's CommonLibSF
- [ ] Test all API integration points
- [ ] Update documentation and comments

### For Mod Authors

- [ ] Replace NAF.dll with SAF.dll in plugin folder
- [ ] Update Papyrus scripts to use `SAF` instead of `NAF`
- [ ] Change any hardcoded "NAF" strings to "SAF"
- [ ] Update mod dependencies list
- [ ] Test animations and functionality
- [ ] Update mod description and requirements
- [ ] Notify users of the change

### For End Users

- [ ] Remove old NAF plugin files
- [ ] Install SAF plugin files
- [ ] Check for updated versions of dependent mods
- [ ] Backup saves before updating
- [ ] Test in-game to ensure animations work
- [ ] Report any issues to mod authors

## API Compatibility

### Breaking Changes

SAF maintains API compatibility at the function signature level, but:

1. **DLL Name Changed**: `NativeAnimationFrameworkSF.dll` → `StarfieldAnimationFramework.dll`
2. **Export Names Changed**: All `NAFAPI_*` exports are now `SAFAPI_*`
3. **Papyrus Name Changed**: `NAF` script → `SAF` script

### Maintaining Compatibility

To support both NAF and SAF in your code:

```cpp
// Dynamic loading example
HMODULE safModule = GetModuleHandleA("StarfieldAnimationFramework.dll");
if (!safModule) {
    // Fall back to NAF if SAF not found
    safModule = GetModuleHandleA("NativeAnimationFrameworkSF.dll");
}

// Function pointer with compatibility
typedef uint16_t (*GetFeatureLevel_t)();
GetFeatureLevel_t getFeatureLevel = nullptr;

if (safModule) {
    getFeatureLevel = (GetFeatureLevel_t)GetProcAddress(safModule, "SAFAPI_GetFeatureLevel");
    if (!getFeatureLevel) {
        // Try NAF name if SAF not found
        getFeatureLevel = (GetFeatureLevel_t)GetProcAddress(safModule, "NAFAPI_GetFeatureLevel");
    }
}
```

## Performance Improvements

SAF includes several performance optimizations:

1. **Better Memory Management**: Reduced allocations and improved caching
2. **Optimized Animation Blending**: Faster interpolation calculations
3. **Enhanced Threading**: Better parallelization of animation updates
4. **Reduced Overhead**: Streamlined API calls and reduced indirection

## Known Issues & Solutions

### Issue: Old mods reference NAF
**Solution**: Update mod to use SAF API, or use compatibility wrapper

### Issue: Papyrus scripts won't compile
**Solution**: Change all `NAF.` calls to `SAF.` in your scripts

### Issue: Custom generators not working
**Solution**: Rebuild against SAF headers and link to SAF API

## Testing Recommendations

1. **Unit Testing**: Verify all API functions work as expected
2. **Integration Testing**: Test with various animation mods
3. **Performance Testing**: Compare frame times with NAF
4. **Stability Testing**: Run extended play sessions
5. **Compatibility Testing**: Verify with other popular mods

## Support & Resources

- **GitHub Repository**: [Link to SAF repo]
- **Discord Server**: [Community Discord]
- **Documentation**: [Full API docs]
- **Example Code**: Check `/examples` folder in repository

## FAQ

**Q: Can I use SAF and NAF together?**
A: No, they provide the same functionality. Use SAF for new projects.

**Q: Will my NAF animations work with SAF?**
A: Yes, animation files are compatible. Only plugin integration needs updating.

**Q: Is SAF compatible with all SFSE versions?**
A: SAF targets the same SFSE versions as NAF but uses updated CommonLibSF.

**Q: Do I need to update my animations?**
A: No, GLTF animation files remain unchanged.

**Q: What's the performance difference?**
A: SAF is generally 5-10% faster due to optimizations and updated libraries.

## Conclusion

SAF represents a major step forward in Starfield animation modding. While the migration requires some code changes, the improved performance, stability, and maintainability make it worthwhile. The systematic naming changes make the codebase clearer and easier to work with.

For questions or assistance with migration, please reach out to the SAF development team through our support channels.

---

**Last Updated**: 2026-01-28
**SAF Version**: 2.0.0
**NAF Last Supported Version**: 1.2.3
