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
#include <vsg/ccVSGShaders.h>

// VSG
#include <vsg/all.h>

namespace
{
	// clang-format off
	const char* s_pointCloudVertexSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

#define VIEW_DESCRIPTOR_SET 0
#define MATERIAL_DESCRIPTOR_SET 1

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 modelView;
} pc;

layout(set = MATERIAL_DESCRIPTOR_SET, binding = 0) uniform PointSizeData {
    float value;
} pointSize;

layout(location = 0) in vec3 vsg_Vertex;
layout(location = 6) in vec4 vsg_Color;

layout(location = 0) out vec4 vertexColor;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
};

void main()
{
    gl_Position = (pc.projection * pc.modelView) * vec4(vsg_Vertex, 1.0);
    gl_PointSize = pointSize.value;
    vertexColor  = vsg_Color;
}
)";

	const char* s_pointCloudFragmentSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 vertexColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vertexColor;
}
)";
	// clang-format on
} // namespace

const char* ccVSGShaders::pointCloudVertexSource()
{
	return s_pointCloudVertexSource;
}

const char* ccVSGShaders::pointCloudFragmentSource()
{
	return s_pointCloudFragmentSource;
}

vsg::ref_ptr<vsg::ShaderSet> ccVSGShaders::createPointCloudShaderSet()
{
	vsg::ShaderStages stages{
	    vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", s_pointCloudVertexSource),
	    vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", s_pointCloudFragmentSource)};

	auto shaderSet = vsg::ShaderSet::create(stages);

	// attributes: same locations as the standard VSG shader sets
	shaderSet->addAttributeBinding("vsg_Vertex", "", 0, VK_FORMAT_R32G32B32_SFLOAT, {});
	shaderSet->addAttributeBinding("vsg_Color", "", 6, VK_FORMAT_R8G8B8A8_UNORM, {});

	// the point size lives in the material descriptor set
	shaderSet->addDescriptorBinding("pointSize", "",
	                                1, // MATERIAL_DESCRIPTOR_SET
	                                0,
	                                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	                                1,
	                                VK_SHADER_STAGE_VERTEX_BIT,
	                                {});

	// VSG pushes 'projection' and 'modelView' itself
	shaderSet->addPushConstantRange("pc", "", VK_SHADER_STAGE_VERTEX_BIT, 0, 128);

	shaderSet->defaultGraphicsPipelineStates = {
	    vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_POINT_LIST),
	    vsg::RasterizationState::create(),
	    vsg::MultisampleState::create(),
	    vsg::ColorBlendState::create(),
	    vsg::DepthStencilState::create()};

	return shaderSet;
}
