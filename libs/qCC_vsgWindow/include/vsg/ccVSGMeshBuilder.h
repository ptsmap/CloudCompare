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

// system
#include <cstdint>

// VSG
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>
#include <vsg/utils/ShaderSet.h>

class ccGenericMesh;
class ccPolyline;
class ccSensor;

namespace vsg
{
	class SharedObjects;
}

//! Builds the VSG representation of the meshes, polylines and sensors
/** Triangles are expanded into a non indexed vertex buffer (like the legacy
    VBO based rendering did), which keeps the code simple and matches the
    per-triangle normals of CloudCompare.

    Supported display modes:
      - solid / wireframe (ccGenericMesh::isShownAsWire())
      - LOD: a vsg::LOD node with a decimated point cloud as the low
        resolution child (the OpenGL backend draws the mesh vertices as points
        when the LOD is activated)
      - transparency: alpha blending + depth write disabled; the caller is
        responsible for putting the node in the depth sorted bin (see
        ccVSGSceneBuilder)

    \warning Line widths greater than 1 are NOT supported by the device
    (see the M0 spike: `wideLines` unavailable). Polylines wider than 1 pixel
    are therefore expanded into quads on the CPU.
**/
class ccVSGMeshBuilder
{
  public:
	ccVSGMeshBuilder();

	//! Builds the VSG nodes of a mesh (TRIANGLE_LIST, or LINE_LIST when wired)
	/** \param transparent optional output: set to true when the entity needs
	                        alpha blending
	    \param entityId     ID written by the picking pass (M6.1) **/
	ccVSGBuiltNodes buildMesh(ccGenericMesh* mesh, const ccColor::Rgba& defaultColor, uint32_t entityId, bool* transparent = nullptr);

	//! Builds the VSG nodes of a polyline
	/** Lines wider than 1 pixel are expanded into quads (see the warning). **/
	ccVSGBuiltNodes buildPolyline(ccPolyline* poly, const ccColor::Rgba& defaultColor, uint32_t entityId, bool* transparent = nullptr);

	//! Builds the VSG nodes of a sensor
	/** GBL sensors are drawn as a head box + legs + axes, camera sensors as a
	    frustum (near plane, side lines, base, arrow and axes) - mirroring
	    ccGBLSensor::drawMeOnly() and ccCameraSensor::drawMeOnly().

	    \warning The sensor is currently **not pickable** (the returned `ids`
	    node is null): its wire geometry is a group of small sub geometries
	    that would each need an ID counterpart - see the TODO in the .cpp. **/
	ccVSGBuiltNodes buildSensor(ccSensor* sensor, uint32_t entityId);

  private:
	//! Lit shader set: solid triangles with per vertex normals
	vsg::ref_ptr<vsg::ShaderSet>     m_meshShaderSet;
	//! Unlit shader sets (lines / wireframes / quads / LOD points)
	vsg::ref_ptr<vsg::ShaderSet>     m_flatTriangleShaderSet;
	vsg::ref_ptr<vsg::ShaderSet>     m_flatLineListShaderSet;
	vsg::ref_ptr<vsg::ShaderSet>     m_flatLineStripShaderSet;
	vsg::ref_ptr<vsg::ShaderSet>     m_pointShaderSet;
	vsg::ref_ptr<vsg::SharedObjects> m_sharedObjects;

	// ----------------------------------------------------------------------
	// Entity picking (M6.1): one R32_UINT shader set per topology
	// ----------------------------------------------------------------------
	vsg::ref_ptr<vsg::ShaderSet> m_triangleIdShaderSet;
	vsg::ref_ptr<vsg::ShaderSet> m_lineListIdShaderSet;
	vsg::ref_ptr<vsg::ShaderSet> m_lineStripIdShaderSet;
};
