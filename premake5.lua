local sanit = false
local inspect = require 'inspect'
local caustic = loadfile("../caustic/caustic.lua")()

workspace "ray_example"
    configurations { "Debug", "Release" }

    defines{"GRAPHICS_API_OPENGL_43"}

    print("includedirs", inspect(caustic.includedirs))
    print("libdirs", inspect(caustic.libdirs))

    includedirs(caustic.includedirs)
    libdirs(caustic.libdirs)
    includedirs { 
        "../caustic/src",
    }

    if sanit then
        linkoptions {
            "-fsanitize=address",
            "-fsanitize-recover=address",
        }
        buildoptions { 
            "-fsanitize=address",
            "-fsanitize-recover=address",
            "-ggdb3",
            "-fPIC",
            "-Wall",
            --"-Wno-strict-aliasing",
        }
    end

    --[[
    links { 
        "m",
        "lua:static",
        "raylib:static",
        "utf8proc:static",
        "chipmunk:static",
    }
    --]]
    links(caustic.links)

    --[[
    libdirs { 
        "./3rd_party/hashtbl/",
        "./3rd_party/utf8proc",
        "./3rd_party/Chipmunk2d/src",
        "./3rd_party/raylib/raylib",
        "./3rd_party/lua-5.4.x",
    }
    --]]

    language "C++"
    cppdialect "C++20"
    kind "ConsoleApp"
    targetprefix ""
    targetdir "."
    symbols "On"

    --[[
    project "gen_height_textures"
        buildoptions { 
            "-ggdb3",
        }
        files { 
            "gen_heiht_textures.c",
            "src/height_color.c",
        }
    --]]

    project "t80"
        libdirs(caustic.libdirs)
        links({
            'raylib',
            'chipmunk',
            'genann',
            'utf8proc',
            'caustic', 
            'smallregex',
            'm'
        })
        --]]
        links('lua')
        --buildcommands("tl build")
        linkoptions {
            "-fsanitize=address",
            "-fsanitize-recover=address",
        }
        buildoptions { 
            "-ggdb3",
        }
        files { 
            "src/**.c",
        }

    --[[
    project "libcaustic"
        kind "StaticLib"
        linkoptions {
            "-fsanitize=address",
            "-fsanitize-recover=address",
        }
        buildoptions { 
            "-ggdb3",
        }
        files { 
            "./src/*.h", 
            "./src/*.c",
            "./3rd_party/small-regex/libsmallregex/libsmallregex.c",
        }
        removefiles("*main.c")
    --]]

    --[[
    project "test_objects_pool"
        buildoptions { 
            "-ggdb3",
        }
        files {
            "src/*.h",
            "src/object.c",
            "tests/munit.c",
            "tests/test_object_pool.c",
        }

    project "test_de_ecs"
        buildoptions { 
            "-ggdb3",
        }
        files {
            "tests/munit.c",
            "tests/test_de_ecs.c",
        }

    project "test_timers"
        buildoptions { 
            "-ggdb3",
        }
        files {
            "src/*.h",
            "tests/munit.c",
            "tests/test_timers.c",
        }

    project "test_array"
        buildoptions { 
            "-ggdb3",
        }
        files {
            "src/*.h",
            "tests/munit.c",
            "tests/test_array.c",
        }

    project "test_console"
        buildoptions { 
            "-ggdb3",
        }
        files {
            "src/*.h",
            "tests/munit.c",
            "tests/test_console.c",
        }

    project "test_table"
        buildoptions { 
            "-ggdb3",
        }
        files {
            "tests/munit.c",
            "tests/test_table.c",
        }
    
        
    project "test_strset"
        buildoptions { 
            "-ggdb3",
        }
        files {
            "src/strset.c",
            "tests/munit.c",
            "tests/test_strset.c",
        }
    --]]
    
    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"
        buildoptions { 
            "-ggdb3"
        }
        linkoptions {
            "-fsanitize=address",
        }
        buildoptions { 
            "-fsanitize=address",
        }

    filter "configurations:Release"
        --buildoptions { 
            --"-O2"
        --}
        --symbols "On"
        --defines { "NDEBUG" }
        --optimize "On"

