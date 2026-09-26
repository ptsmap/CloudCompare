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
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>
#include <vsg/utils/ShaderSet.h>

class ccGenericMesh;
class ccPolyline;

namespace vsg
{
	class SharedObjects;
}

//! Builds the VSG representation of the meshes and polylines
/** Triangles are expanded into a non indexed vertex buffer (like the legacy
    VBO based rendering did), which keeps the code simple and matches the
    per-triangle normals of CloudCompare.

    \warning Line widths greater than 1 are NOT supported by the device
    (see the M0 spike: `wideLines` unavailable). Polylines are therefore drawn
    with 1 pixel lines for now - a quad based expansion is required to support
    thicker lines (M4 follow-up).
**/
class ccVSGMeshBuilder
{
  public:
	ccVSGMeshBuilder();

	//! Builds the VSG node of a mesh (TRIANGLE_LIST)
	vsg::ref_ptr<vsg::Node> buildMesh(ccGenericMesh* mesh, const ccColor::Rgba& defaultColor);

	//! Builds the VSG node of a polyline (LINE_STRIP)
	vsg::ref_ptr<vsg::Node> buildPolyline(ccPolyline* poly, const ccColor::Rgba& defaultColor);

  private:
	vsg::ref_ptr<vsg::ShaderSet>     m_meshShaderSet;
	vsg::ref_ptr<vsg::ShaderSet>     m_lineShaderSet;
	vsg::ref_ptr<vsg::SharedObjects> m_sharedObjects;
};
