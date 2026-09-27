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

	//! Billboard quad ("point sprite") shader: each point is an instanced quad
	//! that is expanded in screen space, which is the only way to get a real
	//! point size on Metal/MoltenVK (R1 - gl_PointSize is ignored there).
	//!
	//! `vsg_Vertex` carries the quad corner as a **normalized device coordinate
	//! offset** (computed on the CPU from the point size and the viewport size),
	//! so the shader needs no extra uniform for either of them.
	const char* s_pointSpriteVertexSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

#define VIEW_DESCRIPTOR_SET 0
#define MATERIAL_DESCRIPTOR_SET 1

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 modelView;
} pc;

layout(location = 0) in vec3 vsg_Vertex;
layout(location = 1) in vec3 cc_PointPosition;
layout(location = 6) in vec4 vsg_Color;

layout(location = 0) out vec4 vertexColor;

void main()
{
    vec4 clip = (pc.projection * pc.modelView) * vec4(cc_PointPosition, 1.0);
    // screen space expansion: scaling the offset by w keeps the quad a
    // constant number of pixels wide after the perspective divide
    clip.xy   += vsg_Vertex.xy * clip.w;
    gl_Position = clip;
    vertexColor = vsg_Color;
}
)";

	//! Unlit ("flat") shader used for lines, wireframes, sensors and the quad
	//! expanded thick lines: the vertex color is simply passed through.
	//! (no gl_PointSize here: it is meaningless for non point topologies)
	const char* s_flatVertexSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 modelView;
} pc;

layout(location = 0) in vec3 vsg_Vertex;
layout(location = 6) in vec4 vsg_Color;

layout(location = 0) out vec4 vertexColor;

void main()
{
    gl_Position = (pc.projection * pc.modelView) * vec4(vsg_Vertex, 1.0);
    vertexColor = vsg_Color;
}
)";

	//! Textured shader: used by the 2D image overlay (M5.6)
	const char* s_texturedVertexSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 modelView;
} pc;

layout(location = 0) in vec3 vsg_Vertex;
layout(location = 1) in vec2 vsg_TexCoord0;
layout(location = 6) in vec4 vsg_Color;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec4 vertexColor;

void main()
{
    gl_Position = (pc.projection * pc.modelView) * vec4(vsg_Vertex, 1.0);
    texCoord    = vsg_TexCoord0;
    vertexColor = vsg_Color;
}
)";

	const char* s_texturedFragmentSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

#define MATERIAL_DESCRIPTOR_SET 1

layout(set = MATERIAL_DESCRIPTOR_SET, binding = 0) uniform sampler2D diffuseMap;

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec4 vertexColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(diffuseMap, texCoord) * vertexColor;
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

vsg::ref_ptr<vsg::ShaderSet> ccVSGShaders::createPointSpriteShaderSet()
{
	vsg::ShaderStages stages{
	    vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", s_pointSpriteVertexSource),
	    vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", s_pointCloudFragmentSource)};

	auto shaderSet = vsg::ShaderSet::create(stages);

	// 'vsg_Vertex' is per vertex (the 4 corners of the quad) while the point
	// data is per instance - the input rate is set by the builder, which calls
	// vsg::GraphicsPipelineConfigurator::enableArray() with either
	// VK_VERTEX_INPUT_RATE_VERTEX or VK_VERTEX_INPUT_RATE_INSTANCE.
	shaderSet->addAttributeBinding("vsg_Vertex", "", 0, VK_FORMAT_R32G32B32_SFLOAT, {});
	shaderSet->addAttributeBinding("cc_PointPosition", "", 1, VK_FORMAT_R32G32B32_SFLOAT, {});
	shaderSet->addAttributeBinding("vsg_Color", "", 6, VK_FORMAT_R8G8B8A8_UNORM, {});

	// VSG pushes 'projection' and 'modelView' itself
	shaderSet->addPushConstantRange("pc", "", VK_SHADER_STAGE_VERTEX_BIT, 0, 128);

	shaderSet->defaultGraphicsPipelineStates = {
	    vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP),
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

vsg::ref_ptr<vsg::ShaderSet> ccVSGShaders::createTexturedShaderSet()
{
	vsg::ShaderStages stages{
	    vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", s_texturedVertexSource),
	    vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", s_texturedFragmentSource)};

	auto shaderSet = vsg::ShaderSet::create(stages);

	shaderSet->addAttributeBinding("vsg_Vertex", "", 0, VK_FORMAT_R32G32B32_SFLOAT, {});
	shaderSet->addAttributeBinding("vsg_TexCoord0", "", 1, VK_FORMAT_R32G32_SFLOAT, {});
	shaderSet->addAttributeBinding("vsg_Color", "", 6, VK_FORMAT_R8G8B8A8_UNORM, {});

	shaderSet->addPushConstantRange("pc", "", VK_SHADER_STAGE_VERTEX_BIT, 0, 128);

	// the image is bound on the material descriptor set (1), binding 0 - the
	// same values as the #define of the shader sources above
	shaderSet->addDescriptorBinding("diffuseMap",
	                                "",
	                                1,
	                                0,
	                                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	                                1,
	                                VK_SHADER_STAGE_FRAGMENT_BIT,
	                                {});

	shaderSet->defaultGraphicsPipelineStates = {
	    vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
	    vsg::RasterizationState::create(),
	    vsg::MultisampleState::create(),
	    vsg::ColorBlendState::create(),
	    vsg::DepthStencilState::create()};

	return shaderSet;
}

vsg::ref_ptr<vsg::ShaderSet> ccVSGShaders::createFlatShaderSet(VkPrimitiveTopology topology)
{
	vsg::ShaderStages stages{
	    vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", s_flatVertexSource),
	    vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", s_pointCloudFragmentSource)};

	auto shaderSet = vsg::ShaderSet::create(stages);

	shaderSet->addAttributeBinding("vsg_Vertex", "", 0, VK_FORMAT_R32G32B32_SFLOAT, {});
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
