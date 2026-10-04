add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("client")
option_end()

add_requires("levilamina 26.51.6", {configs = {target_type = "client"}})

add_requires("levibuildscript 0.6.1")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("AnimatedMojang")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker", {modName = "AnimatedMojang", modVersion = "1.0.0-mc26.51", modPlatform = "client"})
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE")
        set_exceptions("none") -- To avoid conflicts with /EHa.
        add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    after_build(function(target)
        local outputdir = path.join(os.projectdir(), "bin", "AnimatedMojang")
        -- Copy runtime assets only; source assets and development notes stay in the repository.
        local runtimefiles = {
            "config/config.json",
            "resources/data/normal_cave.mesh",
            "resources/data/normal_cave.particles",
            "resources/textures/animated_mojang/cave/atlas.png",
            "resources/textures/animated_mojang/cave/loading_screen-100.png",
            "NOTICE.md", "LICENSE", "README.md", "README.zh-CN.md", "CHANGELOG.md"
        }
        for index = 0, 8 do
            table.insert(runtimefiles, "resources/textures/animated_mojang/title/minecraft_animation/minecraft_" .. index .. ".png")
        end
        for _, file in ipairs(runtimefiles) do
            local destination = path.join(outputdir, file)
            os.mkdir(path.directory(destination))
            os.cp(file, destination)
        end
    end)
target("TimelineTests")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    set_toolchains("clang-cl")
    add_files("tests/timeline_tests.cpp")
    add_includedirs("src")
    add_cxflags("/utf-8", "/EHsc")


target("CpuSceneTests")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    set_toolchains("clang-cl")
    add_packages("levilamina")
    add_files("tests/cpu_scene_tests.cpp")
    add_includedirs("src")
    add_cxflags("/utf-8", "/EHsc")

target("CaveSceneTests")
    set_default(false)
    set_kind("binary")
    set_rundir("$(projectdir)")
    set_languages("c++20")
    set_toolchains("clang-cl")
    add_packages("levilamina")
    add_files("tests/cave_scene_tests.cpp")
    add_includedirs("src")
    add_cxflags("/utf-8", "/EHsc")

target("CaveParticleTests")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    set_toolchains("clang-cl")
    add_packages("levilamina")
    add_files("tests/cave_particle_tests.cpp")
    add_includedirs("src")
    set_rundir(os.projectdir())
    add_cxflags("/utf-8", "/EHsc")

target("CaveOcclusionTests")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    set_toolchains("clang-cl")
    add_packages("levilamina")
    add_files("tests/cave_occlusion_tests.cpp")
    add_includedirs("src")
    set_rundir(os.projectdir())
    add_cxflags("/utf-8", "/EHsc")

target("CaveCameraTests")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    set_toolchains("clang-cl")
    add_packages("levilamina")
    add_files("tests/cave_camera_tests.cpp")
    add_includedirs("src")
    set_rundir(os.projectdir())
    add_cxflags("/utf-8", "/EHsc")
