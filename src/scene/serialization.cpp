#include "pch.h"
#include "serialization.h"

#include "editor/file_dialog.h"
#include "core/yaml.h"
#include "core/log.h"


static YAML::Emitter& operator<<(YAML::Emitter& out, const render_camera& camera)
{
	out << YAML::BeginMap
		<< YAML::Key << "Position" << YAML::Value << camera.position
		<< YAML::Key << "Rotation" << YAML::Value << camera.rotation
		<< YAML::Key << "Near plane" << YAML::Value << camera.nearPlane
		<< YAML::Key << "Far plane" << YAML::Value << camera.farPlane
		<< YAML::Key << "Type" << YAML::Value << camera.type;

	if (camera.type == camera_type_ingame)
	{
		out << YAML::Key << "FOV" << YAML::Value << camera.verticalFOV;
	}
	else
	{
		out << YAML::Key << "Fx" << YAML::Value << camera.intrinsics.fx
			<< YAML::Key << "Fy" << YAML::Value << camera.intrinsics.fy
			<< YAML::Key << "Cx" << YAML::Value << camera.intrinsics.cx
			<< YAML::Key << "Cy" << YAML::Value << camera.intrinsics.cy
			<< YAML::Key << "Width" << YAML::Value << camera.width
			<< YAML::Key << "Height" << YAML::Value << camera.height;
	}

	out << YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const directional_light& sun)
{
	out << YAML::BeginMap
		<< YAML::Key << "Color" << YAML::Value << sun.color
		<< YAML::Key << "Intensity" << YAML::Value << sun.intensity
		<< YAML::Key << "Direction" << YAML::Value << sun.direction
		<< YAML::Key << "Shadow dimensions" << YAML::Value << sun.shadowDimensions
		<< YAML::Key << "Cascades" << YAML::Value << sun.numShadowCascades
		<< YAML::Key << "Distances" << YAML::Value << sun.cascadeDistances
		<< YAML::Key << "Bias" << YAML::Value << sun.bias
		<< YAML::Key << "Blend distances" << YAML::Value << sun.blendDistances
		<< YAML::Key << "Stabilize" << YAML::Value << sun.stabilize
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const tonemap_settings& s)
{
	out << YAML::BeginMap
		<< YAML::Key << "A" << YAML::Value << s.A
		<< YAML::Key << "B" << YAML::Value << s.B
		<< YAML::Key << "C" << YAML::Value << s.C
		<< YAML::Key << "D" << YAML::Value << s.D
		<< YAML::Key << "E" << YAML::Value << s.E
		<< YAML::Key << "F" << YAML::Value << s.F
		<< YAML::Key << "Linear white" << YAML::Value << s.linearWhite
		<< YAML::Key << "Exposure" << YAML::Value << s.exposure
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const hbao_settings& s)
{
	out << YAML::BeginMap
		<< YAML::Key << "Radius" << YAML::Value << s.radius
		<< YAML::Key << "Num rays" << YAML::Value << s.numRays
		<< YAML::Key << "Max num steps per ray" << YAML::Value << s.maxNumStepsPerRay
		<< YAML::Key << "Strength" << YAML::Value << s.strength
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const sss_settings& s)
{
	out << YAML::BeginMap
		<< YAML::Key << "Num steps" << YAML::Value << s.numSteps
		<< YAML::Key << "Ray distance" << YAML::Value << s.rayDistance
		<< YAML::Key << "Thickness" << YAML::Value << s.thickness
		<< YAML::Key << "Max distance" << YAML::Value << s.maxDistanceFromCamera
		<< YAML::Key << "Distance fadeout" << YAML::Value << s.distanceFadeoutRange
		<< YAML::Key << "Border fadeout" << YAML::Value << s.borderFadeout
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const ssr_settings& s)
{
	out << YAML::BeginMap
		<< YAML::Key << "Num steps" << YAML::Value << s.numSteps
		<< YAML::Key << "Max distance" << YAML::Value << s.maxDistance
		<< YAML::Key << "Stride cutoff" << YAML::Value << s.strideCutoff
		<< YAML::Key << "Min stride" << YAML::Value << s.minStride
		<< YAML::Key << "Max stride" << YAML::Value << s.maxStride
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const taa_settings& s)
{
	out << YAML::BeginMap
		<< YAML::Key << "Camera jitter" << YAML::Value << s.cameraJitterStrength
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const bloom_settings& s)
{
	out << YAML::BeginMap
		<< YAML::Key << "Threshold" << YAML::Value << s.threshold
		<< YAML::Key << "Strength" << YAML::Value << s.strength
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const sharpen_settings& s)
{
	out << YAML::BeginMap
		<< YAML::Key << "Strength" << YAML::Value << s.strength
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const renderer_settings& s)
{
	out << YAML::BeginMap
		<< YAML::Key << "Tone map" << YAML::Value << s.tonemapSettings
		<< YAML::Key << "Environment intensity" << YAML::Value << s.environmentIntensity
		<< YAML::Key << "Sky intensity" << YAML::Value << s.skyIntensity
		<< YAML::Key << "Enable SSR" << YAML::Value << s.enableSSR
		<< YAML::Key << "SSR" << YAML::Value << s.ssrSettings
		<< YAML::Key << "Enable TAA" << YAML::Value << s.enableTAA
		<< YAML::Key << "TAA" << YAML::Value << s.taaSettings
		<< YAML::Key << "Enable AO" << YAML::Value << s.enableAO
		<< YAML::Key << "AO" << YAML::Value << s.aoSettings
		<< YAML::Key << "Enable SSS" << YAML::Value << s.enableSSS
		<< YAML::Key << "SSS" << YAML::Value << s.sssSettings
		<< YAML::Key << "Enable Bloom" << YAML::Value << s.enableBloom
		<< YAML::Key << "Bloom" << YAML::Value << s.bloomSettings
		<< YAML::Key << "Enable Sharpen" << YAML::Value << s.enableSharpen
		<< YAML::Key << "Sharpen" << YAML::Value << s.sharpenSettings
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const transform_component& c)
{
	out << YAML::BeginMap
		<< YAML::Key << "Rotation" << YAML::Value << c.rotation
		<< YAML::Key << "Position" << YAML::Value << c.position
		<< YAML::Key << "Scale" << YAML::Value << c.scale
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const position_component& c)
{
	out << YAML::BeginMap
		<< YAML::Key << "Position" << YAML::Value << c.position
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const position_rotation_component& c)
{
	out << YAML::BeginMap
		<< YAML::Key << "Rotation" << YAML::Value << c.rotation
		<< YAML::Key << "Position" << YAML::Value << c.position
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const point_light_component& c)
{
	out << YAML::BeginMap
		<< YAML::Key << "Color" << YAML::Value << c.color
		<< YAML::Key << "Intensity" << YAML::Value << c.intensity
		<< YAML::Key << "Radius" << YAML::Value << c.radius
		<< YAML::Key << "Casts shadow" << YAML::Value << c.castsShadow
		<< YAML::Key << "Shadow resolution" << YAML::Value << c.shadowMapResolution
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const spot_light_component& c)
{
	out << YAML::BeginMap
		<< YAML::Key << "Color" << YAML::Value << c.color
		<< YAML::Key << "Intensity" << YAML::Value << c.intensity
		<< YAML::Key << "Distance" << YAML::Value << c.distance
		<< YAML::Key << "Inner angle" << YAML::Value << c.innerAngle
		<< YAML::Key << "Outer angle" << YAML::Value << c.outerAngle
		<< YAML::Key << "Casts shadow" << YAML::Value << c.castsShadow
		<< YAML::Key << "Shadow resolution" << YAML::Value << c.shadowMapResolution
		<< YAML::EndMap;
	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const raster_component& c)
{
	auto mesh = c.mesh;

	out << YAML::BeginMap
		<< YAML::Key << "Handle" << YAML::Value << mesh->handle
		<< YAML::Key << "Flags" << YAML::Value << mesh->flags
		<< YAML::EndMap;

	return out;
}

