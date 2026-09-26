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
#include <ccGenericMesh.h>
#include <ccGenericPointCloud.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>
#include <ccScalarField.h>

// VSG
#include <vsg/all.h>

// system
#include <algorithm>
#include <cmath>

ccVSGMeshBuilder::ccVSGMeshBuilder()
    : m_meshShaderSet(ccVSGShaders::createMeshShaderSet(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
    , m_lineShaderSet(ccVSGShaders::createMeshShaderSet(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP))
    , m_sharedObjects(vsg::SharedObjects::create())
{
}

vsg::ref_ptr<vsg::Node> ccVSGMeshBuilder::buildMesh(ccGenericMesh* mesh, const ccColor::Rgba& defaultColor)
{
	if (!mesh || mesh->size() == 0 || !m_meshShaderSet)
	{
		return {};
	}

	ccGenericPointCloud* cloud = mesh->getAssociatedCloud();
	if (!cloud)
	{
		return {};
	}

	const unsigned triCount  = mesh->size();
	const unsigned vertCount = triCount * 3;

	const bool useVertexNormals = cloud->hasNormals();
	const bool useTriNormals    = mesh->hasTriNormals();

	// scalar field rendering? (getCurrentDisplayedScalarField() is only
	// available on ccPointCloud, not on the generic interface)
	ccPointCloud*  pc             = dynamic_cast<ccPointCloud*>(cloud);
	ccScalarField* sf             = pc ? pc->getCurrentDisplayedScalarField() : nullptr;
	const bool     useScalarField = (sf != nullptr) && sf->getColorScale();
	const bool     useColors      = !useScalarField && cloud->hasColors();

	auto vertices = vsg::vec3Array::create(vertCount);
	auto normals  = vsg::vec3Array::create(vertCount);
	auto colors   = vsg::ubvec4Array::create(vertCount);

	unsigned out = 0;
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

			(*vertices)[out].set(P->x, P->y, P->z);

			const CCVector3& N = useVertexNormals ? cloud->getPointNormal(idx[k]) : triNormals[k];
			(*normals)[out].set(N.x, N.y, N.z);

			if (useScalarField)
			{
				const ccColor::Rgb* col = sf->getColor(sf->getValue(idx[k]));
				const ccColor::Rgb  rgb = col ? *col : ccColor::lightGreyRGB;
				(*colors)[out]          = vsg::ubvec4(rgb.r, rgb.g, rgb.b, 255);
			}
			else
			{
				const ccColor::Rgba& C = useColors ? cloud->getPointColor(idx[k]) : defaultColor;
				(*colors)[out]         = vsg::ubvec4(C.r, C.g, C.b, C.a);
			}

			++out;
		}
	}

	if (out == 0)
	{
		return {};
	}

	auto config = vsg::GraphicsPipelineConfigurator::create(m_meshShaderSet);
	vsg::DataList arrays;

	config->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
	config->enableArray("vsg_Normal", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
	config->enableArray("vsg_Color", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::ubvec4), VK_FORMAT_R8G8B8A8_UNORM);
	config->assignArray(arrays, "vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, vertices);
	config->assignArray(arrays, "vsg_Normal", VK_VERTEX_INPUT_RATE_VERTEX, normals);
	config->assignArray(arrays, "vsg_Color", VK_VERTEX_INPUT_RATE_VERTEX, colors);
	config->init();

	auto stateGroup = vsg::StateGroup::create();
	config->copyTo(stateGroup, m_sharedObjects);

	auto draw = vsg::VertexDraw::create();
	draw->assignArrays(arrays);
	draw->vertexCount   = out;
	draw->instanceCount = 1;

	stateGroup->addChild(draw);

	return stateGroup;
}

vsg::ref_ptr<vsg::Node> ccVSGMeshBuilder::buildPolyline(ccPolyline* poly, const ccColor::Rgba& defaultColor)
{
	if (!poly || poly->size() < 2 || !m_lineShaderSet)
	{
		return {};
	}

	if (poly->is2DMode())
	{
		// 2D polylines belong to the overlay pass (M5)
		return {};
	}

	const unsigned count = poly->size();

	// a closed polyline needs one extra (duplicated) vertex
	const std::size_t outCount = count + (poly->isClosed() ? 1 : 0);

	auto vertices = vsg::vec3Array::create(outCount);
	auto normals  = vsg::vec3Array::create(outCount);
	auto colors   = vsg::ubvec4Array::create(outCount);

	for (unsigned i = 0; i < outCount; ++i)
	{
		const unsigned  index = (i < count ? i : 0);
		const CCVector3 P     = *poly->getPoint(index);

		(*vertices)[i].set(P.x, P.y, P.z);
		(*normals)[i].set(0.0f, 0.0f, 1.0f);

		// TODO(M4): per vertex colors of a polyline - for now the whole
		// polyline uses the given color
		const ccColor::Rgba& C = defaultColor;
		(*colors)[i]           = vsg::ubvec4(C.r, C.g, C.b, C.a);
	}

	auto config = vsg::GraphicsPipelineConfigurator::create(m_lineShaderSet);
	vsg::DataList arrays;

	config->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
	config->enableArray("vsg_Normal", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
	config->enableArray("vsg_Color", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::ubvec4), VK_FORMAT_R8G8B8A8_UNORM);
	config->assignArray(arrays, "vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, vertices);
	config->assignArray(arrays, "vsg_Normal", VK_VERTEX_INPUT_RATE_VERTEX, normals);
	config->assignArray(arrays, "vsg_Color", VK_VERTEX_INPUT_RATE_VERTEX, colors);
	config->init();

	auto stateGroup = vsg::StateGroup::create();
	config->copyTo(stateGroup, m_sharedObjects);

	auto draw = vsg::VertexDraw::create();
	draw->assignArrays(arrays);
	draw->vertexCount   = static_cast<uint32_t>(outCount);
	draw->instanceCount = 1;

	stateGroup->addChild(draw);

	// TODO(M4): wide lines are not available on this device (see M0 spike), so
	// the polylines are drawn 1 pixel wide. A quad based expansion is needed to
	// honour ccPolyline::getWidth().
	return stateGroup;
}
