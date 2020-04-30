#include "Dicom.h"

#include <filesystem>
#include <exception>
#include <deque>
#include <cstdint>
#include <algorithm>
#include <memory>

#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmdata/dctk.h>

Dicom::Dicom(std::string folder)
{
	if (!std::filesystem::exists(folder))
	{
		std::cerr << "folder " + folder + " not found";
		throw std::runtime_error("folder " + folder + " not found");
	}

	struct DcmSlice
	{
		std::unique_ptr<DicomImage> image;
		double location;
	};

	std::deque<DcmSlice> slices;
	try
	{
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
		{
			DcmFileFormat fileFormat;
			fileFormat.loadFile(entry.path().string().c_str());

			double location;
			fileFormat.getDataset()->findAndGetFloat64(DCM_SliceLocation, location, 0);

			std::unique_ptr<DicomImage> image = std::make_unique<DicomImage>(entry.path().string().c_str());
			if (image->getStatus() == EI_Status::EIS_Normal)
			{
				slices.push_back({ std::move(image), location });
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

	std::vector<uint16_t> data(w * h * d);
	for (size_t i = 0; i < slices.size(); i++)
	{
		slices[i].image->setMinMaxWindow();
		const uint16_t* pixels = reinterpret_cast<const uint16_t*>(slices[i].image->getOutputData(16));
		std::copy(pixels, pixels + w * h, std::begin(data) + w * h * i);
	}

	glGenTextures(1, &mTexture);
	glBindTexture(GL_TEXTURE_3D, mTexture);
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