static YAML::Emitter& operator<<(YAML::Emitter& out, const projector_context& c)
{
	out << YAML::BeginMap
		<< YAML::Key << "Projector calibrations" << YAML::Value << YAML::BeginSeq;

	for (auto it : c.knownProjectorCalibrations)
	{
		auto& c = it.second;

		out << YAML::BeginMap
			<< YAML::Key << "Monitor" << YAML::Value << it.first
			<< YAML::Key << "Rotation" << YAML::Value << c.rotation
			<< YAML::Key << "Position" << YAML::Value << c.position
			<< YAML::Key << "Width" << YAML::Value << c.width
			<< YAML::Key << "Height" << YAML::Value << c.height
			<< YAML::Key << "Fx" << YAML::Value << c.intrinsics.fx
			<< YAML::Key << "Fy" << YAML::Value << c.intrinsics.fy
			<< YAML::Key << "Cx" << YAML::Value << c.intrinsics.cx
			<< YAML::Key << "Cy" << YAML::Value << c.intrinsics.cy
			<< YAML::EndMap;
	}

	out << YAML::EndSeq
		<< YAML::EndMap;

	return out;
}

namespace YAML
{
	template<>
	struct convert<render_camera>
	{
		static bool decode(const Node& n, render_camera& camera) 
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, camera.position, "Position");
			YAML_LOAD(n, camera.rotation, "Rotation");
			YAML_LOAD(n, camera.nearPlane, "Near plane");
			YAML_LOAD(n, camera.farPlane, "Far plane");
			YAML_LOAD_ENUM(n, camera.type, "Type");

