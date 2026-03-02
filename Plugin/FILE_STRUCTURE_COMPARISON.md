# File Structure Comparison: NAF vs SAF

## Complete File Mapping

### Root Directory Files

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `!update.ps1` | `!update.ps1` | NAF → SAF in script content |
| `CMakeLists.txt` | `CMakeLists.txt` | Project name updated |
| `CMakePresets.json` | `CMakePresets.json` | References updated |
| `vcpkg.json` | `vcpkg.json` | Package name updated |

### cmake/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `cmake/Plugin.h.in` | `cmake/Plugin.h.in` | NAF → SAF |
| `cmake/build_stl_modules.props` | `cmake/build_stl_modules.props` | NAF → SAF |
| `cmake/common.cmake` | `cmake/common.cmake` | NAF → SAF |
| `cmake/version.rc.in` | `cmake/version.rc.in` | NAF → SAF |

### dist/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `dist/ExampleConfig.toml` | `dist/ExampleConfig.toml` | Copied as-is |
| `dist/rules/!base.json` | `dist/rules/!base.json` | Copied as-is |
| `dist/rules/bin_copy.json` | `dist/rules/bin_copy.json` | Copied as-is |
| `dist/rules/config_copy.json` | `dist/rules/config_copy.json` | Copied as-is |
| `dist/rules/publish_package.json` | `dist/rules/publish_package.json` | Copied as-is |

### src/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `src/PCH.h` | `src/PCH.h` | Plugin name updated |
| `src/main.cpp` | `src/main.cpp` | NAF → SAF throughout |

### src/API/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `src/API/API_External.h` | `src/API/API_External.h` | NAFAPI → SAFAPI |
| `src/API/API_Internal.h` | `src/API/API_Internal.h` | NAFAPI → SAFAPI |
| `src/API/API_Internal.cpp` | `src/API/API_Internal.cpp` | NAFAPI → SAFAPI |
| `src/API/CCF_API.h` | `src/API/CCF_API.h` | NAF → SAF |

### src/Commands/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `src/Commands/NAFCommand.h` | `src/Commands/SAFCommand.h` | **FILE RENAMED** + NAF → SAF |
| `src/Commands/NAFCommand.cpp` | `src/Commands/SAFCommand.cpp` | **FILE RENAMED** + NAF → SAF |

### src/Commands/BetterConsole/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `src/Commands/BetterConsole/betterapi.h` | `src/Commands/BetterConsole/betterapi.h` | NAF → SAF |
| `src/Commands/BetterConsole/NAF_UI.cpp` | `src/Commands/BetterConsole/SAF_UI.cpp` | **FILE RENAMED** + NAF → SAF |

### src/Papyrus/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `src/Papyrus/NAFScript.h` | `src/Papyrus/SAFScript.h` | **FILE RENAMED** + NAF → SAF |
| `src/Papyrus/NAFScript.cpp` | `src/Papyrus/SAFScript.cpp` | **FILE RENAMED** + NAF → SAF |
| `src/Papyrus/EventManager.h` | `src/Papyrus/EventManager.h` | NAF → SAF |
| `src/Papyrus/EventManager.cpp` | `src/Papyrus/EventManager.cpp` | NAF → SAF |

### src/RE/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `src/RE/Events.h` | `src/RE/Events.h` | NAF → SAF |

### src/Settings/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `src/Settings/SkeletonImpl.h` | `src/Settings/SkeletonImpl.h` | NAF → SAF |
| `src/Settings/SkeletonImpl.cpp` | `src/Settings/SkeletonImpl.cpp` | NAF → SAF |

### src/Tasks/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `src/Tasks/Input.h` | `src/Tasks/Input.h` | NAF → SAF |
| `src/Tasks/Input.cpp` | `src/Tasks/Input.cpp` | NAF → SAF |
| `src/Tasks/SaveLoadListener.h` | `src/Tasks/SaveLoadListener.h` | NAF → SAF |
| `src/Tasks/SaveLoadListener.cpp` | `src/Tasks/SaveLoadListener.cpp` | NAF → SAF |

