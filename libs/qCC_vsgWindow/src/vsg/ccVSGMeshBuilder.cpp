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
#include <vsg/ccVSGMeshBuilder.h>
#include <vsg/ccVSGShaders.h>

// qCC_db
#include <ccCameraSensor.h>
#include <ccGBLSensor.h>
#include <ccGenericMesh.h>
#include <ccGenericPointCloud.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>
#include <ccScalarField.h>
#include <ccSensor.h>

// VSG
#include <vsg/all.h>

// system
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
	//! Minimum number of triangles above which a decimated LOD child is built
	constexpr unsigned MinLODTriangleCount = 100000;

	//! Screen height ratio under which the LOD falls back to its low resolution child
	constexpr double LODSwitchRatio = 0.25;

	using PointList = std::vector<vsg::vec3>;
	using ColorList = std::vector<ccColor::Rgba>;

	inline vsg::vec3 toVec3(const CCVector3& P)
	{
		return vsg::vec3(static_cast<float>(P.x), static_cast<float>(P.y), static_cast<float>(P.z));
	}

	inline vsg::ubvec4 toColor(const ccColor::Rgba& c)
	{
		return vsg::ubvec4(c.r, c.g, c.b, c.a);
	}

	vsg::dmat4 toVSG(const ccGLMatrix& mat)
	{
		const float* m = mat.data();

		// ccGLMatrix is column major, VSG matrices are built row by row
		return vsg::dmat4(m[0], m[4], m[8],  m[12],
		                  m[1], m[5], m[9],  m[13],
		                  m[2], m[6], m[10], m[14],
		                  m[3], m[7], m[11], m[15]);
	}

	//! Bounding sphere of a point list (used by the vsg::LOD nodes)
	vsg::dsphere computeBound(const PointList& pts)
	{
		if (pts.empty())
		{
			return vsg::dsphere(vsg::dvec3(0.0, 0.0, 0.0), 1.0);
		}

		float mn[3] = {pts[0].x, pts[0].y, pts[0].z};
		float mx[3] = {pts[0].x, pts[0].y, pts[0].z};

		for (const vsg::vec3& p : pts)
		{
			const float v[3] = {p.x, p.y, p.z};
			for (unsigned k = 0; k < 3; ++k)
			{
				mn[k] = std::min(mn[k], v[k]);
				mx[k] = std::max(mx[k], v[k]);
			}
		}

		vsg::dvec3 center((mn[0] + mx[0]) * 0.5, (mn[1] + mx[1]) * 0.5, (mn[2] + mx[2]) * 0.5);

		double radius = 0.0;
		for (unsigned k = 0; k < 3; ++k)
		{
			const double d = static_cast<double>(mx[k]) - static_cast<double>(mn[k]);
			radius += d * d;
		}
		radius = 0.5 * std::sqrt(radius);

		return vsg::dsphere(center, std::max(radius, 1.0));
	}

	//! Builds a draw node from the given arrays
	/** \param normals may be null - the flat shader sets have no normal attribute
	    \param transparent enables alpha blending and disables the depth write
	    \param twoSided    disables the back face culling (lines / sensor quads)
	 **/
	vsg::ref_ptr<vsg::Node> buildGeometry(vsg::ref_ptr<vsg::ShaderSet>     shaderSet,
	                                      vsg::ref_ptr<vsg::SharedObjects> sharedObjects,
	                                      vsg::ref_ptr<vsg::vec3Array>     vertices,
	                                      vsg::ref_ptr<vsg::vec3Array>     normals,
	                                      vsg::ref_ptr<vsg::ubvec4Array>   colors,
	                                      bool                             transparent,
	                                      bool                             twoSided)
	{
		if (!shaderSet || !vertices || !colors || vertices->empty())
		{
			return {};
		}

		const bool hasNormals = normals.valid() && (normals->size() >= vertices->size());

		auto config = vsg::GraphicsPipelineConfigurator::create(shaderSet);

		// NOTE: enableArray() only declares the pipeline vertex layout. The arrays
		// must then be pushed in the very same order and assigned to the draw
		// node. Calling assignArray() in addition would register the attribute /
		// binding twice (4 bindings for 2 arrays), which MoltenVK rejects with an
		// "orphaned buffer layout" Metal validation error.
		config->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
		if (hasNormals)
		{
			config->enableArray("vsg_Normal", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
		}
		config->enableArray("vsg_Color", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::ubvec4), VK_FORMAT_R8G8B8A8_UNORM);

		vsg::DataList arrays;
		arrays.push_back(vertices);
		if (hasNormals)
		{
			arrays.push_back(normals);
		}
		arrays.push_back(colors);

		for (auto& state : config->pipelineStates)
		{
			if (auto* cbs = dynamic_cast<vsg::ColorBlendState*>(state.get()))
			{
				// src_alpha / one_minus_src_alpha blending
				if (transparent)
				{
					cbs->configureAttachments(true);
				}
			}
			else if (auto* dss = dynamic_cast<vsg::DepthStencilState*>(state.get()))
			{
				// transparent entities must not occlude each other
				if (transparent)
				{
					dss->depthWriteEnable = VK_FALSE;
				}
			}
			else if (auto* rs = dynamic_cast<vsg::RasterizationState*>(state.get()))
			{
				if (twoSided)
				{
					rs->cullMode = VK_CULL_MODE_NONE;
				}
			}
		}

		config->init();

		auto stateGroup = vsg::StateGroup::create();
		config->copyTo(stateGroup, sharedObjects);

		auto draw = vsg::VertexDraw::create();
		draw->assignArrays(arrays);
		draw->vertexCount   = static_cast<uint32_t>(vertices->size());
		draw->instanceCount = 1;

		stateGroup->addChild(draw);

		return stateGroup;
	}

	//! Builds the **picking** counterpart of buildGeometry() (M6.1)
	/** Same vertices (shared with the display node), but the fragment stage
	    writes the entity ID into the R32_UINT attachment instead of a color.

	    \warning Blending must stay disabled: Vulkan forbids it on an integer
	    attachment. Back face culling is disabled as well, so that lines and
	    two sided quads remain pickable from both sides.
	 **/
	vsg::ref_ptr<vsg::Node> buildIdGeometry(vsg::ref_ptr<vsg::ShaderSet>     idShaderSet,
	                                        vsg::ref_ptr<vsg::SharedObjects> sharedObjects,
	                                        vsg::ref_ptr<vsg::vec3Array>     vertices,
	                                        uint32_t                         entityId)
	{
		if (!idShaderSet || !vertices || vertices->empty())
		{
			return {};
		}

		// the ID is constant over the whole entity but Vulkan has no "constant
		// vertex attribute": one uint32 per vertex it is (4 extra bytes per
		// vertex, nothing else is duplicated)
		auto ids = vsg::uintArray::create(vertices->size());
		std::fill(ids->begin(), ids->end(), entityId);

		auto config = vsg::GraphicsPipelineConfigurator::create(idShaderSet);
		config->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
		config->enableArray("cc_EntityId", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(uint32_t), VK_FORMAT_R32_UINT);

		for (auto& state : config->pipelineStates)
		{
			if (auto* rs = dynamic_cast<vsg::RasterizationState*>(state.get()))
			{
				rs->cullMode = VK_CULL_MODE_NONE;
			}
		}

		config->init();

		vsg::DataList arrays;
		arrays.push_back(vertices); // shared with the display node
		arrays.push_back(ids);

		auto stateGroup = vsg::StateGroup::create();
		config->copyTo(stateGroup, sharedObjects);

		auto draw = vsg::VertexDraw::create();
		draw->assignArrays(arrays);
		draw->vertexCount   = static_cast<uint32_t>(vertices->size());
		draw->instanceCount = 1;

		stateGroup->addChild(draw);

		return stateGroup;
	}

	//! Builds a LINE_LIST from vertex pairs ('pts' contains 2 points per segment)
	vsg::ref_ptr<vsg::Node> buildSegments(vsg::ref_ptr<vsg::ShaderSet>     lineSet,
	                                      vsg::ref_ptr<vsg::SharedObjects> sharedObjects,
	                                      const PointList&                 pts,
	                                      const ColorList&                 cols)
	{
		const std::size_t count = std::min(pts.size(), cols.size());
		if (count < 2)
		{
			return {};
		}

		auto verts = vsg::vec3Array::create(count);
		auto colors = vsg::ubvec4Array::create(count);

		for (std::size_t i = 0; i < count; ++i)
		{
			(*verts)[i]  = pts[i];
			(*colors)[i] = toColor(cols[i]);
		}

		return buildGeometry(lineSet, sharedObjects, verts, {}, colors, false, true);
	}

	//! Builds a closed (or open) line loop out of a point list
	vsg::ref_ptr<vsg::Node> buildLoop(vsg::ref_ptr<vsg::ShaderSet>     lineSet,
	                                  vsg::ref_ptr<vsg::SharedObjects> sharedObjects,
	                                  const PointList&                 pts,
	                                  const ccColor::Rgba&             color,
	                                  bool                             closed)
	{
		const std::size_t n = pts.size();
		if (n < 2)
		{
			return {};
		}

		const std::size_t segCount = closed ? n : n - 1;

		PointList loopPts;
		ColorList loopCols;
		loopPts.reserve(segCount * 2);
		loopCols.reserve(segCount * 2);

		for (std::size_t i = 0; i < segCount; ++i)
		{
			loopPts.push_back(pts[i]);
			loopPts.push_back(pts[(i + 1) % n]);
			loopCols.push_back(color);
			loopCols.push_back(color);
		}

		return buildSegments(lineSet, sharedObjects, loopPts, loopCols);
	}

	//! Builds the 12 edges of a bounding box
	vsg::ref_ptr<vsg::Node> buildBoxEdges(vsg::ref_ptr<vsg::ShaderSet>     lineSet,
	                                      vsg::ref_ptr<vsg::SharedObjects> sharedObjects,
	                                      const vsg::vec3&                 minC,
	                                      const vsg::vec3&                 maxC,
	                                      const ccColor::Rgba&             color)
	{
		const float x0 = minC.x, y0 = minC.y, z0 = minC.z;
		const float x1 = maxC.x, y1 = maxC.y, z1 = maxC.z;

		const vsg::vec3 corners[8] = {vsg::vec3(x0, y0, z0), vsg::vec3(x1, y0, z0), vsg::vec3(x1, y1, z0), vsg::vec3(x0, y1, z0),
		                              vsg::vec3(x0, y0, z1), vsg::vec3(x1, y0, z1), vsg::vec3(x1, y1, z1), vsg::vec3(x0, y1, z1)};

		static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
		                                {4, 5}, {5, 6}, {6, 7}, {7, 4},
		                                {0, 4}, {1, 5}, {2, 6}, {3, 7}};

		PointList pts;
		pts.reserve(24);
		for (const auto& edge : edges)
		{
			pts.push_back(corners[edge[0]]);
			pts.push_back(corners[edge[1]]);
		}

		return buildSegments(lineSet, sharedObjects, pts, ColorList(pts.size(), color));
	}

	//! Builds a filled quad (2 triangles)
	vsg::ref_ptr<vsg::Node> buildQuad(vsg::ref_ptr<vsg::ShaderSet>     triSet,
	                                  vsg::ref_ptr<vsg::SharedObjects> sharedObjects,
	                                  const vsg::vec3&                 p0,
	                                  const vsg::vec3&                 p1,
	                                  const vsg::vec3&                 p2,
	                                  const vsg::vec3&                 p3,
	                                  const ccColor::Rgba&             color)
	{
		auto verts  = vsg::vec3Array::create(6);
		auto colors = vsg::ubvec4Array::create(6);
		const vsg::ubvec4 c = toColor(color);

		const vsg::vec3 tri[6] = {p0, p1, p2, p0, p2, p3};
		for (unsigned i = 0; i < 6; ++i)
		{
			(*verts)[i]  = tri[i];
			(*colors)[i] = c;
		}

		return buildGeometry(triSet, sharedObjects, verts, {}, colors, false, true);
	}

	//! Builds a filled triangle
	vsg::ref_ptr<vsg::Node> buildTriangle(vsg::ref_ptr<vsg::ShaderSet>     triSet,
	                                      vsg::ref_ptr<vsg::SharedObjects> sharedObjects,
	                                      const vsg::vec3&                 p0,
	                                      const vsg::vec3&                 p1,
	                                      const vsg::vec3&                 p2,
	                                      const ccColor::Rgba&             color)
	{
		auto verts  = vsg::vec3Array::create(3);
		auto colors = vsg::ubvec4Array::create(3);
		const vsg::ubvec4 c = toColor(color);

		const vsg::vec3 tri[3] = {p0, p1, p2};
		for (unsigned i = 0; i < 3; ++i)
		{
			(*verts)[i]  = tri[i];
			(*colors)[i] = c;
		}

		return buildGeometry(triSet, sharedObjects, verts, {}, colors, false, true);
	}

	//! Returns a unit vector perpendicular to 'dir'
	CCVector3 perpendicularTo(const CCVector3& dir)
	{
		CCVector3 ref(0, 0, 1);
		if (std::fabs(static_cast<double>(dir.z)) > 0.9)
		{
			ref = CCVector3(0, 1, 0);
		}

		CCVector3 perp = dir.cross(ref);
		if (perp.norm2() > 0)
		{
			perp.normalize();
		}
		else
		{
			perp = CCVector3(1, 0, 0);
		}

		return perp;
	}
} // namespace

