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
class ccVSGShaders
{
  public:
	//! Returns the shader set used to render point clouds (POINT_LIST)
	static vsg::ref_ptr<vsg::ShaderSet> createPointCloudShaderSet();

	//! GLSL sources (exposed for tests / offline compilation)
	static const char* pointCloudVertexSource();
	static const char* pointCloudFragmentSource();
};