			if (camera.type == camera_type_ingame)
			{
				YAML_LOAD(n, camera.verticalFOV, "FOV");
			}
			else
			{
				YAML_LOAD(n, camera.intrinsics.fx, "Fx");
				YAML_LOAD(n, camera.intrinsics.fy, "Fy");
				YAML_LOAD(n, camera.intrinsics.cx, "Cx");
				YAML_LOAD(n, camera.intrinsics.cy, "Cy");
				YAML_LOAD(n, camera.width, "Width");
				YAML_LOAD(n, camera.height, "Height");
			}

			camera.setViewport(camera.width, camera.height);
			camera.updateMatrices();

			return true; 
		}
	};

	template<>
	struct convert<tonemap_settings>
	{
		static bool decode(const Node& n, tonemap_settings& s)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, s.A, "A");
			YAML_LOAD(n, s.B, "B");
			YAML_LOAD(n, s.C, "C");
			YAML_LOAD(n, s.D, "D");
			YAML_LOAD(n, s.E, "E");
			YAML_LOAD(n, s.F, "F");
			YAML_LOAD(n, s.linearWhite, "Linear white");
			YAML_LOAD(n, s.exposure, "Exposure");

			return true;
		}
	};

	template<>
	struct convert<hbao_settings>
	{
		static bool decode(const Node& n, hbao_settings& s)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, s.radius, "Radius");
			YAML_LOAD(n, s.numRays, "Num rays");
			YAML_LOAD(n, s.maxNumStepsPerRay, "Max num steps per ray");
			YAML_LOAD(n, s.strength, "Strength");

			return true;
		}
	};

	template<>
	struct convert<sss_settings>
	{
		static bool decode(const Node& n, sss_settings& s)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, s.numSteps, "Num steps");
			YAML_LOAD(n, s.rayDistance, "Ray distance");
			YAML_LOAD(n, s.thickness, "Thickness");
			YAML_LOAD(n, s.maxDistanceFromCamera, "Max distance");
			YAML_LOAD(n, s.distanceFadeoutRange, "Distance fadeout");
			YAML_LOAD(n, s.borderFadeout, "Border fadeout");

			return true;
		}
	};

	template<>
	struct convert<ssr_settings>
	{
		static bool decode(const Node& n, ssr_settings& s)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, s.numSteps, "Num steps");
			YAML_LOAD(n, s.maxDistance, "Max distance");
			YAML_LOAD(n, s.strideCutoff, "Stride cutoff");
			YAML_LOAD(n, s.minStride, "Min stride");
			YAML_LOAD(n, s.maxStride, "Max stride");

			return true;
		}
	};

	template<>
	struct convert<taa_settings>
	{
		static bool decode(const Node& n, taa_settings& s)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, s.cameraJitterStrength, "Camera jitter");

			return true;
		}
	};

	template<>
	struct convert<bloom_settings>
	{
		static bool decode(const Node& n, bloom_settings& s)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, s.threshold, "Threshold");
			YAML_LOAD(n, s.strength, "Strength");

			return true;
		}
	};

	template<>
	struct convert<sharpen_settings>
	{
		static bool decode(const Node& n, sharpen_settings& s)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, s.strength, "Strength");

			return true;
		}
	};

	template<>
	struct convert<renderer_settings>
	{
		static bool decode(const Node& n, renderer_settings& settings)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, settings.tonemapSettings, "Tone map");
			YAML_LOAD(n, settings.environmentIntensity, "Environment intensity");
			YAML_LOAD(n, settings.skyIntensity, "Sky intensity");
			YAML_LOAD(n, settings.enableSSR, "Enable SSR");
			YAML_LOAD(n, settings.ssrSettings, "SSR");
			YAML_LOAD(n, settings.enableTAA, "Enable TAA");
			YAML_LOAD(n, settings.taaSettings, "TAA");
			YAML_LOAD(n, settings.enableAO, "Enable AO");
			YAML_LOAD(n, settings.aoSettings, "AO");
			YAML_LOAD(n, settings.enableSSS, "Enable SSS");
			YAML_LOAD(n, settings.sssSettings, "SSS");
			YAML_LOAD(n, settings.enableBloom, "Enable Bloom");
			YAML_LOAD(n, settings.bloomSettings, "Bloom");
			YAML_LOAD(n, settings.enableSharpen, "Enable Sharpen");
			YAML_LOAD(n, settings.sharpenSettings, "Sharpen");

			return true;
		}
	};

	template<>
	struct convert<directional_light>
	{
		static bool decode(const Node& n, directional_light& sun)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, sun.color, "Color");
			YAML_LOAD(n, sun.intensity, "Intensity");
			YAML_LOAD(n, sun.direction, "Direction");
			YAML_LOAD(n, sun.shadowDimensions, "Shadow dimensions");
			YAML_LOAD(n, sun.numShadowCascades, "Cascades");
			YAML_LOAD(n, sun.cascadeDistances, "Distances");
			YAML_LOAD(n, sun.bias, "Bias");
			YAML_LOAD(n, sun.blendDistances, "Blend distances");
			YAML_LOAD(n, sun.stabilize, "Stabilize");

			return true;
		}
	};

	template<>
	struct convert<transform_component>
	{
		static bool decode(const Node& n, transform_component& c)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, c.rotation, "Rotation");
			YAML_LOAD(n, c.position, "Position");
			YAML_LOAD(n, c.scale, "Scale");

			return true;
		}
	};

	template<>
	struct convert<position_component>
	{
		static bool decode(const Node& n, position_component& c)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, c.position, "Position");

			return true;
		}
	};

	template<>
	struct convert<position_rotation_component>
	{
		static bool decode(const Node& n, position_rotation_component& c)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, c.rotation, "Rotation");
			YAML_LOAD(n, c.position, "Position");

			return true;
		}
	};

	template<>
	struct convert<point_light_component>
	{
		static bool decode(const Node& n, point_light_component& c)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, c.color, "Color");
			YAML_LOAD(n, c.intensity, "Intensity");
			YAML_LOAD(n, c.radius, "Radius");
			YAML_LOAD(n, c.castsShadow, "Casts shadow");
			YAML_LOAD(n, c.shadowMapResolution, "Shadow resolution");

			return true;
		}
	};

	template<>
	struct convert<spot_light_component>
	{
		static bool decode(const Node& n, spot_light_component& c)
		{
			if (!n.IsMap()) { return false; }

			YAML_LOAD(n, c.color, "Color");
			YAML_LOAD(n, c.intensity, "Intensity");
			YAML_LOAD(n, c.distance, "Distance");
			YAML_LOAD(n, c.innerAngle, "Inner angle");
			YAML_LOAD(n, c.outerAngle, "Outer angle");
			YAML_LOAD(n, c.castsShadow, "Casts shadow");
			YAML_LOAD(n, c.shadowMapResolution, "Shadow resolution");

			return true;
		}
	};

	template<>
	struct convert<raster_component>
	{
		static bool decode(const Node& n, raster_component& c)
		{
			if (!n.IsMap()) { return false; }

			asset_handle handle;
			uint32 flags;

			YAML_LOAD(n, handle, "Handle");
			YAML_LOAD(n, flags, "Flags");

			c.mesh = loadMeshFromHandle(handle, flags);

			return true;
		}
	};

	template<>
	struct convert<projector_context>
	{
		static bool decode(const Node& n, projector_context& c)
		{
			if (!n.IsMap()) { return false; }

			if (Node projectorsNode = n["Projector calibrations"])
			{
				for (Node proj : projectorsNode)
				{
					std::string monitor;
					projector_calibration desc;
					
					YAML_LOAD(proj, monitor, "Monitor");
					YAML_LOAD(proj, desc.rotation, "Rotation");
					YAML_LOAD(proj, desc.position, "Position");
					YAML_LOAD(proj, desc.width, "Width");
					YAML_LOAD(proj, desc.height, "Height");
					YAML_LOAD(proj, desc.intrinsics.fx, "Fx");
					YAML_LOAD(proj, desc.intrinsics.fy, "Fy");
					YAML_LOAD(proj, desc.intrinsics.cx, "Cx");
					YAML_LOAD(proj, desc.intrinsics.cy, "Cy");

					c.knownProjectorCalibrations.insert({ monitor, desc });
				}
			}
			
			return true;
		}
	};
}

