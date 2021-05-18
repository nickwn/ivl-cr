#define NOMINMAX

#include "Dicom.h"

#include <filesystem>
#include <exception>
#include <deque>
#include <cstdint>
#include <algorithm>
#include <memory>

#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmdata/dctk.h>

#include "stb_image.h"

Dicom::Dicom(std::string folder, const std::optional<CuttingPlane>& cuttingPlane, MaskMode maskMode)
{
	if (!std::filesystem::exists(folder))
	{
		std::cerr << "folder " + folder + " not found";
		throw std::runtime_error("folder " + folder + " not found");
	}

	struct DcmSlice
	{
		std::unique_ptr<DicomImage> image;
		glm::dvec3 spacing;
		double location;
	};

	std::deque<DcmSlice> slices;
	glm::dvec3 maxSpacing = glm::dvec3(0.0);
	try
	{
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
		{
			DcmFileFormat fileFormat;
			fileFormat.loadFile(entry.path().string().c_str());

			DcmDataset* dataset = fileFormat.getDataset();

			glm::dvec3 spacing;
			dataset->findAndGetFloat64(DCM_PixelSpacing, spacing.x, 0);
			dataset->findAndGetFloat64(DCM_PixelSpacing, spacing.y, 1);
			dataset->findAndGetFloat64(DCM_SliceThickness, spacing.z, 0);

			double location;
			fileFormat.getDataset()->findAndGetFloat64(DCM_SliceLocation, location, 0);

			std::unique_ptr<DicomImage> image = std::make_unique<DicomImage>(entry.path().string().c_str());
			if (image->getStatus() == EI_Status::EIS_Normal)
			{
				slices.push_back({ std::move(image), spacing, location });
				maxSpacing = glm::max(maxSpacing, spacing);
			}
			else
			{
				std::cerr << "error loading dicom " << entry.path().string() << "\n";
			}
		}
	}
	catch (std::exception e)
	{
		std::cerr << e.what();
		throw std::runtime_error("filesystem error");
	}

	std::sort(slices.begin(), slices.end(), [](const DcmSlice& a, const DcmSlice& b) {
		return a.location < b.location;
	});

	const uint32_t w = slices[0].image->getWidth();
	const uint32_t h = slices[0].image->getHeight();
	const uint32_t d = uint32_t(slices.size());

	std::vector<std::uint8_t> mask = std::vector<std::uint8_t>(w * h * d);
	std::string maskFolder = folder + "/mask";
	try
	{
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(maskFolder))
		{
			int tmp_w, tmp_h, tmp_ch;
			std::uint8_t* data = stbi_load(entry.path().string().c_str(), &tmp_w, &tmp_h, &tmp_ch, 0);
			if (data)
			{
				if (tmp_w != w && tmp_h == h)
				{
					throw std::runtime_error("bad mask dimensions");
				}

				std::string layerStr = entry.path().stem().string();
				int layer = std::stoi(layerStr);

				std::size_t offset = layer * w * h;
				std::uint8_t* insertBegin = std::next(&*std::begin(mask), offset);
				if (tmp_ch == 1)
				{
					std::memcpy(insertBegin, data, w * h);
				}
				else
				{
					for (std::size_t i = 0; i < w * h; i++)
					{
						*std::next(insertBegin, i) = data[i * 3];
					}
				}

				stbi_image_free(data);
			}
			else
			{
				std::cerr << "error loading mask " << entry.path().string() << "\n";
			}
		}
	}
	catch (std::exception e)
	{
		std::cerr << e.what();
		throw std::runtime_error("filesystem error");
	}

	glm::vec2 b = glm::vec2(slices[0].location);
	for (const auto& i : slices) 
	{
		b.x = (float)fmin(i.location - i.spacing.z * .5, b.x);
		b.y = (float)fmax(i.location + i.spacing.z * .5, b.y);
	}

	mPhysicalSize = glm::vec3(.001f * glm::vec3(glm::vec2(maxSpacing) * glm::vec2(w, h), b.y - b.x));

	std::vector<uint16_t> data(w * h * d);
	for (size_t i = 0; i < slices.size(); i++)
	{
		slices[i].image->setMinMaxWindow();
		const uint16_t* pixels = reinterpret_cast<const uint16_t*>(slices[i].image->getOutputData(16));
		std::copy(pixels, pixels + w * h, std::begin(data) + w * h * i);
	}

	struct VoxelData
	{
		// | density (15)           | isMask (1) |
		// | density (8) | mask (7) | isMask (1) |
		std::uint16_t data;

		void setIsMask(bool isMask)
		{
			data |= isMask & 0x0001;
		}

		void setMask(std::uint8_t mask)
		{
			data |= (mask << 1) & 0x00FE;
		}

		void setDensity_lowp(std::uint16_t density)
		{
			std::uint8_t compressed = density >> 8;
			data |= (compressed << 8) & 0xFF00;
		}

		void setDensity_highp(std::uint16_t density)
		{
			std::uint16_t compressed = density >> 1;
			std::uint32_t data32 = data;
			std::uint32_t tmp = std::uint32_t(compressed << 8) | 0x0001;
			std::uint32_t tmp2 = data32 & tmp;
			std::uint16_t tmp3 = data32 & tmp;
			data |= std::uint32_t(compressed << 1) & 0xFFFE;
		}
	};

	for (size_t k = 0; k < d; k++)
	{
		for (size_t j = 0; j < h; j++)
		{
			for (size_t i = 0; i < w; i++)
			{
				const std::size_t offset = i + w * j + h * w * k;
				std::uint16_t density = data[offset];
				if (cuttingPlane)
				{
					const glm::vec3& ppoint = cuttingPlane->ppoint;
					const glm::vec3& pnorm = cuttingPlane->pnorm;
					const float tx = (float)i / w - ppoint.x, ty = (float)j / h - ppoint.y, tz = (float)k / d - ppoint.z;
					const float dot = tx * pnorm.x + ty * pnorm.y + tz * pnorm.z;
					if (dot > 0)
					{
						density = 1;
					}
				}

				VoxelData voxel{ 0 };
				if (mask.size() && maskMode != MaskMode::None)
				{
					if (!mask[offset])
					{
						if (maskMode == MaskMode::Isolate)
						{
							density = 1;
						}

						voxel.setIsMask(false);
						voxel.setDensity_highp(density);
					}
					else
					{
						std::uint8_t maskval = mask[offset];
						voxel.setIsMask(true);
						voxel.setMask(mask[offset]);
						voxel.setDensity_lowp(density);
					}
				}
				else
				{
					voxel.setMask(false);
					voxel.setDensity_highp(density);
				}


				data[offset] = voxel.data;
			}
		}
	}

	glBindTexture(GL_TEXTURE_3D, mUniqueTexture.Get());
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);

	const float color[] = { 0.f, 0.f, 0.f, 1.f };
	glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, &color[0]);

	glTexImage3D(GL_TEXTURE_3D, 0, GL_R16, w, h, d, 0, GL_RED, GL_UNSIGNED_SHORT, data.data());
	//glBindImageTexture(1, mTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R16);

	mDim = glm::ivec3(w, h, d);
}