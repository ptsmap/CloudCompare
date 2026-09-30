#pragma once
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
#include <qCC_vsgWindow.h>
#include <vsg/ccVSGBuiltNodes.h>

// qCC_db
#include <ccColorTypes.h>

// VSG
#include <vsg/core/Array.h>
#include <vsg/core/Value.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/sphere.h>
#include <vsg/nodes/Node.h>
#include <vsg/utils/ShaderSet.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class ccPointCloud;
class ccScalarField;

namespace vsg
{
	class SharedObjects;
}

//! Builds the VSG representation of a ccPointCloud
/** The cloud is split into chunks (like the historical VBO based rendering) so
    that a big cloud is made of several draw calls, each with its own bounding
    sphere for frustum culling.

    Pipelines, descriptor sets and shaders are shared between the chunks thanks
    to vsg::SharedObjects.
**/
class ccVSGPointCloudBuilder
{
  public:
	//! Number of points per chunk (draw call)
	static constexpr std::size_t ChunkSize = 1 << 16;

	ccVSGPointCloudBuilder();

	//! Sets the point size (in pixels) used by every built node
	/** The points are billboard quads expanded in screen space, so changing the
	    size only re-uploads the (shared) quad corners: the scene graph does not
	    have to be rebuilt. **/
	void setPointSize(float size);

	//! Returns the current point size
	float pointSize() const;

	//! Sets the size (in pixels) of the viewport the points are drawn in
	/** The quad corners are stored as normalized device coordinate offsets, so
	    they must be recomputed whenever the viewport is resized. **/
	void setViewportSize(int width, int height);

	//! Builds the VSG nodes of the given cloud
	/** \param cloud        the cloud to convert
	    \param defaultColor color used when the cloud has no per point color
	    \param entityId     ID written by the picking pass (M6.1). The display
	                        node does not depend on it, so 0 can be passed for
	                        a non pickable entity.
	    \return the display node (a vsg::Group, one child per chunk) and the
	            matching picking node; both share the very same vertex arrays
	 **/
	ccVSGBuiltNodes build(ccPointCloud* cloud, const ccColor::Rgba& defaultColor, uint32_t entityId);

	// ----------------------------------------------------------------------
	// Level of detail (M7.3)
	// ----------------------------------------------------------------------

	//! Number of points above which a decimated LOD child is built
	/** Below that, drawing the whole cloud is already cheap.

	    Note: the OpenGL backend only decimates the clouds above
	    ccGui::Parameters().minLoDCloudSize, whose default is 50 000 000 - far
	    more conservative. The VSG backend draws every point as an instanced
	    billboard quad, which costs more per point than a GL point sprite, so
	    the threshold is lower here. **/
	static constexpr unsigned MinLODPointCount = 2000000;

	//! Fraction of the points kept by the decimated LOD child (1 point out of 8)
	static constexpr unsigned LODDecimationFactor = 8;

	//! Screen height ratio under which the LOD falls back to its low resolution child
	/** The test is `bound.radius > lodDistance * ratio`, so 0.25 keeps the full
	    resolution as long as the cloud is reasonably large on screen. **/
	static constexpr double LODSwitchRatio = 0.25;

	//! Enables or disables the LOD (mirrors ccGLWindowInterface::setLODEnabled)
	void setLODEnabled(bool state)
	{
		m_lodEnabled = state;
	}

	//! Returns whether the LOD is enabled or not
	bool isLODEnabled() const
	{
		return m_lodEnabled;
	}

  private:
	//! Nodes built for one resolution level of a cloud
	struct PointSet
	{
		vsg::ref_ptr<vsg::Group> root;
		//! Picking subtree (built only for the full resolution level)
		vsg::ref_ptr<vsg::Group> idsRoot;
		//! Bounding sphere of the whole point set
		vsg::dsphere             bound;
	};

	//! Builds the chunks of a subset of the cloud
	/** \param subset  indices of the points to draw, or null for every point
	    \param withIds whether the picking subtree must be built as well
	 **/
	PointSet buildPointSet(ccPointCloud*              cloud,
	                       const ccColor::Rgba&       defaultColor,
	                       uint32_t                   entityId,
	                       ccScalarField*             sf,
	                       bool                       useScalarField,
	                       bool                       useColors,
	                       const std::vector<uint32_t>* subset,
	                       bool                       withIds);

	//! Recomputes the (shared) quad corners from the point size and the
	//! viewport size, and re-uploads them to the GPU
	void updateQuadCorners();

	vsg::ref_ptr<vsg::ShaderSet>     m_shaderSet;
	//! Shader set of the entity picking pass (R32_UINT output - M6.1)
	vsg::ref_ptr<vsg::ShaderSet>     m_idShaderSet;
	vsg::ref_ptr<vsg::SharedObjects> m_sharedObjects;

	//! The 4 corners of the billboard quad, as NDC offsets
	/** Shared by every chunk and every cloud: it only depends on the point size
	    and on the viewport size. **/
	vsg::ref_ptr<vsg::vec3Array>     m_quadCorners;

	float m_pointSize      = 1.0f;
	int   m_viewportWidth  = 640;
	int   m_viewportHeight = 480;

	//! Whether the decimated LOD children are built (M7.3)
	bool m_lodEnabled = true;
};
