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
#include <cstdio>

ccVSGPointCloudBuilder::ccVSGPointCloudBuilder()
    : m_shaderSet(ccVSGShaders::createPointSpriteShaderSet())
    , m_idShaderSet(ccVSGShaders::createPointSpriteIdShaderSet())
    , m_sharedObjects(vsg::SharedObjects::create())
    , m_quadCorners(vsg::vec3Array::create(4))
{
	updateQuadCorners();
}

void ccVSGPointCloudBuilder::setPointSize(float size)
{
	if (std::fabs(m_pointSize - size) < 1.0e-6f)
	{
		return;
	}

	m_pointSize = size;
	updateQuadCorners();
}

float ccVSGPointCloudBuilder::pointSize() const
{
	return m_pointSize;
}

void ccVSGPointCloudBuilder::setViewportSize(int width, int height)
{
	const int w = std::max(width, 1);
	const int h = std::max(height, 1);

	if (w == m_viewportWidth && h == m_viewportHeight)
	{
		return;
	}

	m_viewportWidth  = w;
	m_viewportHeight = h;
	updateQuadCorners();
}

void ccVSGPointCloudBuilder::updateQuadCorners()
{
	if (!m_quadCorners || m_quadCorners->size() != 4)
	{
		return;
	}

	// The corners are stored as an offset in normalized device coordinates:
	// the NDC range covers 2.0 for the whole viewport, hence 'pointSize' pixels
	// are halfWidth = pointSize / viewportWidth in NDC units. The quad is then
	// expanded in screen space by the vertex shader, so the size stays constant
	// in pixels whatever the distance to the camera (like gl_PointSize).
	const float halfW = m_pointSize / static_cast<float>(m_viewportWidth);
	const float halfH = m_pointSize / static_cast<float>(m_viewportHeight);

	(*m_quadCorners)[0].set(-halfW, -halfH, 0.0f);
	(*m_quadCorners)[1].set(halfW, -halfH, 0.0f);
	(*m_quadCorners)[2].set(-halfW, halfH, 0.0f);
	(*m_quadCorners)[3].set(halfW, halfH, 0.0f);

	// re-upload the (shared) buffer: vsg::Data::dirty() does not require the
	// pipeline to be recompiled
	m_quadCorners->dirty();
}

namespace
{
	//! Deterministic 32 bit integer hash (murmur3 finalizer)
	/** Used to thin the points without any RNG state, so that a given cloud
	    always produces the very same LOD subsets. **/
	inline uint32_t hash32(uint32_t x)
	{
		x ^= x >> 16;
		x *= 0x7feb352dU;
		x ^= x >> 15;
		x *= 0x846ca68bU;
		x ^= x >> 16;
		return x;
	}

	//! Keeps roughly one point out of 'factor'
	/** The predicate is 'hash32(i) % factor == 0'. Levels built with factor,
	    factor^2, ... are therefore strictly nested: a coarser level is always
	    a subset of a finer one, which keeps the successive LOD levels
	    coherent with each other (and makes the thinning progressive).

	    A hash is used instead of a fixed stride because the points of a cloud
	    are usually stored in acquisition order: taking one point out of N
	    would then produce visible stripes instead of thinning the cloud
	    uniformly. **/
	std::vector<uint32_t> thinnedIndices(unsigned count, unsigned factor)
	{
		std::vector<uint32_t> subset;
		if (count == 0 || factor <= 1)
		{
			return subset;
		}

		subset.reserve(count / factor + 1);
		for (unsigned i = 0; i < count; ++i)
		{
			if (hash32(i) % factor == 0)
			{
				subset.push_back(i);
			}
		}

		return subset;
	}
} // namespace

