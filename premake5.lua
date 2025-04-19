project "imgui"
--	kind "SharedLib"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir (engine.lib)
	objdir (engine.intermediate)

	includedirs {
		"./",
		"./backends/",
        engine.dependency.vulkan,
	}

	symbols "on"
	links { "vulkan" }

	linkoptions { "-fPIC", "-O0", "-lstdc++" }
	buildoptions { "-fPIC", "-O0", "-g", "-fno-exceptions" }

	files {
		"imconfig.h",
		"imgui.h",
		"imgui.cpp",
		"imgui_tables.cpp",
		"imgui_draw.cpp",
		"imgui_internal.h",
		"imgui_widgets.cpp",
		"imstb_rectpack.h",
		"imstb_textedit.h",
		"imstb_truetype.h",
		"imgui_demo.cpp",

		"./backends/imgui_impl_vulkan.cpp",
		"./backends/imgui_impl_x11.cpp",
        "./backends/imgui_impl_opengl3.cpp",
        "./backends/imgui_impl_glfw.cpp",
	}

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"

	filter "system:linux"
	-- needed?
		buildoptions "-fPIC"

	filter "system:windows"
		systemversion "latest"
