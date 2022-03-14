

local SHADER_MODEL = "6.1"
local VECTOR_INSTRUCTIONS = "AVX2"


-- Premake extension to include files at solution-scope. From https://github.com/premake/premake-core/issues/1061#issuecomment-441417853

require('vstudio')
premake.api.register {
	name = "workspacefiles",
	scope = "workspace",
	kind = "list:string",
}

premake.override(premake.vstudio.sln2005, "projects", function(base, wks)
	if wks.workspacefiles and #wks.workspacefiles > 0 then
		premake.push('Project("{2150E333-8FDC-42A3-9474-1A3956D46DE8}") = "Solution Items", "Solution Items", "{' .. os.uuid("Solution Items:"..wks.name) .. '}"')
		premake.push("ProjectSection(SolutionItems) = preProject")
		for _, file in ipairs(wks.workspacefiles) do
			file = path.rebase(file, ".", wks.location)
			premake.w(file.." = "..file)
		end
		premake.pop("EndProjectSection")
		premake.pop("EndProject")
	end
	base(wks)
end)


-----------------------------------------
-- GENERATE SOLUTION
-----------------------------------------

workspace "ProjectionMapping"
	architecture "x64"
	startproject "ProjectionMapping"

	configurations {
		"Debug",
		"Release"
	}

	flags {
		"MultiProcessorCompile"
	}
	
	workspacefiles {
        "premake5.lua",
    }


outputdir = "%{cfg.buildcfg}_%{cfg.architecture}"
shaderoutputdir = "shaders/bin/%{cfg.buildcfg}/"

group "Dependencies"
	include "ext/assimp"
	include "ext/yaml-cpp"


	externalproject "DirectXTex_Desktop_2019_Win10"
		location "ext/directxtex/DirectXTex"
		kind "StaticLib"
		language "C++"

group ""


project "ProjectionMapping"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "Off"

	targetdir ("./bin/" .. outputdir)
	objdir ("./bin_int/" .. outputdir ..  "/%{prj.name}")

	debugenvs {
		"PATH=ext/bin;%PATH%;"
	}
	debugdir "."

	pchheader "pch.h"
	pchsource "src/pch.cpp"

	files {
		"src/**.h",
		"src/**.cpp",
		"shaders/**.hlsl*",
	}

	vpaths {
		["Headers/*"] = { "src/**.h" },
		["Sources/*"] = { "src/**.cpp" },
		["Shaders/*"] = { "shaders/**.hlsl" },
	}

	libdirs {
		"ext/lib",
	}

	links {
		"d3d12",
		"D3Dcompiler",
		"DXGI",
		"dxguid",
		"dxcompiler",
		"uxtheme",
		"setupapi",
		"assimp",
		"yaml-cpp",
		"DirectXTex_Desktop_2019_Win10",
		"Ws2_32.lib",
		"k4a.lib",
		"realsense2.lib",
	}

	filter "configurations:Debug"
		links {
			"ceres-debug.lib",
			"gflags_debug.lib",
			"glogd.lib",
		}

	filter "configurations:Release"
		links {
			"ceres.lib",
			"gflags.lib",
			"glog.lib",
		}

	filter {}

	dependson {
		"assimp",
		"yaml-cpp",
		"DirectXTex_Desktop_2019_Win10",
	}

	includedirs {
		"src",
		"shaders/rs",
		"shaders/common",
	}

	sysincludedirs {
		"ext/assimp/include",
		"ext/yaml-cpp/include",
		"ext/entt/src",
		"ext/directxtex",
		"ext",
	}

	prebuildcommands {
		"ECHO Compiling shaders..."
	}

	vectorextensions (VECTOR_INSTRUCTIONS)
	floatingpoint "Fast"

	filter "configurations:Debug"
        runtime "Debug"
		symbols "On"
		
	filter "configurations:Release"
        runtime "Release"
		optimize "On"

	filter "system:windows"
		systemversion "latest"

		defines {
			"_UNICODE",
			"UNICODE",
			"_CRT_SECURE_NO_WARNINGS",
			"ENABLE_CPU_PROFILING=1",
			"ENABLE_DX_PROFILING=1",
			"ENABLE_MESSAGE_LOG=1",
		}

		defines { "SHADER_BIN_DIR=L\"" .. shaderoutputdir .. "\"" }


	filter "files:**.hlsl"
		shadermodel (SHADER_MODEL)

		flags "ExcludeFromBuild"
		shaderobjectfileoutput(shaderoutputdir .. "%{file.basename}.cso")
		shaderincludedirs {
			"shaders/rs",
			"shaders/common"
		}
		shaderdefines {
			"HLSL",
			"mat4=float4x4",
			"mat4x3=float4x3",
			"mat3x4=float3x4",
			"vec2=float2",
			"vec3=float3",
			"vec4=float4",
			"uint32=uint",
			"int32=int"
		}
	
		shaderoptions {
			"/WX",
			"/all_resources_bound",
		}
	
	filter { "configurations:Debug", "files:**.hlsl" }
		shaderoptions {
			"/Qembed_debug",
		}
		
 	
	filter("files:**_vs.hlsl")
		removeflags("ExcludeFromBuild")
		shadertype("Vertex")
 	
	filter("files:**_gs.hlsl")
		removeflags("ExcludeFromBuild")
		shadertype("Geometry")
 	
	filter("files:**_hs.hlsl")
		removeflags("ExcludeFromBuild")
		shadertype("Hull")
 	
	filter("files:**_ds.hlsl")
		removeflags("ExcludeFromBuild")
		shadertype("Domain")
	
	filter("files:**_ps.hlsl")
		removeflags("ExcludeFromBuild")
		shadertype("Pixel")
 	
	filter("files:**_cs.hlsl")
		removeflags("ExcludeFromBuild")
		shadertype("Compute")
