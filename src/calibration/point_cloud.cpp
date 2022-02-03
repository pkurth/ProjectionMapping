#include "pch.h"
#include "point_cloud.h"
#include "core/log.h"
#include "core/image.h"

#include <fstream>

void image_point_cloud::constructFromRendering(const image<vec4>& rendering, const image<vec2>& unprojectTable)
{
	entries.resize(rendering.width, rendering.height);
	numEntries = 0;

	for (uint32 y = 0; y < rendering.height; ++y)
	{
		for (uint32 x = 0; x < rendering.width; ++x)
		{
			vec4 r = rendering(y, x);
			float depth = r.w;

			point_cloud_entry& entry = entries(y, x);
			entry.position = vec3(0.f, 0.f, 0.f);

			if (depth > 0.f)
			{
				vec2 u = unprojectTable(y, x);

				entry.position = vec3(u, -1.f) * depth;
				entry.normal = r.xyz;

				++numEntries;
			}
		}
	}
}

image<uint8> image_point_cloud::createValidMask()
{
	image<uint8> result;
	result.convertFrom(entries, [](const point_cloud_entry& entry) -> uint8
		{
			return entry.position.z != 0.f ? 255 : 0;
		});
	return result;
}

static void writeHeaderToFile(std::ofstream& outfile, uint32 numPoints, bool writeNormals, bool writeColors, uint32 numLines = 0)
{
	const char* format_header = "binary_little_endian 1.0";
	outfile << "ply" << std::endl
		<< "format " << format_header << std::endl
		<< "comment scan3d-capture generated" << std::endl
		<< "element vertex " << numPoints << std::endl
		<< "property float x" << std::endl
		<< "property float y" << std::endl
		<< "property float z" << std::endl;

	if (writeNormals)
	{
		outfile << "property float nx" << std::endl
			<< "property float ny" << std::endl
			<< "property float nz" << std::endl;
	}

	if (writeColors)
	{
		outfile << "property uchar red" << std::endl
			<< "property uchar green" << std::endl
			<< "property uchar blue" << std::endl
			<< "property uchar alpha" << std::endl;
	}

	if (numLines > 0)
	{
		outfile << "element edge " << numLines << std::endl
			<< "property int vertex1" << std::endl
			<< "property int vertex2" << std::endl;

		outfile << "property uchar red" << std::endl
			<< "property uchar green" << std::endl
			<< "property uchar blue" << std::endl
			<< "property uchar alpha" << std::endl;
	}

	outfile << "element face 0" << std::endl
		<< "property list uchar int vertex_indices" << std::endl
		<< "end_header" << std::endl;
}

static void writeVec3ToFile(std::ofstream& outfile, const vec3& v)
{
	outfile.write(reinterpret_cast<const char*>(&(v.x)), sizeof(float));
	outfile.write(reinterpret_cast<const char*>(&(v.y)), sizeof(float));
	outfile.write(reinterpret_cast<const char*>(&(v.z)), sizeof(float));
}

static void writeColorToFile(std::ofstream& outfile, const color_bgra& c)
{
	outfile.write(reinterpret_cast<const char*>(&(c.r)), sizeof(unsigned char));
	outfile.write(reinterpret_cast<const char*>(&(c.g)), sizeof(unsigned char));
	outfile.write(reinterpret_cast<const char*>(&(c.b)), sizeof(unsigned char));
	outfile.write(reinterpret_cast<const char*>(&(c.a)), sizeof(unsigned char));
}

static void writeIntToFile(std::ofstream& outfile, int i)
{
	outfile.write(reinterpret_cast<const char*>(&i), sizeof(int));
}

static bool outputEntriesArray(const fs::path& path, const point_cloud_entry* entries, uint32 count, uint32 numValid)
{
	std::ofstream outfile;
	std::ios::openmode mode = std::ios::out | std::ios::trunc | std::ios::binary;
	outfile.open(path, mode);
	if (!outfile.is_open())
	{
		return false;
	}

	int numPoints = numValid;

	writeHeaderToFile(outfile, numPoints, true, false);

	for (uint32 i = 0; i < count; ++i)
	{
		const point_cloud_entry& entry = entries[i];

		if (entry.position.z != 0.f)
		{
			writeVec3ToFile(outfile, entry.position);
			writeVec3ToFile(outfile, entry.normal);
		}
	}

	outfile.close();
	LOG_MESSAGE("Wrote point cloud with %u entries to file '%ws'", numValid, path.c_str());
	return true;
}

bool image_point_cloud::writeToImage(const fs::path& path)
{
	image<uint8> output = createValidMask();

	DirectX::Image image;
	image.width = output.width;
	image.height = output.height;
	image.format = DXGI_FORMAT_R8_UNORM;
	image.rowPitch = output.width * getFormatSize(image.format);
	image.slicePitch = image.rowPitch * image.height;
	image.pixels = output.data;

	bool result = saveImageToFile(path, image);

	if (result)
	{
		LOG_MESSAGE("Wrote point cloud image to file '%ws'", path.c_str());
	}
	return result;
}

bool image_point_cloud::writeToFile(const fs::path& path)
{
	return outputEntriesArray(path, entries.data, entries.width * entries.height, numEntries);
}
