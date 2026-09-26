// ##########################################################################
// #                                                                        #
// #                            CLOUDCOMPARE                                #
// #                                                                        #
// #  This program is free software; you can redistribute it and/or modify  #
// #  it under the terms of the GNU General Public License as published by  #
// #  the Free Software Foundation; version 2 or later of the License.      #
// #                                                                        #
// #  This program is distributed in the hope that it will be useful,       #
// #  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
// #  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
// #  GNU General Public License for more details.                          #
// #                                                                        #
// #          COPYRIGHT: CloudCompare project                               #
// #                                                                        #
// ##########################################################################

// Local
#include <vsg/ccVSGPointCloudBuilder.h>
#include <vsg/ccVSGShaders.h>

// qCC_db
#include <ccPointCloud.h>
#include <ccScalarField.h>

// VSG
#include <vsg/all.h>

// system
#include <algorithm>
#include <cmath>

ccVSGPointCloudBuilder::ccVSGPointCloudBuilder()
    : m_shaderSet(ccVSGShaders::createPointCloudShaderSet())
    , m_sharedObjects(vsg::SharedObjects::create())
    , m_pointSizeData(vsg::floatValue::create(1.0f))
{
}

void ccVSGPointCloudBuilder::setPointSize(float size)
{
	if (m_pointSizeData && m_pointSizeData->value() != size)
	{
		m_pointSizeData->value() = size;
		// triggers the transfer of the new value to the GPU (see vsg::TransferTask)
		m_pointSizeData->dirty();
	}
}

float ccVSGPointCloudBuilder::pointSize() const
{
	return m_pointSizeData ? m_pointSizeData->value() : 1.0f;
}

vsg::ref_ptr<vsg::Node> ccVSGPointCloudBuilder::build(ccPointCloud* cloud, const ccColor::Rgba& defaultColor)
{
	if (!cloud || cloud->size() == 0 || !m_shaderSet)
	{
		return {};
	}

	// scalar field rendering?
	// NOTE: the colors are resolved on the CPU with ccScalarField::getColor()
	// so that we follow exactly the CloudCompare color logic (color scale,
	// ramp steps, out of range color, ...). A GPU color ramp texture would
	// avoid re-uploading the colors when only the color scale changes - it
	// can be added later as an optimization.
	ccScalarField* sf = cloud->getCurrentDisplayedScalarField();
	const bool     useScalarField = (sf != nullptr) && sf->getColorScale();

	const unsigned count     = cloud->size();
	const bool     useColors = !useScalarField && cloud->hasColors();

	auto root = vsg::Group::create();

	for (unsigned first = 0; first < count; first += static_cast<unsigned>(ChunkSize))
	{
		const std::size_t chunkCount = std::min(static_cast<std::size_t>(ChunkSize),
		                                        static_cast<std::size_t>(count - first));

		auto vertices = vsg::vec3Array::create(chunkCount);
		auto colors   = vsg::ubvec4Array::create(chunkCount);

		double minX = 0.0, minY = 0.0, minZ = 0.0;
		double maxX = 0.0, maxY = 0.0, maxZ = 0.0;

		for (std::size_t i = 0; i < chunkCount; ++i)
		{
			const unsigned  index = first + static_cast<unsigned>(i);
			const CCVector3 P     = *cloud->getPoint(index);

			(*vertices)[i] = vsg::vec3(static_cast<float>(P.x), static_cast<float>(P.y), static_cast<float>(P.z));

			// NOTE: like the OpenGL backend, rendering uses the **local**
			// (i.e. shifted) coordinates stored in the cloud - the global
			// shift/scale of ccShiftedObject is NOT applied here (it is only
			// used when displaying or exporting coordinates, see
			// ccShiftedObject::toGlobal3d()).
			if (useScalarField)
			{
				const ccColor::Rgb* col = sf->getColor(sf->getValue(index));
				const ccColor::Rgb  rgb = col ? *col : ccColor::lightGreyRGB;
				(*colors)[i]            = vsg::ubvec4(rgb.r, rgb.g, rgb.b, 255);
			}
			else
			{
				const ccColor::Rgba& C = useColors ? cloud->getPointColor(index) : defaultColor;
				(*colors)[i]           = vsg::ubvec4(C.r, C.g, C.b, C.a);
			}

			if (i == 0)
			{
				minX = maxX = P.x;
				minY = maxY = P.y;
				minZ = maxZ = P.z;
			}
			else
			{
				minX = std::min(minX, static_cast<double>(P.x));
				minY = std::min(minY, static_cast<double>(P.y));
				minZ = std::min(minZ, static_cast<double>(P.z));
				maxX = std::max(maxX, static_cast<double>(P.x));
				maxY = std::max(maxY, static_cast<double>(P.y));
				maxZ = std::max(maxZ, static_cast<double>(P.z));
			}
		}

		// pipeline / descriptor set
		auto config = vsg::GraphicsPipelineConfigurator::create(m_shaderSet);
		vsg::DataList arrays;

		// canonical VSG pattern (see vsg::Builder): enableArray declares the
		// vertex-input layout of the pipeline, then the arrays are filled in
		// the SAME order and handed to the draw. Do NOT also call
		// assignArray(arrays, ...) here - that would double-register the
		// attributes and create inconsistent vertex bindings.
		config->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
		config->enableArray("vsg_Color", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::ubvec4), VK_FORMAT_R8G8B8A8_UNORM);
		config->init();

		// arrays must match the enableArray order (vsg_Vertex at binding 0,
		// vsg_Color at binding 1) so the draw binds them to the right slots.
		arrays.push_back(vertices);
		arrays.push_back(colors);

		auto stateGroup = vsg::StateGroup::create();
		config->copyTo(stateGroup, m_sharedObjects);

		auto draw = vsg::VertexDraw::create();
		draw->assignArrays(arrays);
		draw->vertexCount   = static_cast<uint32_t>(chunkCount);
		draw->instanceCount = 1;

		stateGroup->addChild(draw);

		// bounding sphere for frustum culling
		const vsg::dvec3 center((minX + maxX) * 0.5, (minY + maxY) * 0.5, (minZ + maxZ) * 0.5);
		const double     radius = 0.5 * std::sqrt((maxX - minX) * (maxX - minX)
		                                          + (maxY - minY) * (maxY - minY)
		                                          + (maxZ - minZ) * (maxZ - minZ));

		auto cullNode = vsg::CullNode::create();
		cullNode->bound.set(center.x, center.y, center.z, radius + 1.0); // +1: point size margin
		cullNode->child = stateGroup;

		root->addChild(cullNode);
	}

	return root;
}
