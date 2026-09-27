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

// qCC_db
#include <ccColorTypes.h>

// VSG
#include <vsg/core/Array.h>
#include <vsg/core/Value.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>
#include <vsg/utils/ShaderSet.h>

#include <cstddef>

class ccPointCloud;

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

	//! Builds the VSG node for the given cloud
	/** \param cloud        the cloud to convert
	    \param defaultColor color used when the cloud has no per point color
	    \return a vsg::Group (one child per chunk), or nullptr on failure
	 **/
	vsg::ref_ptr<vsg::Node> build(ccPointCloud* cloud, const ccColor::Rgba& defaultColor);

  private:
	//! Recomputes the (shared) quad corners from the point size and the
	//! viewport size, and re-uploads them to the GPU
	void updateQuadCorners();

	vsg::ref_ptr<vsg::ShaderSet>     m_shaderSet;
	vsg::ref_ptr<vsg::SharedObjects> m_sharedObjects;

	//! The 4 corners of the billboard quad, as NDC offsets
	/** Shared by every chunk and every cloud: it only depends on the point size
	    and on the viewport size. **/
	vsg::ref_ptr<vsg::vec3Array>     m_quadCorners;

	float m_pointSize      = 1.0f;
	int   m_viewportWidth  = 640;
	int   m_viewportHeight = 480;
};