### src/Util/ Directory

| NAF File | SAF File | Changes |
|----------|----------|---------|
| `src/Util/VM.h` | `src/Util/VM.h` | NAF → SAF |
| `src/Util/VM.cpp` | `src/Util/VM.cpp` | NAF → SAF |

## Summary Statistics

### File Counts
- **Total Files**: 35 files
- **Files Renamed**: 5 files
- **Files with Content Changes**: 30 files
- **Files Copied As-Is**: 5 files (dist/rules/*.json)

### Renamed Files
1. `NAFCommand.h` → `SAFCommand.h`
2. `NAFCommand.cpp` → `SAFCommand.cpp`
3. `NAFScript.h` → `SAFScript.h`
4. `NAFScript.cpp` → `SAFScript.cpp`
5. `NAF_UI.cpp` → `SAF_UI.cpp`

### Key Changes Made

1. **Global Replace**: `NAF` → `SAF` in all source files
2. **API Replace**: `NAFAPI` → `SAFAPI` in API files
3. **Namespace Updates**: All NAF namespaces changed to SAF
4. **Project Name**: `NativeAnimationFrameworkSF` → `StarfieldAnimationFramework`
5. **Plugin Name**: Updated in PCH.h
6. **Version**: 1.2.3 → 2.0.0
7. **Dependencies**: `NAF-Common` → `SAF-Common`

### Directory Structure

```
SAF_Plugin/
├── cmake/                  (4 files - all updated)
├── dist/
│   └── rules/             (5 files - copied as-is)
└── src/
    ├── API/               (4 files - all updated)
    ├── Commands/
    │   └── BetterConsole/ (3 files - 2 renamed, all updated)
    ├── Papyrus/           (4 files - 2 renamed, all updated)
    ├── RE/                (1 file - updated)
    ├── Settings/          (2 files - all updated)
    ├── Tasks/             (4 files - all updated)
    ├── Util/              (2 files - all updated)
    ├── PCH.h              (updated)
    └── main.cpp           (updated)
```

## Build System Changes

### CMakeLists.txt
- Project name: `NativeAnimationFrameworkSF` → `StarfieldAnimationFramework`
- Version: `1.2.3` → `2.0.0`
- External dependency: `NAF-Common` → `SAF-Common`

### vcpkg.json
- Package name: `nativeanimationframeworksf` → `starfieldanimationframework`
- Version: `1.0.0` → `2.0.0`
- Homepage: Updated to new repository URL

### CMakePresets.json
- All NAF references replaced with SAF
- Configuration names remain the same

## Migration Notes

### For Build Systems
- Update submodule references from `NAF-Common` to `SAF-Common`
- Update output DLL name expectations
- Update install paths if hardcoded

### For CI/CD
- Update artifact names
- Update deployment scripts
- Update version tags

### For Documentation
- Update all references from NAF to SAF
- Update API documentation
- Update example code

## Verification Checklist

After migration, verify:

- [ ] All files renamed correctly
- [ ] No remaining NAF references in code
- [ ] NAFAPI calls changed to SAFAPI
- [ ] CMake configuration builds successfully
- [ ] Plugin loads in Starfield
- [ ] API exports are correct
- [ ] Papyrus integration works
- [ ] Console commands function
- [ ] No regressions in functionality

## Additional Files Created

New files in SAF that don't exist in NAF:

1. `README.md` - Project overview and documentation
2. `NAF_to_SAF_Migration_Guide.md` - Detailed migration instructions
3. `FILE_STRUCTURE_COMPARISON.md` - This file

These files help developers understand the changes and migrate their code.

---

**Note**: This comparison is based on the Plugin folder structure. The complete project includes additional folders (extern/, Blender/, CreationKit/, AnimationOptimizer/) which should be migrated similarly.