void serializeSceneToDisk(game_scene& scene, const renderer_settings& rendererSettings, depth_tracker* tracker, projector_context* projectorContext)
{
	if (scene.savePath.empty())
	{
		fs::path filename = saveFileDialog("Scene files", "sc");
		if (filename.empty())
		{
			return;
		}

		scene.savePath = filename;
	}

	YAML::Emitter out;
	out << YAML::BeginMap;

	out << YAML::Key << "Scene" << YAML::Value << "My scene";
	out << YAML::Key << "Camera" << YAML::Value << scene.camera;
	out << YAML::Key << "Rendering" << YAML::Value << rendererSettings;
	out << YAML::Key << "Sun" << YAML::Value << scene.sun;
	out << YAML::Key << "Environment" << (scene.environment ? scene.environment->name : fs::path());

	out << YAML::Key << "Tracker position" << YAML::Value << tracker->globalCameraPosition
		<< YAML::Key << "Tracker rotation" << YAML::Value << tracker->globalCameraRotation;

	out << YAML::Key << "Projector context" << YAML::Value << *projectorContext;

	out << YAML::Key << "Entities"
		<< YAML::Value
		<< YAML::BeginSeq;


	scene.forEachEntity([&out, &scene, tracker](entt::entity entityID)
	{
		scene_entity entity = { entityID, scene };

		if (!entity.hasComponent<projector_component>()) // For now we don't serialize projectors.
		{
			if (tag_component* tag = entity.getComponentIfExists<tag_component>())
			{
				out << YAML::BeginMap;

				out << YAML::Key << "Tag" << YAML::Value << tag->name;

				if (auto* c = entity.getComponentIfExists<transform_component>()) { out << YAML::Key << "Transform" << YAML::Value << *c; }
				if (auto* c = entity.getComponentIfExists<position_component>()) { out << YAML::Key << "Position" << YAML::Value << *c; }
				if (auto* c = entity.getComponentIfExists<position_rotation_component>()) { out << YAML::Key << "Position/Rotation" << YAML::Value << *c; }
				if (entity.hasComponent<dynamic_transform_component>()) { out << YAML::Key << "Dynamic" << YAML::Value << true; }
				if (auto* c = entity.getComponentIfExists<point_light_component>()) { out << YAML::Key << "Point light" << YAML::Value << *c; }
				if (auto* c = entity.getComponentIfExists<spot_light_component>()) { out << YAML::Key << "Spot light" << YAML::Value << *c; }
				if (auto* c = entity.getComponentIfExists<raster_component>()) { out << YAML::Key << "Raster" << YAML::Value << *c; }

				if (tracker->isEntityTracked(entity))
				{
					out << YAML::Key << "Tracking" << YAML::Value << true;
				}

				out << YAML::EndMap;
			}
		}
		});

	out << YAML::EndSeq;

	out << YAML::EndMap;

	fs::create_directories(scene.savePath.parent_path());

	std::ofstream fout(scene.savePath);
	fout << out.c_str();

	LOG_MESSAGE("Scene saved to '%ws'", scene.savePath.c_str());
}

