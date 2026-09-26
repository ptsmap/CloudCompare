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

layout(location = 6) in vec4 vsg_Color;

layout(location = 0) in vec3 vsg_Vertex;

layout(location = 0) out vec4 vertexColor;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
};

void main()
{
    gl_Position = (pc.projection * pc.modelView) * vec4(vsg_Vertex, 1.0);
    // NOTE: Metal (MoltenVK) ignores gl_PointSize for POINT_LIST (points are
    // always 1px), and a vertex-stage UBO for point size makes MoltenVK emit an
    // MTLVertexDescriptor with orphaned buffer layouts that Metal rejects.
    // Real point sizing must be done with billboard sprites (later milestone).
    gl_PointSize = 1.0;
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

	//! Mesh (TRIANGLE_LIST) shader: per vertex color, simple diffuse lighting
	const char* s_meshVertexSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

#define VIEW_DESCRIPTOR_SET 0
#define MATERIAL_DESCRIPTOR_SET 1

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 modelView;
} pc;

layout(location = 0) in vec3 vsg_Vertex;
layout(location = 1) in vec3 vsg_Normal;
layout(location = 6) in vec4 vsg_Color;

layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec3 normalDir;

out gl_PerVertex {
    vec4 gl_Position;
};

void main()
{
    gl_Position = (pc.projection * pc.modelView) * vec4(vsg_Vertex, 1.0);
    vertexColor = vsg_Color;
    normalDir   = mat3(pc.modelView) * vsg_Normal;
}
)";

	const char* s_meshFragmentSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec3 normalDir;
layout(location = 0) out vec4 outColor;

void main()
{
    // cheap two-sided lambert term so that meshes are readable
    vec3  n     = normalize(normalDir);
    float light = abs(dot(n, vec3(0.0, 0.0, 1.0)));
    light       = 0.35 + 0.65 * light;

    outColor = vec4(vertexColor.rgb * light, vertexColor.a);
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

vsg::ref_ptr<vsg::ShaderSet> ccVSGShaders::createMeshShaderSet(VkPrimitiveTopology topology)
{
	vsg::ShaderStages stages{
	    vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", s_meshVertexSource),
	    vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", s_meshFragmentSource)};

	auto shaderSet = vsg::ShaderSet::create(stages);

	shaderSet->addAttributeBinding("vsg_Vertex", "", 0, VK_FORMAT_R32G32B32_SFLOAT, {});
	shaderSet->addAttributeBinding("vsg_Normal", "", 1, VK_FORMAT_R32G32B32_SFLOAT, {});
	shaderSet->addAttributeBinding("vsg_Color", "", 6, VK_FORMAT_R8G8B8A8_UNORM, {});

	shaderSet->addPushConstantRange("pc", "", VK_SHADER_STAGE_VERTEX_BIT, 0, 128);

	shaderSet->defaultGraphicsPipelineStates = {
	    vsg::InputAssemblyState::create(topology),
	    vsg::RasterizationState::create(),
	    vsg::MultisampleState::create(),
	    vsg::ColorBlendState::create(),
	    vsg::DepthStencilState::create()};

	return shaderSet;
}
