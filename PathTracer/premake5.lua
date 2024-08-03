project "PathTracer"
	architecture "x86_64"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp"
	}

	includedirs
	{
		globalIncludes,
	}	 

	links
	{
		"Vulture",
	}

	defines 
	{
		"_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS",
		"IMGUI_DEFINE_MATH_OPERATORS"
	}

	buildoptions { "/MP" }

	filter "system:windows"
		defines "WIN"
		systemversion "latest"

	filter "configurations:Debug"
		defines "DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "RELEASE"
		runtime "Release"
		optimize "Full"

	filter "configurations:Distribution"
		defines "DISTRIBUTION"
		runtime "Release"
		optimize "Full"