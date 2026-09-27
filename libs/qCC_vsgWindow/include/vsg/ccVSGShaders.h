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

// VSG
#include <vsg/core/ref_ptr.h>
#include <vsg/utils/ShaderSet.h>

//! Shader sets specific to the CloudCompare render entities
/** VSG's built-in shader sets (flat/phong/pbr) hardcode `gl_PointSize = 1.0`
    (see the VSG_POINT_SPRITE block of the generated flat_ShaderSet), which is
    not enough for CloudCompare: the point size is a first class interaction
    (mouse wheel + Alt). Hence our own point cloud shader set.

    It follows the standard VSG conventions so that it plugs into the normal
    vsg::View / GraphicsPipelineConfigurator machinery:
      - push constants: `mat4 projection; mat4 modelView;` (128 bytes, set 0)
      - attributes:     `vsg_Vertex` (location 0), `vsg_Color` (location 6)
      - material data:  descriptor set 1 (MATERIAL_DESCRIPTOR_SET)
**/
//! Define enabling the scalar field (color ramp) variant of a shader set
#define CC_SCALAR_FIELD_DEFINE "CC_SCALAR_FIELD"

class ccVSGShaders
{
  public:
	//! Returns the shader set used to render point clouds (POINT_LIST)
	/** Supports two variants selected with the CC_SCALAR_FIELD define:
	    - default:               per point RGBA color (`vsg_Color`)
	    - CC_SCALAR_FIELD:       per point scalar coordinate (`vsg_Scalar`)
	                             + a color ramp texture sampled by the shader

	    \warning Metal/MoltenVK ignores `gl_PointSize`, so this shader set can
	    only draw 1 pixel points. Use createPointSpriteShaderSet() to get a
	    real, pixel sized point (see R1 of the migration plan).
	 **/
	static vsg::ref_ptr<vsg::ShaderSet> createPointCloudShaderSet();

	//! Returns the shader set used to render point clouds as billboard quads
	/** Each point is drawn as an instanced triangle strip quad that is expanded
	    in **screen space** (`clip.xy += corner * clip.w`), which is the only way
	    to get a real point size on Metal/MoltenVK (R1).

	    The vertex attributes are:
	      - `vsg_Vertex`       (per vertex)   the quad corner, already expressed
	                                          as a normalized device coordinate
	                                          offset (see ccVSGPointCloudBuilder)
	      - `cc_PointPosition` (per instance) the point position
	      - `vsg_Color`        (per instance) the point color
	 **/
	static vsg::ref_ptr<vsg::ShaderSet> createPointSpriteShaderSet();

	//! Returns the shader set used to render meshes
	/** \param topology VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST (solid) or
	                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST (wireframe)
	 **/
	static vsg::ref_ptr<vsg::ShaderSet> createMeshShaderSet(VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//! Returns an unlit "flat" shader set: the vertex color is passed through
	/** Used for the entities that must not be lit: wireframes, sensor wire
	    geometry and the quad expanded thick lines. **/
	static vsg::ref_ptr<vsg::ShaderSet> createFlatShaderSet(VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//! Returns a textured shader set (used by the 2D image overlay, M5.6)
	/** Samples `diffuseMap` and multiplies the result by the vertex color,
	    whose alpha channel carries the global opacity of the overlay. **/
	static vsg::ref_ptr<vsg::ShaderSet> createTexturedShaderSet();

	// ----------------------------------------------------------------------
	// Entity picking (M6.1)
	// ----------------------------------------------------------------------

	//! Returns the shader set of the **entity picking** pass for point clouds
	/** The picking pass renders the scene a second time into an offscreen
	    `VK_FORMAT_R32_UINT` attachment: every entity writes the unique ID of
	    the `ccHObject` it stands for, and the CPU reads the attachment back to
	    know which entity is under the cursor (see M6.1 of the migration plan).

	    The geometry is exactly the same as the displayed one (billboard quads,
	    same point size) so that what you see is what you pick: only the
	    fragment output differs. The entity ID is a **per instance** attribute
	    (`cc_EntityId`), which avoids adding a second push constant range on
	    top of the `projection` / `modelView` one VSG owns (offset 0..128).

	    \warning The ID must be sampled with `flat` interpolation (integers
	    cannot be interpolated).
	 **/
	static vsg::ref_ptr<vsg::ShaderSet> createPointSpriteIdShaderSet();

	//! Returns the shader set of the **entity picking** pass for the meshes,
	//! polylines and sensors (the non instanced counterpart of the above)
	static vsg::ref_ptr<vsg::ShaderSet> createFlatIdShaderSet(VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//! GLSL sources (exposed for tests / offline compilation)
	static const char* pointCloudVertexSource();
	static const char* pointCloudFragmentSource();
};