ccVSGPointCloudBuilder::PointSet ccVSGPointCloudBuilder::buildPointSet(
    ccPointCloud*                cloud,
    const ccColor::Rgba&         defaultColor,
    uint32_t                     entityId,
    ccScalarField*               sf,
    bool                         useScalarField,
    bool                         useColors,
    const std::vector<uint32_t>* subset,
    bool                         withIds)
{
	PointSet result;

	if (!cloud)
	{
		return result;
	}

	// 'count' and the chunk boundaries refer to the subset when there is one
	const unsigned count = subset ? static_cast<unsigned>(subset->size()) : cloud->size();
	if (count == 0)
	{
		return result;
	}

	auto root = vsg::Group::create();
	// parallel tree used by the entity picking pass (M6.1): same geometry,
	// but the fragment stage writes the entity ID (R32_UINT attachment)
	auto idsRoot = vsg::Group::create();

	// bounding box of the whole point set: the vsg::LOD node needs a bound
	// that covers every level, not only the (smaller) decimated one
	double gMinX = 0.0, gMinY = 0.0, gMinZ = 0.0;
	double gMaxX = 0.0, gMaxY = 0.0, gMaxZ = 0.0;
	bool   haveBound = false;

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
			const unsigned  index = subset ? (*subset)[static_cast<std::size_t>(first) + i]
			                               : (first + static_cast<unsigned>(i));
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
		// The points are drawn as **billboard quads**: a 4 vertex triangle strip
		// instanced once per point. Metal/MoltenVK ignores gl_PointSize (points
		// are always 1px there - see R1 of the migration plan), so expanding a
		// quad in screen space is the only way to get a real point size.
		auto config = vsg::GraphicsPipelineConfigurator::create(m_shaderSet);
		vsg::DataList arrays;

		// canonical VSG pattern (see vsg::Builder): enableArray declares the
		// vertex-input layout of the pipeline, then the arrays are filled in
		// the SAME order and handed to the draw. Do NOT also call
		// assignArray(arrays, ...) here - that would double-register the
		// attributes and create inconsistent vertex bindings.
		// The quad corners are per vertex, the point data is per instance.
		config->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
		config->enableArray("cc_PointPosition", VK_VERTEX_INPUT_RATE_INSTANCE, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
		config->enableArray("vsg_Color", VK_VERTEX_INPUT_RATE_INSTANCE, sizeof(vsg::ubvec4), VK_FORMAT_R8G8B8A8_UNORM);
		config->init();

		// arrays must match the enableArray order (quad corners at binding 0,
		// point positions at binding 1, colors at binding 2) so the draw binds
		// them to the right slots.
		arrays.push_back(m_quadCorners); // shared by every chunk and cloud
		arrays.push_back(vertices);
		arrays.push_back(colors);

		auto stateGroup = vsg::StateGroup::create();
		config->copyTo(stateGroup, m_sharedObjects);

		auto draw = vsg::VertexDraw::create();
		draw->assignArrays(arrays);
		draw->firstVertex   = 0;
		draw->vertexCount   = 4;
		draw->firstInstance = 0;
		draw->instanceCount = static_cast<uint32_t>(chunkCount);

		stateGroup->addChild(draw);

		// merge the chunk bounds into the bounds of the whole point set
		if (!haveBound)
		{
			gMinX = minX;
			gMinY = minY;
			gMinZ = minZ;
			gMaxX = maxX;
			gMaxY = maxY;
			gMaxZ = maxZ;
			haveBound = true;
		}
		else
		{
			gMinX = std::min(gMinX, minX);
			gMinY = std::min(gMinY, minY);
			gMinZ = std::min(gMinZ, minZ);
			gMaxX = std::max(gMaxX, maxX);
			gMaxY = std::max(gMaxY, maxY);
			gMaxZ = std::max(gMaxZ, maxZ);
		}

		// bounding sphere for frustum culling
		const vsg::dvec3 center((minX + maxX) * 0.5, (minY + maxY) * 0.5, (minZ + maxZ) * 0.5);
		const double     radius = 0.5 * std::sqrt((maxX - minX) * (maxX - minX)
		                                          + (maxY - minY) * (maxY - minY)
		                                          + (maxZ - minZ) * (maxZ - minZ));

		auto cullNode = vsg::CullNode::create();
		cullNode->bound.set(center.x, center.y, center.z, radius + 1.0); // +1: point size margin
		cullNode->child = stateGroup;

		root->addChild(cullNode);

		// Only the full resolution level is pickable: the picking pass must
		// stay pixel exact with the finest geometry (M7.3).
		if (!withIds)
		{
			continue;
		}

		// ---- picking pass (M6.1) ------------------------------------------
		// Same instanced billboard quads (so that what you see is what you
		// pick), but the fragment stage writes the entity ID. The quad corners
		// and the point positions are shared with the display draw: only the
		// ID array (one uint32 per instance) is new.
		auto ids = vsg::uintArray::create(chunkCount);
		std::fill(ids->begin(), ids->end(), entityId);

		auto idConfig = vsg::GraphicsPipelineConfigurator::create(m_idShaderSet);
		idConfig->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
		idConfig->enableArray("cc_PointPosition", VK_VERTEX_INPUT_RATE_INSTANCE, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
		idConfig->enableArray("cc_EntityId", VK_VERTEX_INPUT_RATE_INSTANCE, sizeof(uint32_t), VK_FORMAT_R32_UINT);
		idConfig->init();

		vsg::DataList idArrays;
		idArrays.push_back(m_quadCorners); // shared by every chunk and cloud
		idArrays.push_back(vertices);      // shared with the display draw
		idArrays.push_back(ids);

		auto idStateGroup = vsg::StateGroup::create();
		idConfig->copyTo(idStateGroup, m_sharedObjects);

		auto idDraw = vsg::VertexDraw::create();
		idDraw->assignArrays(idArrays);
		idDraw->firstVertex   = 0;
		idDraw->vertexCount   = 4;
		idDraw->firstInstance = 0;
		idDraw->instanceCount = static_cast<uint32_t>(chunkCount);

		idStateGroup->addChild(idDraw);

		auto idCullNode = vsg::CullNode::create();
		idCullNode->bound.set(center.x, center.y, center.z, radius + 1.0);
		idCullNode->child = idStateGroup;

		idsRoot->addChild(idCullNode);
	}

	result.root = root;
	if (withIds)
	{
		result.idsRoot = idsRoot;
	}

	if (haveBound)
	{
		const vsg::dvec3 gCenter((gMinX + gMaxX) * 0.5, (gMinY + gMaxY) * 0.5, (gMinZ + gMaxZ) * 0.5);
		const double     gRadius = 0.5 * std::sqrt((gMaxX - gMinX) * (gMaxX - gMinX)
		                                           + (gMaxY - gMinY) * (gMaxY - gMinY)
		                                           + (gMaxZ - gMinZ) * (gMaxZ - gMinZ));
		// +1: point size margin. The LOD test compares this radius to the
		// distance to the camera, so it must cover every level.
		result.bound.set(gCenter.x, gCenter.y, gCenter.z, gRadius + 1.0);
	}

	return result;
}

ccVSGBuiltNodes ccVSGPointCloudBuilder::build(ccPointCloud* cloud, const ccColor::Rgba& defaultColor, uint32_t entityId)
{
	if (!cloud || cloud->size() == 0 || !m_shaderSet || !m_idShaderSet)
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

	// ---------------------------------------------------------------------
	// Full resolution: it is what is drawn when the cloud is large on screen
	// and when the camera is idle.
	// ---------------------------------------------------------------------
	PointSet high = buildPointSet(cloud, defaultColor, entityId, sf, useScalarField, useColors, nullptr, true);

	// ---------------------------------------------------------------------
	// M7.3 LOD: a decimated child, selected by vsg::LOD when the cloud is
	// small on screen or while the camera is moving (the window then raises
	// vsg::View::LODScale - see ccVSGWindowInterface::noteCameraMotion()).
	//
	// The picking pass is NOT decimated: it always uses the full resolution
	// geometry, so that "what you see is what you pick" still holds (the same
	// choice was made for the meshes).
	// ---------------------------------------------------------------------
	if (m_lodEnabled && count > MinLODPointCount && high.root)
	{
		const std::vector<uint32_t> coarse = thinnedIndices(count, LODDecimationFactor);

		if (!coarse.empty())
		{
			PointSet low = buildPointSet(cloud, defaultColor, entityId, sf, useScalarField, useColors, &coarse, false);

			if (low.root && !low.root->children.empty())
			{
				auto lod = vsg::LOD::create();
				// the children are ordered from the highest to the lowest
				// resolution: VSG traverses the first child whose
				// minimumScreenHeightRatio is satisfied, and only that one.
				lod->addChild(vsg::LOD::Child{LODSwitchRatio, high.root});
				// 0.0 == always visible: the low resolution child is the
				// fallback of every other level.
				lod->addChild(vsg::LOD::Child{0.0, low.root});
				// the LOD needs an explicit bound: it is used both by the
				// screen height test and by the view frustum culling
				lod->bound = high.bound;

				// Only printed for the clouds that are actually decimated
				// (i.e. the big ones), so it stays readable in the logs.
				std::fprintf(stderr,
				             "[VSG] cloud LOD: %u points -> %u decimated (1 out of %u), ratio %.2f\n",
				             count,
				             static_cast<unsigned>(coarse.size()),
				             LODDecimationFactor,
				             LODSwitchRatio);

				return {lod, high.idsRoot};
			}
		}
	}

	return {high.root, high.idsRoot};
}