ccVSGMeshBuilder::ccVSGMeshBuilder()
    : m_meshShaderSet(ccVSGShaders::createMeshShaderSet(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
    , m_flatTriangleShaderSet(ccVSGShaders::createFlatShaderSet(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
    , m_flatLineListShaderSet(ccVSGShaders::createFlatShaderSet(VK_PRIMITIVE_TOPOLOGY_LINE_LIST))
    , m_flatLineStripShaderSet(ccVSGShaders::createFlatShaderSet(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP))
    , m_pointShaderSet(ccVSGShaders::createFlatShaderSet(VK_PRIMITIVE_TOPOLOGY_POINT_LIST))
    , m_sharedObjects(vsg::SharedObjects::create())
    , m_triangleIdShaderSet(ccVSGShaders::createFlatIdShaderSet(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
    , m_lineListIdShaderSet(ccVSGShaders::createFlatIdShaderSet(VK_PRIMITIVE_TOPOLOGY_LINE_LIST))
    , m_lineStripIdShaderSet(ccVSGShaders::createFlatIdShaderSet(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP))
{
}

ccVSGBuiltNodes ccVSGMeshBuilder::buildMesh(ccGenericMesh* mesh, const ccColor::Rgba& defaultColor, uint32_t entityId, bool* transparent)
{
	if (transparent)
	{
		*transparent = false;
	}

	if (!mesh || mesh->size() == 0)
	{
		return {};
	}

	ccGenericPointCloud* cloud = mesh->getAssociatedCloud();
	if (!cloud)
	{
		return {};
	}

	const unsigned triCount = mesh->size();

	const bool useVertexNormals = cloud->hasNormals();
	const bool useTriNormals    = mesh->hasTriNormals();

	// scalar field rendering? (getCurrentDisplayedScalarField() is only
	// available on ccPointCloud, not on the generic interface)
	ccPointCloud*  pc             = dynamic_cast<ccPointCloud*>(cloud);
	ccScalarField* sf             = pc ? pc->getCurrentDisplayedScalarField() : nullptr;
	const bool     useScalarField = (sf != nullptr) && sf->getColorScale();
	const bool     useColors      = !useScalarField && cloud->hasColors();

	// gather the triangles (3 vertices per triangle, like the legacy VBO)
	PointList          tv;
	PointList          tn;
	std::vector<vsg::ubvec4> tc;
	tv.reserve(triCount * 3);
	tn.reserve(triCount * 3);
	tc.reserve(triCount * 3);

	bool anyTransparent = false;

	for (unsigned t = 0; t < triCount; ++t)
	{
		const CCCoreLib::VerticesIndexes* tsi = mesh->getTriangleVertIndexes(t);
		if (!tsi)
		{
			continue;
		}

		const unsigned idx[3] = {tsi->i1, tsi->i2, tsi->i3};

		// face normal (used as a fallback)
		CCVector3 faceNormal(0, 0, 1);
		{
			const CCVector3* A = cloud->getPoint(idx[0]);
			const CCVector3* B = cloud->getPoint(idx[1]);
			const CCVector3* C = cloud->getPoint(idx[2]);
			if (A && B && C)
			{
				faceNormal = (*B - *A).cross(*C - *A);
				if (faceNormal.norm2() > 0)
				{
					faceNormal.normalize();
				}
				else
				{
					faceNormal = CCVector3(0, 0, 1);
				}
			}
		}

		CCVector3 triNormals[3] = {faceNormal, faceNormal, faceNormal};
		if (useTriNormals)
		{
			mesh->getTriangleNormals(t, triNormals[0], triNormals[1], triNormals[2]);
		}

		for (unsigned k = 0; k < 3; ++k)
		{
			const CCVector3* P = cloud->getPoint(idx[k]);
			if (!P)
			{
				continue;
			}

			tv.push_back(toVec3(*P));

			const CCVector3& N = useVertexNormals ? cloud->getPointNormal(idx[k]) : triNormals[k];
			tn.push_back(toVec3(N));

			ccColor::Rgba C = defaultColor;
			if (useScalarField)
			{
				const ccColor::Rgb* col = sf->getColor(sf->getValue(idx[k]));
				const ccColor::Rgb  rgb = col ? *col : ccColor::lightGreyRGB;
				C                       = ccColor::Rgba(rgb.r, rgb.g, rgb.b, 255);
			}
			else if (useColors)
			{
				C = cloud->getPointColor(idx[k]);
			}

			if (C.a < 255)
			{
				anyTransparent = true;
			}

			tc.push_back(vsg::ubvec4(C.r, C.g, C.b, C.a));
		}
	}

	if (tv.empty())
	{
		return {};
	}

	if (transparent)
	{
		*transparent = anyTransparent;
	}

	// ---------------------------------------------------------------------
	// wireframe: the 3 edges of every triangle, as a single LINE_LIST
	// ---------------------------------------------------------------------
	if (mesh->isShownAsWire())
	{
		PointList wirePts;
		std::vector<vsg::ubvec4> wireCols;
		wirePts.reserve(tv.size() * 2);
		wireCols.reserve(tv.size() * 2);

		for (std::size_t t = 0; t + 2 < tv.size(); t += 3)
		{
			for (unsigned e = 0; e < 3; ++e)
			{
				const std::size_t a = t + e;
				const std::size_t b = t + ((e + 1) % 3);

				wirePts.push_back(tv[a]);
				wirePts.push_back(tv[b]);
				wireCols.push_back(tc[a]);
				wireCols.push_back(tc[b]);
			}
		}

		auto verts  = vsg::vec3Array::create(wirePts.size());
		auto colors = vsg::ubvec4Array::create(wirePts.size());
		for (std::size_t i = 0; i < wirePts.size(); ++i)
		{
			(*verts)[i]  = wirePts[i];
			(*colors)[i] = wireCols[i];
		}

		return {buildGeometry(m_flatLineListShaderSet, m_sharedObjects, verts, {}, colors, anyTransparent, true),
		        buildIdGeometry(m_lineListIdShaderSet, m_sharedObjects, verts, entityId)};
	}

	// ---------------------------------------------------------------------
	// solid triangles
	// ---------------------------------------------------------------------
	auto verts  = vsg::vec3Array::create(tv.size());
	auto norms  = vsg::vec3Array::create(tv.size());
	auto colors = vsg::ubvec4Array::create(tv.size());

	for (std::size_t i = 0; i < tv.size(); ++i)
	{
		(*verts)[i]  = tv[i];
		(*norms)[i]  = tn[i];
		(*colors)[i] = tc[i];
	}

	vsg::ref_ptr<vsg::Node> highRes = buildGeometry(m_meshShaderSet, m_sharedObjects, verts, norms, colors, anyTransparent, false);
	// picking counterpart (M6.1): the LOD is not reproduced - the picking pass
	// always uses the full resolution geometry
	vsg::ref_ptr<vsg::Node> highResIds = buildIdGeometry(m_triangleIdShaderSet, m_sharedObjects, verts, entityId);

	// ---------------------------------------------------------------------
	// LOD: a decimated point cloud as the low resolution child
	// (the OpenGL backend draws the mesh vertices as points when the LOD is
	//  activated - see ccMesh::drawMeOnly: triangleDisplayType = GL_POINTS)
	// ---------------------------------------------------------------------
	if (triCount > MinLODTriangleCount)
	{
		const std::size_t decimStep = std::max<std::size_t>(
		    1,
		    static_cast<std::size_t>(std::ceil(static_cast<double>(tv.size()) / static_cast<double>(MinLODTriangleCount))));

		PointList                lowPts;
		std::vector<vsg::ubvec4> lowCols;
		lowPts.reserve(tv.size() / decimStep + 1);
		lowCols.reserve(tv.size() / decimStep + 1);

		for (std::size_t i = 0; i < tv.size(); i += decimStep)
		{
			lowPts.push_back(tv[i]);
			lowCols.push_back(tc[i]);
		}

		auto lowVerts  = vsg::vec3Array::create(lowPts.size());
		auto lowColors = vsg::ubvec4Array::create(lowPts.size());
		for (std::size_t i = 0; i < lowPts.size(); ++i)
		{
			(*lowVerts)[i]  = lowPts[i];
			(*lowColors)[i] = lowCols[i];
		}

		vsg::ref_ptr<vsg::Node> lowRes = buildGeometry(m_pointShaderSet, m_sharedObjects, lowVerts, {}, lowColors, anyTransparent, true);

		if (highRes && lowRes)
		{
			auto lod = vsg::LOD::create();
			// the children are ordered from the highest to the lowest resolution:
			// VSG traverses the first child whose minimumScreenHeightRatio is
			// satisfied, and only that one.
			lod->addChild(vsg::LOD::Child{LODSwitchRatio, highRes});
			lod->addChild(vsg::LOD::Child{0.0, lowRes});
			// the LOD needs an explicit bound: it is used both for the screen
			// height test and for the view frustum culling
			lod->bound = computeBound(tv);

			return {lod, highResIds};
		}
	}

	return {highRes, highResIds};
}

ccVSGBuiltNodes ccVSGMeshBuilder::buildPolyline(ccPolyline* poly, const ccColor::Rgba& defaultColor, uint32_t entityId, bool* transparent)
{
	const bool anyTransparent = (defaultColor.a < 255);

	if (transparent)
	{
		*transparent = anyTransparent;
	}

	if (!poly || poly->size() < 2)
	{
		return {};
	}

	if (poly->is2DMode())
	{
		// 2D polylines belong to the overlay pass (M5)
		return {};
	}

	const unsigned count  = poly->size();
	const bool     closed = poly->isClosed();

	const float width = static_cast<float>(poly->getWidth());

	// -------------------------------------------------------------------------
	// 1 pixel lines: the cheap (and exact) path
	// -------------------------------------------------------------------------
	if (width <= 1.0f)
	{
		const std::size_t outCount = count + (closed ? 1 : 0);

		auto verts  = vsg::vec3Array::create(outCount);
		auto colors = vsg::ubvec4Array::create(outCount);
		const vsg::ubvec4 c = toColor(defaultColor);

		for (std::size_t i = 0; i < outCount; ++i)
		{
			const CCVector3 P = *poly->getPoint(i < count ? i : 0);
			(*verts)[i]       = toVec3(P);
			(*colors)[i]      = c;
		}

		return {buildGeometry(m_flatLineStripShaderSet, m_sharedObjects, verts, {}, colors, anyTransparent, true),
		        buildIdGeometry(m_lineStripIdShaderSet, m_sharedObjects, verts, entityId)};
	}

	// -------------------------------------------------------------------------
	// thick lines: quad expansion on the CPU
	//
	// The device has no wide line support (M0 spike: lineWidthRange = [1..1])
	// and a screen space expansion in the vertex shader would require the
	// viewport size, i.e. a vertex stage uniform, which MoltenVK currently
	// rejects (see R1). The thickness is therefore expressed in world units,
	// proportional to the polyline size so that it stays usable at any zoom
	// level. Pixel exact widths remain a TODO.
	// -------------------------------------------------------------------------
	const ccBBox    bb    = poly->getOwnBB(false);
	const CCVector3 diag  = bb.maxCorner() - bb.minCorner();
	const double    bbSize = std::max(std::max(std::fabs(static_cast<double>(diag.x)), std::fabs(static_cast<double>(diag.y))),
                                      std::max(std::fabs(static_cast<double>(diag.z)), 1.0e-9));
	const float     halfWidth = static_cast<float>(width * bbSize * 5.0e-4 * 0.5);

	const std::size_t segCount = closed ? count : count - 1;

	PointList quadPts;
	quadPts.reserve(segCount * 6);

	for (std::size_t i = 0; i < segCount; ++i)
	{
		const CCVector3 A = *poly->getPoint(i);
		const CCVector3 B = *poly->getPoint((i + 1) % count);

		CCVector3 dir = B - A;
		if (dir.norm2() <= 0)
		{
			continue;
		}
		dir.normalize();

		CCVector3 perp = perpendicularTo(dir);
		perp *= halfWidth;

		const vsg::vec3 a = toVec3(A);
		const vsg::vec3 b = toVec3(B);
		const vsg::vec3 p = toVec3(perp);

		const vsg::vec3 a0(a.x - p.x, a.y - p.y, a.z - p.z);
		const vsg::vec3 a1(a.x + p.x, a.y + p.y, a.z + p.z);
		const vsg::vec3 b0(b.x - p.x, b.y - p.y, b.z - p.z);
		const vsg::vec3 b1(b.x + p.x, b.y + p.y, b.z + p.z);

		// two triangles per segment - the joints are not filled (TODO)
		quadPts.push_back(a0);
		quadPts.push_back(a1);
		quadPts.push_back(b1);
		quadPts.push_back(a0);
		quadPts.push_back(b1);
		quadPts.push_back(b0);
	}

	if (quadPts.empty())
	{
		return {};
	}

	auto verts  = vsg::vec3Array::create(quadPts.size());
	auto colors = vsg::ubvec4Array::create(quadPts.size());
	const vsg::ubvec4 c = toColor(defaultColor);

	for (std::size_t i = 0; i < quadPts.size(); ++i)
	{
		(*verts)[i]  = quadPts[i];
		(*colors)[i] = c;
	}

	return {buildGeometry(m_flatTriangleShaderSet, m_sharedObjects, verts, {}, colors, anyTransparent, true),
	        buildIdGeometry(m_triangleIdShaderSet, m_sharedObjects, verts, entityId)};
}

ccVSGBuiltNodes ccVSGMeshBuilder::buildSensor(ccSensor* sensor, uint32_t entityId)
{
	if (!sensor)
	{
		return {};
	}

	// mirrors ccGBLSensor::drawMeOnly() / ccCameraSensor::drawMeOnly(): the
	// geometry is expressed in the sensor local frame and the active position
	// is applied on top of it
	ccIndexedTransformation sensorPos;
	if (!sensor->getActiveAbsoluteTransformation(sensorPos))
	{
		// no visible position for this index!
		return {};
	}

	auto group = vsg::Group::create();

	if (auto* gbl = dynamic_cast<ccGBLSensor*>(sensor))
	{
		// ---- ground based lidar: head box + legs + axes ----
		const PointCoordinateType scale = gbl->getGraphicScale();
		const ccColor::Rgb        rgb   = gbl->getSensorColor();
		const ccColor::Rgba       col(rgb.r, rgb.g, rgb.b, 255);

		constexpr PointCoordinateType halfHeadSize = 0.3;

		// sensor axes (+X red, +Y green, +Z blue)
		{
			const PointCoordinateType axisLength = halfHeadSize * scale;

			PointList pts;
			ColorList cols;

			const ccColor::Rgba axisColors[3] = {ccColor::Rgba(255, 0, 0, 255),
			                                     ccColor::Rgba(0, 255, 0, 255),
			                                     ccColor::Rgba(0, 0, 255, 255)};
			const vsg::vec3     axisDirs[3]   = {vsg::vec3(axisLength, 0.0f, 0.0f),
			                                     vsg::vec3(0.0f, axisLength, 0.0f),
			                                     vsg::vec3(0.0f, 0.0f, axisLength)};

			for (unsigned k = 0; k < 3; ++k)
			{
				pts.push_back(vsg::vec3(0.0f, 0.0f, 0.0f));
				pts.push_back(axisDirs[k]);
				cols.push_back(axisColors[k]);
				cols.push_back(axisColors[k]);
			}

			if (auto node = buildSegments(m_flatLineListShaderSet, m_sharedObjects, pts, cols))
			{
				group->addChild(node);
			}
		}

		// sensor head (wireframe box)
		{
			const float hs = static_cast<float>(halfHeadSize * scale);
			if (auto node = buildBoxEdges(m_flatLineListShaderSet,
			                              m_sharedObjects,
			                              vsg::vec3(-hs, -hs, -hs),
			                              vsg::vec3(hs, hs, hs),
			                              col))
			{
				group->addChild(node);
			}
		}

		// sensor legs
		{
			const float s  = static_cast<float>(scale);
			const float hz = static_cast<float>(halfHeadSize * scale);

			const vsg::vec3 headConnect(0.0f, 0.0f, -hz);
			const vsg::vec3 legEnds[3] = {vsg::vec3(-s, -s, -s),
			                              vsg::vec3(-s, s, -s),
			                              vsg::vec3(s, 0.0f, -s)};

			PointList pts;
			ColorList cols;
			for (unsigned k = 0; k < 3; ++k)
			{
				pts.push_back(headConnect);
				pts.push_back(legEnds[k]);
				cols.push_back(col);
				cols.push_back(col);
			}

			if (auto node = buildSegments(m_flatLineListShaderSet, m_sharedObjects, pts, cols))
			{
				group->addChild(node);
			}
		}
	}
	else if (auto* cam = dynamic_cast<ccCameraSensor*>(sensor))
	{
		// ---- camera sensor: near plane + side lines + base + arrow + axes ----
		const CCVector3 ul = cam->getUpperLeftPoint();
		const float     ulx = static_cast<float>(ul.x);
		const float     uly = static_cast<float>(ul.y);
		const float     ulz = static_cast<float>(ul.z);

		const ccColor::Rgb  rgb = cam->getSensorColor();
		const ccColor::Rgba col(rgb.r, rgb.g, rgb.b, 255);

		const vsg::vec3 nearCorners[4] = {vsg::vec3(ulx, uly, -ulz),
		                                  vsg::vec3(-ulx, uly, -ulz),
		                                  vsg::vec3(-ulx, -uly, -ulz),
		                                  vsg::vec3(ulx, -uly, -ulz)};

		// near plane (the OpenGL backend used a LINE_LOOP)
		{
			PointList pts(nearCorners, nearCorners + 4);
			if (auto node = buildLoop(m_flatLineListShaderSet, m_sharedObjects, pts, col, true))
			{
				group->addChild(node);
			}
		}

		// side lines: from the optical center to the 4 corners
		{
			PointList pts;
			ColorList cols;
			for (unsigned k = 0; k < 4; ++k)
			{
				pts.push_back(vsg::vec3(0.0f, 0.0f, 0.0f));
				pts.push_back(nearCorners[k]);
				cols.push_back(col);
				cols.push_back(col);
			}

			if (auto node = buildSegments(m_flatLineListShaderSet, m_sharedObjects, pts, cols))
			{
				group->addChild(node);
			}
		}

		// base
		{
			const float baseHeight    = 6.0f * uly / 5.0f;
			const float baseHalfWidth = ulx / 5.0f;

			if (auto node = buildQuad(m_flatTriangleShaderSet,
			                          m_sharedObjects,
			                          vsg::vec3(-baseHalfWidth, uly, -ulz),
			                          vsg::vec3(baseHalfWidth, uly, -ulz),
			                          vsg::vec3(baseHalfWidth, baseHeight, -ulz),
			                          vsg::vec3(-baseHalfWidth, baseHeight, -ulz),
			                          col))
			{
				group->addChild(node);
			}
		}

		// arrow
		{
			const float arrowHeight    = 3.0f * uly / 2.0f;
			const float baseHeight     = 6.0f * uly / 5.0f;
			const float arrowHalfWidth = 2.0f * ulx / 5.0f;

			if (auto node = buildTriangle(m_flatTriangleShaderSet,
			                              m_sharedObjects,
			                              vsg::vec3(0.0f, arrowHeight, -ulz),
			                              vsg::vec3(-arrowHalfWidth, baseHeight, -ulz),
			                              vsg::vec3(arrowHalfWidth, baseHeight, -ulz),
			                              col))
			{
				group->addChild(node);
			}
		}

		// frustum (6 faces, each drawn as a line loop - see drawMeOnly)
		if (cam->frustumIsDrawn())
		{
			CCVector3 corners[8];
			if (cam->getFrustumCorners(corners))
			{
				static const int faces[6][4] = {{0, 1, 3, 2},
				                                {2, 3, 5, 4},
				                                {4, 5, 7, 6},
				                                {6, 7, 1, 0},
				                                {6, 0, 2, 4},
				                                {1, 7, 5, 3}};

				for (const auto& face : faces)
				{
					PointList pts;
					for (unsigned k = 0; k < 4; ++k)
					{
						pts.push_back(toVec3(corners[face[k]]));
					}

					if (auto node = buildLoop(m_flatLineListShaderSet, m_sharedObjects, pts, col, true))
					{
						group->addChild(node);
					}
				}
			}
		}

		// axis (+X red, +Y green, -Z blue)
		{
			const float l = ulz / 2.0f;

			PointList pts;
			ColorList cols;

			const ccColor::Rgba axisColors[3] = {ccColor::Rgba(255, 0, 0, 255),
			                                     ccColor::Rgba(0, 255, 0, 255),
			                                     ccColor::Rgba(0, 0, 255, 255)};
			const vsg::vec3     axisDirs[3]   = {vsg::vec3(l, 0.0f, 0.0f),
			                                     vsg::vec3(0.0f, l, 0.0f),
			                                     vsg::vec3(0.0f, 0.0f, -l)};

			for (unsigned k = 0; k < 3; ++k)
			{
				pts.push_back(vsg::vec3(0.0f, 0.0f, 0.0f));
				pts.push_back(axisDirs[k]);
				cols.push_back(axisColors[k]);
				cols.push_back(axisColors[k]);
			}

			if (auto node = buildSegments(m_flatLineListShaderSet, m_sharedObjects, pts, cols))
			{
				group->addChild(node);
			}
		}
	}
	else
	{
		return {};
	}

	if (group->children.empty())
	{
		return {};
	}

	auto transform = vsg::MatrixTransform::create();
	transform->matrix = toVSG(sensorPos);
	transform->addChild(group);

	// TODO(M6): give the sensors a picking counterpart. Their wire geometry is
	// a group of small sub geometries (lines, quads, triangles), each of which
	// would need an ID node built alongside the displayed one.
	(void)entityId;

	return {transform, {}};
}