bool deserializeSceneFromDisk(game_scene& scene, renderer_settings& rendererSettings, std::string& environmentName, depth_tracker* tracker, projector_context* projectorContext)
{
	fs::path filename = openFileDialog("Scene files", "sc");
	if (filename.empty())
	{
		return false;
	}

	std::ifstream stream(filename);
	YAML::Node n = YAML::Load(stream);
	if (!n["Scene"])
	{
		return false;
	}

	scene = game_scene();
	scene.savePath = std::move(filename);
	projectorContext->knownProjectorCalibrations.clear();
	tracker->clearTrackedEntities();

	std::string sceneName = n["Scene"].as<std::string>();

	YAML_LOAD(n, scene.camera, "Camera");
	YAML_LOAD(n, rendererSettings, "Rendering");
	YAML_LOAD(n, scene.sun, "Sun");

	YAML_LOAD(n, environmentName, "Environment");

	YAML_LOAD(n, tracker->globalCameraPosition, "Tracker position");
	YAML_LOAD(n, tracker->globalCameraRotation, "Tracker rotation");

	YAML_LOAD(n, *projectorContext, "Projector context");

	auto entitiesNode = n["Entities"];
	for (auto entityNode : entitiesNode)
	{
		std::string name = entityNode["Tag"].as<std::string>();
		scene_entity entity = scene.createEntity(name.c_str());

#define LOAD_COMPONENT(type, name) if (auto node = entityNode[name]) { entity.addComponent<type>(node.as<type>()); }

		LOAD_COMPONENT(transform_component, "Transform");
		LOAD_COMPONENT(position_component, "Position");
		LOAD_COMPONENT(position_rotation_component, "Position/Rotation");
		if (entityNode["Dynamic"]) { entity.addComponent<dynamic_transform_component>(); }
		LOAD_COMPONENT(point_light_component, "Point light");
		LOAD_COMPONENT(spot_light_component, "Spot light");
		LOAD_COMPONENT(raster_component, "Raster");

		if (entityNode["Tracking"])
		{
			tracker->trackEntity(entity);
		}
	}

	LOG_MESSAGE("Scene loaded from '%ws'", scene.savePath.c_str());

	return true;
}

