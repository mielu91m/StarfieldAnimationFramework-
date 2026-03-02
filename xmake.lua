add_requires("ozz-animation", "fastgltf", "simdjson", "zstr")

set_project("StarfieldAnimationFramework")
set_version("1.0.0")
add_rules("mode.debug", "mode.release")
set_policy("check.auto_ignore_flags", false)

--------------------------------------------------
-- SIMDJSON (CZYSTA, OSOBNA BIBLIOTEKA)
--------------------------------------------------
target("simdjson_lib")
    set_kind("static")
    set_languages("c++23")
    set_runtimes("MD")
    add_files("extern/simdjson/src/simdjson.cpp")
    add_includedirs(
        "extern/simdjson/include",
        "extern/simdjson/src",
        { public = true }
    )
    add_defines("SIMDJSON_STATIC")

--------------------------------------------------
-- FMT (SKOMPILOWANA BIBLIOTEKA)
--------------------------------------------------
target("fmt_lib")
    set_kind("static")
    set_languages("c++23")
    set_runtimes("MD")
    -- Pomiń fmt.cc bo to plik dla C++ modules
    add_files("extern/fmt/src/*.cc|fmt.cc")
    add_includedirs(
        "extern/fmt/include",
        { public = true }
    )
    add_defines("FMT_STATIC_THOUSANDS_SEPARATOR")

--------------------------------------------------
-- SPDLOG (SKOMPILOWANA BIBLIOTEKA)
--------------------------------------------------
target("spdlog_lib")
    set_kind("static")
    set_languages("c++23")
    set_runtimes("MD")
    add_files("extern/spdlog/src/*.cpp")
    add_includedirs(
        "extern/spdlog/include",
        "extern/fmt/include",
        { public = true }
    )
    add_defines(
        "SPDLOG_COMPILED_LIB",
        "SPDLOG_FMT_EXTERNAL"
    )
    add_deps("fmt_lib")

--------------------------------------------------
-- GŁÓWNY PLUGIN
--------------------------------------------------
target("StarfieldAnimationFramework")
    set_kind("shared")
    set_languages("c++23")
    set_runtimes("MD")
    
    -- POPRAWIONE: Flagi podstawowe osobno, forced include osobno
    add_cxflags(
        "/utf-8",
        "/EHsc",
        "/Zc:inline",
        { force = true }
    )
    
    -- POPRAWIONE: force_undef.h w głównym katalogu (obok xmake.lua)
    add_cxflags("/FIforce_undef.h", { force = true })
    
    add_syslinks("kernel32", "user32", "bcrypt", "version", "shell32", "ole32", "advapi32")
    add_links("simdjson_lib", "spdlog_lib", "fmt_lib")
    
    --------------------------------------------------
    -- TWOJE PLIKI ŹRÓDŁOWE
    --------------------------------------------------
    add_files(
        "Plugin/src/main.cpp",
        "Plugin/src/version.rc",
        "Plugin/src/Commands/*.cpp",
        "Plugin/src/API/*.cpp",
        "Plugin/src/Papyrus/*.cpp",
        "Plugin/src/Serialization/*.cpp",
        "Plugin/src/Settings/*.cpp",
        "Plugin/src/Tasks/*.cpp",
        "Plugin/src/Util/*.cpp",
        "Plugin/src/Animation/*.cpp",    
        
        -- CommonLibSF
        "extern/commonlibsf/lib/commonlib-shared/src/REL/*.cpp",
        "extern/commonlibsf/lib/commonlib-shared/src/REX/*.cpp",
        "extern/commonlibsf/src/SFSE/*.cpp",
        
        {
            cxflags = {
                "/FIfix_literals.h",
                "/FIREX/REX.h",
                "/FIREL/REL.h",
                "/FIcstdint",
                "/FItype_traits",
                "/FIstring",
                "/FIstring_view"
            }
        }
    )
    
    --------------------------------------------------
    -- ZEWNĘTRZNE BIBLIOTEKI (BEZ /FI)
    --------------------------------------------------
    add_files("extern/fastgltf/src/*.cpp|*simdjson*")
    
    --------------------------------------------------
    -- INCLUDE DIRS
    --------------------------------------------------
    add_includedirs(
        "Plugin/src",
        ".",
        { public = true }
    )
    add_includedirs(
        "extern/commonlibsf/include",
        "extern/commonlibsf/lib/commonlib-shared/include",
        { public = true }
    )
    add_includedirs(
        "extern/spdlog/include",
        "extern/fmt/include",
        { public = true }
    )
    add_includedirs(
        "extern/fastgltf/include",
        { public = true }
    )
    
    add_includedirs(
        "extern/ozz-animation/include",
        { public = true }
    )
    
    add_includedirs(
        "extern/simdjson/include",
        { public = true }
    )
    
    --------------------------------------------------
    -- DEFINICJE
    --------------------------------------------------
    add_defines(
        "SFSE_SUPPORT_XBYAK",
        "SF_VERSION=1_15_222",
        "WIN32_LEAN_AND_MEAN",
        "NOMINMAX",
        "_UNICODE",
        "UNICODE",
        "SPDLOG_COMPILED_LIB",
        "SPDLOG_FMT_EXTERNAL"
    )
    
    --------------------------------------------------
    -- LINKOWANIE
    --------------------------------------------------
    add_deps("simdjson_lib", "fmt_lib", "spdlog_lib")
    
    add_syslinks(
        "kernel32",
        "user32",
        "shell32",
        "ole32",
        "advapi32",
        "version",
        "ws2_32",
        "dbghelp",
        "bcrypt",
        "uuid",
        "d3d11",
        "dxgi",
        "d3dcompiler"
    )
    
    --------------------------------------------------
    -- EXPORT SYMBOLI / LINKER
    --------------------------------------------------
    add_shflags("/ignore:4217", "/ignore:4286")
    -- Symbole RE:: (BSScript, Ni*) są w exe gry – rozwiązywane przy ładowaniu DLL
    add_shflags("/FORCE:UNRESOLVED")
    add_rules("utils.symbols.export_all")
    add_packages("ozz-animation", "fastgltf", "simdjson", "zlib", "zstr")
