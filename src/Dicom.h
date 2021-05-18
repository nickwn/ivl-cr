#pragma once

#include <string>
#include <optional>
#include <stdexcept>

#include <gl/glew.h>
#include <glm/glm.hpp>

#include "GLObjects.h"

class Dicom
{
public:
	struct CuttingPlane
	{
		glm::vec3 ppoint;
		glm::vec3 pnorm;
	};

	// see mask color values in common.glsl
	enum class MaskMode
	{
		None, // display volume data with simple gradient specified, no masking
		Body, // display volume data with no mask value like above, and data with a mask value using the respective mask color
		Isolate // dont display volume data with no mask value, and display daat with a mask value using the respective mask color
	};

	static MaskMode ParseMaskMode(std::string_view str)
	{
		if (str == "none")
		{
			return MaskMode::None;
		}
		else if (str == "body")
		{
			return MaskMode::Body;
		}
		else if (str == "isolate")
		{
			return MaskMode::Isolate;
		}
		
		throw std::runtime_error("bad mask mode");
	}

	Dicom(std::string folder, const std::optional<CuttingPlane>& cuttingPlane, MaskMode maskMode);
	const UniqueTexture& GetTexture() const { return mUniqueTexture; }
	UniqueTexture& GetTexture() { return mUniqueTexture; }
	const glm::ivec3& GetScanSize() const { return mDim; }
	const glm::vec3& GetPhysicalSize() const { return mPhysicalSize; }

private:
	UniqueTexture mUniqueTexture;
	glm::ivec3 mDim;
	glm::vec3 mPhysicalSize;
};