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
#include "ccVSGWindowInterface.h"

// qCC_db
#include <cc2DLabel.h>
#include <ccBBox.h>
#include <ccDrawableObject.h>
#include <ccGLMatrix.h>
#include <ccGenericMesh.h>
#include <ccGenericPointCloud.h>
#include <ccHObject.h>
#include <ccHObjectCaster.h>
#include <ccImage.h>
#include <ccLog.h>
#include <ccPointCloud.h>
#include <ccScalarField.h>

// CCCoreLib
#include <CCConst.h>

// VSG
#include <iostream>
#include <vsg/all.h>

// vsgQt
#include <vsgQt/Window.h>

// Qt
#include <QApplication>
#include <QCoreApplication>

// system
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <vector>

namespace
{
	// M7.1: sample count of the offscreen 3D render target. The 3D scene is
	// rendered into a multisampled offscreen color+depth target and resolved
	// into a single-sample color image that the post-process pass samples, so
	// antialiasing (M7.5) is preserved even though the 3D no longer renders
	// straight into the (single-sample) swapchain. Drop to VK_SAMPLE_COUNT_1_BIT
	// if a driver rejects the resolve.
	constexpr VkSampleCountFlagBits CC_VSG_POST_SAMPLES = VK_SAMPLE_COUNT_4_BIT;

	// Full-screen post-process pass (M7.1): an oversized triangle covering the
	// whole clip space, sampling the offscreen 3D color image. The vertex
	// shader forwards the clip-space position (camera matrices are ignored)
	// and the UVs. UV.y is flipped (v = 0 is the top of the offscreen image,
	// matching Vulkan's framebuffer origin) so the image is not upside down.
	const char* s_postVertexSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 vsg_Vertex;
layout(location = 1) in vec2 vsg_TexCoord0;

layout(location = 1) out vec2 uv;

void main()
{
    gl_Position = vec4(vsg_Vertex, 1.0);
    uv = vsg_TexCoord0;
}
)";

	const char* s_postFragmentSource = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform sampler2D colorTexture;

layout(location = 1) in vec2 uv;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(colorTexture, uv);
}
)";

	//! Collects the visible 2D images of the DB tree (M5.6)
	/** CloudCompare draws all the visible images, one on top of the other. **/
	void collectImages(ccHObject* obj, std::vector<const ccImage*>& out)
	{
		if (!obj || !obj->isEnabled())
		{
			return;
		}

		if (obj->isKindOf(CC_TYPES::IMAGE) && (obj->isVisible() || obj->isSelected()))
		{
			out.push_back(static_cast<const ccImage*>(obj));
		}

		for (unsigned i = 0; i < obj->getChildrenNumber(); ++i)
		{
			collectImages(obj->getChild(i), out);
		}
	}

	//! Returns the scalar field currently displayed by an entity (or one of its children)
	ccScalarField* findDisplayedScalarField(ccHObject* obj)
	{
		if (!obj)
		{
			return nullptr;
		}

		if (auto* cloud = dynamic_cast<ccPointCloud*>(obj))
		{
			if (ccScalarField* sf = cloud->getCurrentDisplayedScalarField())
			{
				return sf;
			}
		}

		for (unsigned i = 0; i < obj->getChildrenNumber(); ++i)
		{
			if (ccScalarField* sf = findDisplayedScalarField(obj->getChild(i)))
			{
				return sf;
			}
		}

		return nullptr;
	}

	//! M7.5: resource hints shared by every viewer->compile() so that large
	//! point clouds and scenes with many entities get generous preallocation.
	//! The default 16 MB minimums of vsg::ResourceHints are far too small for
	//! tens of millions of points (the billboard-quad expansion blows up the
	//! per-instance buffers), which made the first compile stutter / over-commit.
	vsg::ref_ptr<vsg::ResourceHints> ccVSGResourceHints()
	{
		auto hints = vsg::ResourceHints::create();

		hints->minimumBufferSize        = 256 * 1024 * 1024;
		hints->minimumDeviceMemorySize  = 1024 * 1024 * 1024;
		hints->minimumStagingBufferSize = 128 * 1024 * 1024;

		// Reserve a generous minimum number of descriptor sets. The per-type
		// descriptor counts are still computed by VSG from the scene graph
		// (descriptorPoolSizes is intentionally left empty), so this only lifts
		// the pool's maxSets floor and never starves any descriptor type.
		hints->numDescriptorSets = 8192;

		return hints;
	}
} // namespace

ccVSGWindowInterface::ccVSGWindowInterface()
{
	m_signalEmitter = new ccVSGWindowSignalEmitter(this);

	// Conservative defaults: they are refined once the Vulkan device is known
	// (see M0/M3 - point size and wide lines are the two critical features).
	m_renderCapabilities.backendName             = QStringLiteral("VSG");
	m_renderCapabilities.pointSizeSupported      = false;
	m_renderCapabilities.maxPointSize            = 1.0f;
	m_renderCapabilities.wideLinesSupported      = false;
	m_renderCapabilities.maxLineWidth            = 1.0f;
	m_renderCapabilities.integerPickingSupported = true;
	m_renderCapabilities.msaaSupported           = true;
}

ccVSGWindowInterface::~ccVSGWindowInterface()
{
	delete m_signalEmitter;
	m_signalEmitter = nullptr;
}

bool ccVSGWindowInterface::initializeViewer(vsg::ref_ptr<vsgQt::Viewer> viewer, vsgQt::Window* vsgWindow)
{
	if (m_initialized || !viewer || !vsgWindow || !vsgWindow->windowAdapter)
	{
		return false;
	}

	m_viewer = viewer;
	m_window = vsgWindow;

	vsg::ref_ptr<vsg::Window> window = vsgWindow->windowAdapter;
	m_viewer->addWindow(window);

	const VkExtent2D& extent = window->extent2D();

	// The camera is driven by the CloudCompare viewport parameters (single
	// source of truth), exposed to VSG through two thin matrix adapters.
	m_viewMatrix       = ccVSGViewMatrix::create();
	m_projectionMatrix = ccVSGProjectionMatrix::create();
	m_camera           = vsg::Camera::create(m_projectionMatrix,
                                             m_viewMatrix,
                                             vsg::ViewportState::create(0, 0, extent.width, extent.height));

	// Query the real device capabilities (M0 spike: never hardcode these, they
	// differ per GPU / driver - e.g. wideLines is unavailable on Metal/MoltenVK).
	if (vsg::ref_ptr<vsg::PhysicalDevice> physicalDevice = window->getOrCreatePhysicalDevice())
	{
		const VkPhysicalDeviceProperties& properties = physicalDevice->getProperties();
		const VkPhysicalDeviceFeatures&   features   = physicalDevice->getFeatures();
		const VkPhysicalDeviceLimits&     limits     = properties.limits;

		m_renderCapabilities.deviceName = QString::fromUtf8(properties.deviceName);

		m_renderCapabilities.pointSizeSupported = limits.pointSizeRange[1] > limits.pointSizeRange[0] + 1e-6f;
		m_renderCapabilities.maxPointSize       = limits.pointSizeRange[1];

		m_renderCapabilities.wideLinesSupported = (features.wideLines == VK_TRUE)
		                                          && (limits.lineWidthRange[1] > limits.lineWidthRange[0] + 1e-6f);
		m_renderCapabilities.maxLineWidth = limits.lineWidthRange[1];

		m_renderCapabilities.integerPickingSupported = true;
		m_renderCapabilities.msaaSupported           = true;
	}

	// Root of the VSG scene graph. It is kept in sync with the ccHObject tree
	// by ccVSGSceneBuilder (see M3). The builder owns a *persistent* group that
	// is recreated lazily on the first update(), so we must trigger update()
	// here (even with an empty DB) to make sure the group exists before we
	// build the command graph - otherwise m_sceneRoot would stay null and
	// render nothing. Only its children are rebuilt on setSceneDB(), the group
	// object itself is reused, so the command graph stays valid afterwards.
	m_sceneBuilder.update();
	m_sceneRoot = m_sceneBuilder.sceneRoot();
	assert(m_sceneRoot);

	// ----------------------------------------------------------------------
	// 2D overlay (M5.1)
	// A second View, drawn on top of the 3D image (its pipelines have the
	// depth test disabled). It is re-parented into the post-process render
	// graph by buildCommandGraph() (M7.1), which also builds the offscreen
	// 3D target + full-screen post pass.
	// ----------------------------------------------------------------------
	m_overlayViewMatrix = ccVSGViewMatrix::create();
	m_overlayViewMatrix->matrix = vsg::dmat4(); // identity: pixel coordinates
	m_overlayProjection = vsg::Orthographic::create();
	// the viewport state is shared with the 3D camera so that both stay in sync
	m_overlayCamera = vsg::Camera::create(m_overlayProjection, m_overlayViewMatrix, m_camera->viewportState);

	// M7.1: build the offscreen 3D target + full-screen post-process pass +
	// overlay. (Re)creates m_commandGraph and compiles the viewer.
	buildCommandGraph();

	// CloudCompare camera semantics (virtual trackball, pivot point, ...)
	m_manipulator = ccVSGCameraManipulator::create(this);
	m_viewer->addEventHandler(m_manipulator);

	updateCamera();

	m_viewer->compile(ccVSGResourceHints());

	// Render continuously (a QTimer drives the frames)
	m_viewer->continuousUpdate = true;
	m_viewer->setInterval(16);

	m_initialized = true;

	return true;
}

void ccVSGWindowInterface::buildCommandGraph()
{
	if (!m_viewer || !m_window || !m_window->windowAdapter || !m_camera || !m_sceneRoot)
	{
		return;
	}

	vsg::ref_ptr<vsg::Window> window = m_window->windowAdapter;
	vsg::ref_ptr<vsg::Device> device = window->getOrCreateDevice();
	if (!device)
	{
		return;
	}

	uint32_t width  = window->extent2D().width;
	uint32_t height = window->extent2D().height;
	if (width == 0) width = 1;
	if (height == 0) height = 1;

	// ----------------------------------------------------------------------
	// Offscreen 3D target: multisampled color + depth, resolved into a
	// single-sample color image that the post pass samples (M7.1).
	// ----------------------------------------------------------------------
	const VkSampleCountFlagBits samples = CC_VSG_POST_SAMPLES;

	auto makeImage = [&](VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits imageSamples) -> vsg::ref_ptr<vsg::Image>
	{
		vsg::ref_ptr<vsg::Image> image = vsg::Image::create();
		image->flags         = 0;
		image->imageType     = VK_IMAGE_TYPE_2D;
		image->format        = format;
		image->extent        = VkExtent3D{width, height, 1};
		image->mipLevels     = 1;
		image->arrayLayers   = 1;
		image->samples       = imageSamples;
		image->tiling        = VK_IMAGE_TILING_OPTIMAL;
		image->usage         = usage;
		image->sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
		image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (image->compile(device) != VK_SUCCESS)
		{
			return {};
		}
		return image;
	};

	// color (MSAA) + depth (MSAA) + resolved color (single-sample, sampled)
	vsg::ref_ptr<vsg::Image> colorMS      = makeImage(VK_FORMAT_R8G8B8A8_UNORM,
	                                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	                                                 samples);
	vsg::ref_ptr<vsg::Image> depthMS      = makeImage(VK_FORMAT_D32_SFLOAT,
	                                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
	                                                 samples);
	vsg::ref_ptr<vsg::Image> resolveColor = makeImage(VK_FORMAT_R8G8B8A8_UNORM,
	                                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	                                                 VK_SAMPLE_COUNT_1_BIT);

	if (!colorMS || !depthMS || !resolveColor)
	{
		std::cerr << "[VSG] buildCommandGraph: failed to allocate the offscreen attachments" << std::endl;
		return;
	}

	vsg::ref_ptr<vsg::ImageView> colorMS_iv      = vsg::createImageView(device, colorMS, VK_IMAGE_ASPECT_COLOR_BIT);
	vsg::ref_ptr<vsg::ImageView> depthMS_iv      = vsg::createImageView(device, depthMS, VK_IMAGE_ASPECT_DEPTH_BIT);
	vsg::ref_ptr<vsg::ImageView> resolveColor_iv = vsg::createImageView(device, resolveColor, VK_IMAGE_ASPECT_COLOR_BIT);

	if (!colorMS_iv || !depthMS_iv || !resolveColor_iv)
	{
		std::cerr << "[VSG] buildCommandGraph: failed to create the offscreen image views" << std::endl;
		return;
	}

	// Render pass: 0 = MSAA color, 1 = resolved color (sampled), 2 = MSAA depth.
	vsg::RenderPass::Attachments attachments(3);

	attachments[0].format        = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples       = samples;
	attachments[0].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	attachments[1].format        = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[1].samples       = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp        = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	attachments[2].format        = VK_FORMAT_D32_SFLOAT;
	attachments[2].samples       = samples;
	attachments[2].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[2].storeOp       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[2].stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[2].finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	vsg::AttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	vsg::AttachmentReference resolveRef{1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	vsg::AttachmentReference depthRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	vsg::RenderPass::Subpasses subpasses(1);
	subpasses[0].pipelineBindPoint         = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachments.push_back(colorRef);
	subpasses[0].resolveAttachments.push_back(resolveRef);
	subpasses[0].depthStencilAttachments.push_back(depthRef);

	// Barriers so the post pass (external) can sample the resolved color:
	// copied from the vsgrendertotexture offscreen example.
	vsg::RenderPass::Dependencies dependencies(2);
	dependencies[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass    = 0;
	dependencies[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	dependencies[1].srcSubpass    = 0;
	dependencies[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	vsg::ref_ptr<vsg::RenderPass> renderPass = vsg::RenderPass::create(device, attachments, subpasses, dependencies);

	vsg::ref_ptr<vsg::Framebuffer> framebuffer = vsg::Framebuffer::create(renderPass,
	                                                                       vsg::ImageViews{colorMS_iv, resolveColor_iv, depthMS_iv},
	                                                                       width,
	                                                                       height,
	                                                                       1);

	// 3D view, rendered into the offscreen target.
	auto view3D = vsg::View::create(m_camera, m_sceneRoot);

	// Transparent entities: dedicated bin, sorted back-to-front (M4.5).
	while (static_cast<int32_t>(view3D->bins.size()) <= CC_VSG_TRANSPARENT_BIN)
	{
		view3D->bins.push_back(vsg::Bin::create(static_cast<int32_t>(view3D->bins.size()), vsg::Bin::NO_SORT));
	}
	view3D->bins[CC_VSG_TRANSPARENT_BIN] = vsg::Bin::create(CC_VSG_TRANSPARENT_BIN, vsg::Bin::DESCENDING);

	auto rg3D = vsg::RenderGraph::create();
	rg3D->framebuffer = framebuffer;
	rg3D->renderArea  = VkRect2D{{0, 0}, {width, height}};
	rg3D->setClearValues(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}}, VkClearDepthStencilValue{0.0f, 0});
	rg3D->addChild(view3D);

	// ----------------------------------------------------------------------
	// Post-process pass: full-screen triangle sampling the resolved color.
	// ----------------------------------------------------------------------
	auto postVertexShader   = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", s_postVertexSource);
	auto postFragmentShader = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", s_postFragmentSource);

	// Full-screen triangle: clip-space positions + UVs (UV.y flipped so the
	// offscreen image is not upside down, see the shader comment).
	vsg::ref_ptr<vsg::vec3Array> positions = vsg::vec3Array::create({
		vsg::vec3(-1.0f, -1.0f, 0.0f),
		vsg::vec3( 3.0f, -1.0f, 0.0f),
		vsg::vec3(-1.0f,  3.0f, 0.0f)});
	vsg::ref_ptr<vsg::vec2Array> uvs = vsg::vec2Array::create({
		vsg::vec2(0.0f, 1.0f),
		vsg::vec2(2.0f, 1.0f),
		vsg::vec2(0.0f, -1.0f)});

	vsg::DescriptorSetLayoutBindings descriptorBindings{
		{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
	vsg::ref_ptr<vsg::DescriptorSetLayout> descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

	vsg::PushConstantRanges pushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};
	vsg::VertexInputState::Bindings vertexBindingsDescriptions{
		VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
		VkVertexInputBindingDescription{1, sizeof(vsg::vec2), VK_VERTEX_INPUT_RATE_VERTEX}};
	vsg::VertexInputState::Attributes vertexAttributeDescriptions{
		VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
		VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32_SFLOAT, 0}};

	vsg::ref_ptr<vsg::DepthStencilState> depthStencil = vsg::DepthStencilState::create();
	depthStencil->depthTestEnable  = VK_FALSE;
	depthStencil->depthWriteEnable = VK_FALSE;

	vsg::GraphicsPipelineStates pipelineStates{
		vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
		vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		vsg::RasterizationState::create(),
		vsg::MultisampleState::create(VK_SAMPLE_COUNT_1_BIT),
		vsg::ColorBlendState::create(),
		depthStencil};

	vsg::ref_ptr<vsg::PipelineLayout> pipelineLayout =
		vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
	vsg::ref_ptr<vsg::GraphicsPipeline> graphicsPipeline =
		vsg::GraphicsPipeline::create(pipelineLayout,
		                             vsg::ShaderStages{postVertexShader, postFragmentShader},
		                             pipelineStates);
	vsg::ref_ptr<vsg::BindGraphicsPipeline> bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

	// Sampler + descriptor for the resolved color image.
	vsg::ref_ptr<vsg::Sampler> sampler = vsg::Sampler::create();
	sampler->magFilter     = VK_FILTER_LINEAR;
	sampler->minFilter     = VK_FILTER_LINEAR;
	sampler->addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler->addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler->addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler->maxLod        = 1.0f;

	vsg::ref_ptr<vsg::ImageInfo> colorImageInfo = vsg::ImageInfo::create();
	colorImageInfo->imageView    = resolveColor_iv;
	colorImageInfo->imageLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	colorImageInfo->sampler      = sampler;

	vsg::ref_ptr<vsg::DescriptorImage> texture =
		vsg::DescriptorImage::create(colorImageInfo, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	vsg::ref_ptr<vsg::DescriptorSet> descriptorSet =
		vsg::DescriptorSet::create(descriptorSetLayout, vsg::Descriptors{texture});
	vsg::ref_ptr<vsg::BindDescriptorSet> bindDescriptorSet =
		vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, descriptorSet);

	vsg::ref_ptr<vsg::Commands> commands = vsg::Commands::create();
	commands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{positions, uvs}));
	commands->addChild(vsg::Draw::create(3, 1, 0, 0));

	vsg::ref_ptr<vsg::StateGroup> postScene = vsg::StateGroup::create();
	postScene->add(bindGraphicsPipeline);
	postScene->add(bindDescriptorSet);
	postScene->addChild(commands);

	// Post camera: a clip-space pass-through (the matrices are ignored by the
	// shader); it only provides the viewport state for the window RG.
	vsg::ref_ptr<vsg::Orthographic> postProjection = vsg::Orthographic::create(-1.0, 1.0, -1.0, 1.0, 0.0, 1.0);
	vsg::ref_ptr<vsg::LookAt>       postViewMatrix = vsg::LookAt::create(vsg::dvec3(0.0, 0.0, 1.0),
	                                                                      vsg::dvec3(0.0, 0.0, 0.0),
	                                                                      vsg::dvec3(0.0, 1.0, 0.0));
	vsg::ref_ptr<vsg::Camera>       postCamera     = vsg::Camera::create(postProjection, postViewMatrix, m_camera->viewportState);

	// Window-attached post render graph: clears the swapchain, draws the
	// full-screen post pass, then the 2D overlay on top.
	vsg::ref_ptr<vsg::RenderGraph> rgPost = vsg::createRenderGraphForView(window, postCamera, postScene);

	m_overlayView = vsg::View::create(m_overlayCamera, m_overlayBuilder.overlayRoot());
	{
		vsg::ref_ptr<vsg::DepthStencilState> overlayDss = vsg::DepthStencilState::create();
		overlayDss->depthTestEnable  = VK_FALSE;
		overlayDss->depthWriteEnable = VK_FALSE;
		m_overlayView->overridePipelineStates = {overlayDss};
	}
	rgPost->addChild(m_overlayView);

	// ----------------------------------------------------------------------
	// Single command graph: offscreen 3D first, then the window post pass.
	// ----------------------------------------------------------------------
	vsg::ref_ptr<vsg::CommandGraph> commandGraph = vsg::CommandGraph::create(window);
	commandGraph->addChild(rg3D);
	commandGraph->addChild(rgPost);
	m_commandGraph = commandGraph;

	m_viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});
	m_viewer->compile(ccVSGResourceHints());

	m_postExtent = window->extent2D();
}

vsg::dmat4 ccVSGWindowInterface::viewMatrix() const
{
	return m_viewMatrix ? m_viewMatrix->matrix : vsg::dmat4();
}

vsg::dmat4 ccVSGWindowInterface::projectionMatrix() const
{
	return m_projectionMatrix ? m_projectionMatrix->matrix : vsg::dmat4();
}

void ccVSGWindowInterface::rotateBaseViewMat(const ccGLMatrixd& rotMat)
{
	m_viewportParams.viewMat = rotMat * m_viewportParams.viewMat;

	updateCamera();
	redraw();
}

void ccVSGWindowInterface::moveCamera(const CCVector3d& v)
{
	// current X, Y and Z viewing directions correspond to the 'model view' matrix rows
	CCVector3d u = v;
	if (!m_viewportParams.objectCenteredView)
	{
		m_viewportParams.viewMat.transposed().applyRotation(u);
	}

	setCameraPos(m_viewportParams.getCameraCenter() + u);
}

void ccVSGWindowInterface::setCameraPos(const CCVector3d& P)
{
	m_viewportParams.setCameraCenter(P, true);

	updateCamera();
	redraw();
}

void ccVSGWindowInterface::setFocalDistance(double focalDistance)
{
	m_viewportParams.setFocalDistance(focalDistance);

	updateCamera();
	redraw();
}

void ccVSGWindowInterface::setPivotPoint(const CCVector3d& P,
                                         bool              autoUpdateCameraPos /*=false*/,
                                         bool              verbose /*=false*/)
{
	if (autoUpdateCameraPos && m_viewportParams.objectCenteredView)
	{
		// compute the equivalent camera center (same as the OpenGL backend:
		// the point of view must not change when the pivot moves)
		CCVector3d pivotShift           = m_viewportParams.getPivotPoint() - P;
		CCVector3d pivotShiftInCameraCS = pivotShift;
		m_viewportParams.viewMat.applyRotation(pivotShiftInCameraCS);
		CCVector3d newCameraPos = m_viewportParams.getCameraCenter() + pivotShiftInCameraCS - pivotShift;

		if (!m_viewportParams.perspectiveView)
		{
			// in orthographic mode, make sure the level of 'zoom' is maintained by repositioning the camera
			newCameraPos.z = m_viewportParams.getFocalDistance() + P.z;
		}

		setCameraPos(newCameraPos); // will also update the pixel size
	}

	m_viewportParams.setPivotPoint(P, true);

	if (m_signalEmitter)
	{
		Q_EMIT m_signalEmitter->pivotPointChanged(P);
	}

	if (verbose)
	{
		// the VSG backend has no on screen message queue yet (M5): the console
		// is used instead, as the OpenGL backend does with its status bar
		ccLog::Print(QString("Point (%1 ; %2 ; %3) set as rotation center")
		                 .arg(P.x, 0, 'f', 6)
		                 .arg(P.y, 0, 'f', 6)
		                 .arg(P.z, 0, 'f', 6));
	}

	invalidateViewport();
	deprecate3DLayer();
	redraw(true, false);
}

void ccVSGWindowInterface::updateCamera()
{
	if (!m_camera || !m_viewMatrix || !m_projectionMatrix)
	{
		return;
	}

	// ----------------------------------------------------------------------
	// Point size (part of the viewport parameters)
	// ----------------------------------------------------------------------
	m_sceneBuilder.pointCloudBuilder().setPointSize(m_viewportParams.defaultPointSize);

	// ----------------------------------------------------------------------
	// View matrix: strictly identical to the OpenGL backend
	// ----------------------------------------------------------------------
	const ccGLMatrixd viewMatd = m_viewportParams.computeViewMatrix();
	m_viewMatrix->matrix       = toVSGMatrix(viewMatd);

	// ----------------------------------------------------------------------
	// Projection matrix
	// Replicates ccGLWindowInterface::computeProjectionMatrix() but builds the
	// matrix with the vsg:: helpers, which produce **Vulkan reverse depth**
	// matrices (near -> NDC z = 1, far -> NDC z = 0) and invert the Y axis.
	// ----------------------------------------------------------------------
	const QSize screenSize = getScreenSize();
	const int   width      = std::max(screenSize.width(), 1);
	const int   height     = std::max(screenSize.height(), 1);

	// The point sprites are expanded in screen space: their (shared) quad
	// corners are stored as NDC offsets, so they depend on the viewport size.
	m_sceneBuilder.pointCloudBuilder().setViewportSize(width, height);

	// bounding box of the visible objects
	ccBBox visibleObjectsBBox;
	if (m_globalDBRoot)
	{
		visibleObjectsBBox = m_globalDBRoot->getBB_recursive(false, true);
	}
	if (m_winDBRoot)
	{
		const ccBBox ownBox = m_winDBRoot->getBB_recursive(false, true);
		visibleObjectsBBox += ownBox;
	}
	if (!visibleObjectsBBox.isValid())
	{
		// default box, as ccGLWindowInterface::computeProjectionMatrix() does
		constexpr PointCoordinateType halfSize = static_cast<PointCoordinateType>(0.5);
		visibleObjectsBBox                     = ccBBox(CCVector3(-halfSize, -halfSize, -halfSize),
                                                       CCVector3(halfSize, halfSize, halfSize),
                                                       true);
	}

	const double bbHalfDiag = visibleObjectsBBox.getDiagNormd() / 2.0;

	// min and max distances in camera space (apply the view matrix to the 8 corners)
	double zMin = std::numeric_limits<double>::quiet_NaN();
	double zMax = zMin;
	{
		const CCVector3&         bbMin = visibleObjectsBBox.minCorner();
		const CCVector3&         bbMax = visibleObjectsBBox.maxCorner();
		std::array<CCVector3, 8> bbCorners{
		    bbMin,
		    CCVector3(bbMin.x, bbMin.y, bbMax.z),
		    CCVector3(bbMin.x, bbMax.y, bbMin.z),
		    CCVector3(bbMax.x, bbMin.y, bbMin.z),
		    bbMax,
		    CCVector3(bbMax.x, bbMax.y, bbMin.z),
		    CCVector3(bbMax.x, bbMin.y, bbMax.z),
		    CCVector3(bbMin.x, bbMax.y, bbMax.z)};

		for (const CCVector3& P : bbCorners)
		{
			const CCVector3d Pd = viewMatd * CCVector3d::fromArray(P.u);
			const double     z  = -Pd.z; // the camera looks toward -Z
			if (std::isnan(zMin) || z < zMin)
			{
				zMin = z;
			}
			if (std::isnan(zMax) || z > zMax)
			{
				zMax = z;
			}
		}
	}

	double       zNear                   = zMin;
	double       zFar                    = zMax;
	const double epsilon                 = std::max(bbHalfDiag / 1000.0, 1.0e-6);
	const double ar                      = static_cast<double>(height) / static_cast<double>(width);
	const double distanceToHalfWidthRatio = m_viewportParams.computeDistanceToHalfWidthRatio();

	if (m_viewportParams.perspectiveView)
	{
		const double minZFar = static_cast<double>(CCCoreLib::ZERO_TOLERANCE_F) / m_viewportParams.zNearCoef;
		if (zFar < minZFar)
		{
			// no object in front of the camera! (or too small)
			zFar  = minZFar;
			zNear = epsilon;
		}
		else
		{
			zNear = std::max(zMin, zFar * m_viewportParams.zNearCoef);
		}

		const double delta = zFar / 2.0;
		if (zFar - zNear < delta)
		{
			// zNear can't be too close to zFar
			zNear -= delta / 2.0;
			zFar += delta / 2.0;
		}

		const double xMax = zNear * distanceToHalfWidthRatio;
		const double yMax = xMax * ar;

		m_projectionMatrix->matrix = vsg::perspective(-xMax, xMax, -yMax, yMax, zNear, zFar);
	}
	else
	{
		zFar += epsilon;
		zNear -= epsilon;

		const double xMax = std::abs(m_viewportParams.getFocalDistance()) * distanceToHalfWidthRatio;
		const double yMax = xMax * ar;

		m_projectionMatrix->matrix = vsg::orthographic(-xMax, xMax, -yMax, yMax, zNear, zFar);
	}

	m_viewportParams.zNear = zNear;
	m_viewportParams.zFar  = zFar;

	// ----------------------------------------------------------------------
	// 2D overlay (M5)
	// Orthographic projection centred on the viewport, in pixels - the very
	// same one the OpenGL backend uses for its foreground entities
	// (ccGLWindowInterface::setStandardOrthoCenter()).
	// ----------------------------------------------------------------------
	if (m_overlayProjection)
	{
		const double halfW = static_cast<double>(width) * 0.5;
		const double halfH = static_cast<double>(height) * 0.5;
		const double maxS  = std::max(halfW, halfH);

		m_overlayProjection->left         = -halfW;
		m_overlayProjection->right        = halfW;
		m_overlayProjection->bottom       = -halfH;
		m_overlayProjection->top          = halfH;
		m_overlayProjection->nearDistance = -maxS;
		m_overlayProjection->farDistance  = maxS;
	}

	// the trihedron follows the camera orientation and the viewport size
	// TODO(M5.4): wire the real 'showTrihedron' display parameter
	{
		// only the *rotation* of the view matrix is used: the trihedron
		// geometry is expressed in pixels, whereas the view translation is in
		// world units (which can be huge, e.g. for a mesh in millimetres)
		ccGLMatrixd rotationOnly = viewMatd;
		rotationOnly.data()[12] = 0.0;
		rotationOnly.data()[13] = 0.0;
		rotationOnly.data()[14] = 0.0;

		m_overlayBuilder.update(width, height, toVSGMatrix(rotationOnly), true);
	}

	// the scalar field color scale (M5.5)
	ccScalarField* sf = findDisplayedScalarField(m_winDBRoot);
	if (!sf)
	{
		sf = findDisplayedScalarField(m_globalDBRoot);
	}

	m_overlayBuilder.setDevicePixelRatio(devicePixelRatio());

	if (m_overlayBuilder.updateColorScale(sf, width, height, 1.0f))
	{
		m_overlayNeedsCompile = true;
	}

	// the scale bar (M5.4): only meaningful in orthographic mode
	const double pixelSize = m_viewportParams.computePixelSize(width, height);

	if (m_overlayBuilder.updateScaleBar(!m_viewportParams.perspectiveView, pixelSize, width, height))
	{
		m_overlayNeedsCompile = true;
	}

	// the 2D image overlay (M5.6): all the visible images
	std::vector<const ccImage*> images;
	collectImages(m_winDBRoot, images);
	collectImages(m_globalDBRoot, images);

	if (m_overlayBuilder.updateImages(images, width, height))
	{
		m_overlayNeedsCompile = true;
	}

	// the 2D labels (M5.3): cc2DLabel anchors + cc2DViewportLabel ROIs
	{
		ccHObject* labelRoot = m_winDBRoot ? m_winDBRoot : m_globalDBRoot;

		if (m_overlayBuilder.updateLabels(labelRoot,
		                                  m_viewMatrix->matrix,
		                                  m_projectionMatrix->matrix,
		                                  m_viewportParams,
		                                  width,
		                                  height))
		{
			m_overlayNeedsCompile = true;
		}
	}

	// the on-screen messages (M5.4): whatever displayNewMessage() recorded
	{
		purgeExpiredMessages();

		std::vector<ccVSGOverlayBuilder::Message> overlayMessages;
		overlayMessages.reserve(m_messagesToDisplay.size());

		for (const MessageToDisplay& mess : m_messagesToDisplay)
		{
			overlayMessages.push_back({mess.message, static_cast<int>(mess.position)});
		}

		if (m_overlayBuilder.updateMessages(overlayMessages, width, height))
		{
			m_overlayNeedsCompile = true;
		}
	}
}

void ccVSGWindowInterface::setSceneDB(ccHObject* root)
{
	if (m_globalDBRoot == root)
	{
		return;
	}

	m_globalDBRoot = root;

	// synchronize the ccHObject tree with the VSG scene graph
	m_sceneBuilder.setRoot(root);

	// the new nodes have to be compiled before they can be rendered
	if (m_sceneBuilder.update() && m_viewer)
	{
		m_viewer->compile(ccVSGResourceHints());
	}

	// keep the member in sync with the (persistent) builder root group
	m_sceneRoot = m_sceneBuilder.sceneRoot();

	// mirrors ccGLWindowInterface::setSceneDB(): adapt the zoom (and hence the
	// near/far planes) to the new scene contents
	zoomGlobal();

	// in case the zoom could not be applied (empty DB)
	updateCamera();
	redraw();
}

void ccVSGWindowInterface::setViewportParameters(const ccViewportParameters& params)
{
	m_viewportParams = params;

	updateCamera();

	redraw();
}

void ccVSGWindowInterface::setPickingMode(PICKING_MODE mode, Qt::CursorShape defaultCursorShape)
{
	Q_UNUSED(defaultCursorShape);

	if (m_pickingModeLocked)
	{
		return;
	}

	// same convention as the OpenGL backend: DEFAULT_PICKING *is* the entity
	// picking mode (see ccGLWindowInterface::setPickingMode)
	if (mode == DEFAULT_PICKING)
	{
		mode = ENTITY_PICKING;
	}

	m_pickingMode = mode;
}

void ccVSGWindowInterface::setInteractionMode(INTERACTION_FLAGS flags)
{
	m_interactionFlags = flags;
}

void ccVSGWindowInterface::setUnclosable(bool state)
{
	m_unclosable = state;
}

void ccVSGWindowInterface::addToOwnDB(ccHObject* obj, bool noDependency /*=true*/)
{
	if (!obj)
	{
		assert(false);
		return;
	}

	if (m_winDBRoot)
	{
		m_winDBRoot->addChild(obj, noDependency ? ccHObject::DP_NONE : ccHObject::DP_PARENT_OF_OTHER);
		obj->setDisplay(this);
	}
	else
	{
		ccLog::Error("[ccVSGWindowInterface::addToOwnDB] Window has no DB!");
	}
}

void ccVSGWindowInterface::removeFromOwnDB(ccHObject* obj)
{
	if (m_winDBRoot)
	{
		m_winDBRoot->removeChild(obj);
	}
}

double ccVSGWindowInterface::elapsedSeconds() const
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count();
}

void ccVSGWindowInterface::refreshOverlay()
{
	// deferred: updateCamera() must not be run from a render or event callback
	scheduleDeferredAction([this]() { updateCamera(); });
}

bool ccVSGWindowInterface::purgeExpiredMessages()
{
	const double now = elapsedSeconds();

	bool changed = false;
	for (auto it = m_messagesToDisplay.begin(); it != m_messagesToDisplay.end();)
	{
		if (it->messageValidity_sec <= now)
		{
			it = m_messagesToDisplay.erase(it);
			changed = true;
		}
		else
		{
			++it;
		}
	}

	return changed;
}

void ccVSGWindowInterface::displayNewMessage(const QString&  message,
                                            MessagePosition pos,
                                            bool            append /*=false*/,
                                            int             displayMaxDelay_sec /*=2*/,
                                            MessageType     type /*=CUSTOM_MESSAGE*/)
{
	// mirrors ccGLWindowInterface::displayNewMessage(): the message is only
	// stored here, it is drawn by the 2D overlay (M5.4)
	if (message.isEmpty())
	{
		if (!append)
		{
			// an empty message removes the ones displayed at the same position
			for (auto it = m_messagesToDisplay.begin(); it != m_messagesToDisplay.end();)
			{
				if (it->position == pos)
				{
					it = m_messagesToDisplay.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
		else
		{
			ccLog::Warning("[VSG][displayNewMessage] Appending an empty message has no effect!");
		}
		return;
	}

	if (!append)
	{
		// a non custom message replaces the previous one of the same type
		if (type != CUSTOM_MESSAGE)
		{
			for (auto it = m_messagesToDisplay.begin(); it != m_messagesToDisplay.end();)
			{
				if (it->type == type)
				{
					it = m_messagesToDisplay.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
	}
	else if (pos == SCREEN_CENTER_MESSAGE)
	{
		ccLog::Warning("[VSG][displayNewMessage] Append is not supported for center screen messages!");
	}

	MessageToDisplay mess;
	mess.message             = message;
	mess.messageValidity_sec = elapsedSeconds() + displayMaxDelay_sec;
	mess.position            = pos;
	mess.type                = type;
	m_messagesToDisplay.push_back(mess);

	// the overlay is only refreshed by updateCamera() (i.e. when the camera or
	// the viewport changes): a message has to trigger its own refresh, both to
	// appear now and to disappear once its delay has expired
	refreshOverlay();

	if (displayMaxDelay_sec > 0)
	{
		scheduleDeferredAction([this]() { refreshOverlay(); }, displayMaxDelay_sec * 1000);
	}
}

void ccVSGWindowInterface::aboutToBeRemoved(ccDrawableObject* obj)
{
	Q_UNUSED(obj);
	// TODO(M3): drop the corresponding VSG nodes when an entity is removed
}

void ccVSGWindowInterface::zoomGlobal()
{
	// mirrors ccGLWindowInterface::updateConstellationCenterAndZoom()

	// bounding box of the visible objects
	ccBBox zoomedBox;
	if (m_globalDBRoot)
	{
		zoomedBox = m_globalDBRoot->getBB_recursive(false, true);
	}
	if (m_winDBRoot)
	{
		zoomedBox += m_winDBRoot->getBB_recursive(false, true);
	}
	if (!zoomedBox.isValid())
	{
		return;
	}

	double bbDiag = zoomedBox.getDiagNorm();
	if (CCCoreLib::LessThanEpsilon(bbDiag))
	{
		ccLog::Warning("[ccVSGWindow] Entity/DB has a null bounding-box!");
		bbDiag = 1.0;
	}

	// the pivot point is set on the box center
	const CCVector3d P = zoomedBox.getCenter();
	m_viewportParams.setPivotPoint(P, false);

	// distance required for the camera to see the whole bounding box
	const QSize screenSize = getScreenSize();
	const int   width      = std::max(screenSize.width(), 1);
	const int   height     = std::max(screenSize.height(), 1);
	const double focalDistance = bbDiag / m_viewportParams.computeDistanceToWidthRatio(width, height);

	setCameraPos(P);

	CCVector3d v(0, 0, focalDistance);
	moveCamera(v);

	// just in case
	updateCamera();
	redraw();
}

void ccVSGWindowInterface::getGLCameraParameters(ccGLCameraParameters& params) const
{
	const QSize screenSize = getScreenSize();
	const int   width      = std::max(screenSize.width(), 1);
	const int   height     = std::max(screenSize.height(), 1);

	// The view matrix is strictly the same as the OpenGL one: it is derived
	// from ccViewportParameters, which is the single source of truth.
	params.modelViewMat = m_viewportParams.computeViewMatrix();

	// The projection matrix, on the other hand, has to be converted back from
	// the Vulkan convention (reverse depth + inverted Y axis).
	params.projectionMat = fromVSGMatrix(vulkanToGLProjection(projectionMatrix()));

	params.viewport[0] = 0;
	params.viewport[1] = 0;
	params.viewport[2] = width;
	params.viewport[3] = height;

	params.pixelSize = m_viewportParams.computePixelSize(width, height);

	params.perspective       = m_viewportParams.perspectiveView;
	params.fov_deg           = m_viewportParams.fov_deg;
	params.nearClippingDepth = m_viewportParams.nearClippingDepth;
	params.farClippingDepth  = m_viewportParams.farClippingDepth;
}

bool ccVSGWindowInterface::renderOffscreen(const OffscreenRequest& request, std::vector<uint8_t>& pixels)
{
	pixels.clear();

	if (!m_viewer || !m_window || !m_window->windowAdapter || !m_camera || !request.scene)
	{
		ccLog::Warning("[VSG] renderOffscreen: the viewer is not initialized");
		return false;
	}

	vsg::ref_ptr<vsg::Window> window = m_window->windowAdapter;
	vsg::ref_ptr<vsg::Device> device = window->getOrCreateDevice();
	if (!device)
	{
		ccLog::Warning("[VSG] renderOffscreen: no Vulkan device");
		return false;
	}

	const uint32_t width  = std::max<uint32_t>(1u, request.width);
	const uint32_t height = std::max<uint32_t>(1u, request.height);

	// size of one pixel of the attachment that is read back
	std::size_t elementSize = 4; // R8G8B8A8_UNORM, R32_UINT and D32_SFLOAT
	if (request.readDepth)
	{
		if (request.depthFormat == VK_FORMAT_D32_SFLOAT)
		{
			elementSize = 4;
		}
		else if (request.depthFormat == VK_FORMAT_D16_UNORM)
		{
			elementSize = 2;
		}
		else
		{
			// a packed depth/stencil format (D24_UNORM_S8_UINT for instance)
			// interleaves its components: decoding it is not worth the risk
			ccLog::Warning("[VSG] renderOffscreen: the depth format cannot be read back (only D32_SFLOAT and D16_UNORM are supported)");
			return false;
		}
	}

	// ----------------------------------------------------------------------
	// offscreen attachments
	// ----------------------------------------------------------------------
	auto makeAttachment = [&device, width, height](VkFormat format, VkImageUsageFlags usage) -> vsg::ref_ptr<vsg::Image>
	{
		vsg::ref_ptr<vsg::Image> image = vsg::Image::create();
		image->imageType     = VK_IMAGE_TYPE_2D;
		image->format        = format;
		image->extent        = VkExtent3D{width, height, 1};
		image->mipLevels     = 1;
		image->arrayLayers   = 1;
		image->samples       = VK_SAMPLE_COUNT_1_BIT;
		image->tiling        = VK_IMAGE_TILING_OPTIMAL;
		image->usage         = usage;
		image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (image->compile(device) != VK_SUCCESS)
		{
			return {};
		}

		return image;
	};

	const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
	                                     | (request.readDepth ? VkImageUsageFlags{0} : VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
	const VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
	                                     | (request.readDepth ? VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT} : VkImageUsageFlags{0});

	vsg::ref_ptr<vsg::Image> colorImage = makeAttachment(request.colorFormat, colorUsage);
	vsg::ref_ptr<vsg::Image> depthImage = makeAttachment(request.depthFormat, depthUsage);

	if (!colorImage || !depthImage)
	{
		ccLog::Warning("[VSG] renderOffscreen: failed to allocate the offscreen attachments");
		return false;
	}

	vsg::ref_ptr<vsg::ImageView> colorImageView = vsg::createImageView(device, colorImage, VK_IMAGE_ASPECT_COLOR_BIT);
	vsg::ref_ptr<vsg::ImageView> depthImageView = vsg::createImageView(device, depthImage, vsg::computeAspectFlagsForFormat(request.depthFormat));

	// ----------------------------------------------------------------------
	// render pass / framebuffer / render graph
	// ----------------------------------------------------------------------
	// 'requiresDepthRead' is what makes the depth attachment actually stored
	// (vsg::defaultDepthAttachment() uses STORE_OP_DONT_CARE otherwise)
	vsg::ref_ptr<vsg::RenderPass>  renderPass  = vsg::createRenderPass(device, request.colorFormat, request.depthFormat, request.readDepth);
	vsg::ref_ptr<vsg::Framebuffer> framebuffer = vsg::Framebuffer::create(renderPass,
	                                                                     vsg::ImageViews{colorImageView, depthImageView},
	                                                                     width,
	                                                                     height,
	                                                                     1);

	// the very same camera, but with the offscreen viewport
	vsg::ref_ptr<vsg::ViewportState> viewportState = vsg::ViewportState::create(0, 0, width, height);
	vsg::ref_ptr<vsg::Camera>        camera        = vsg::Camera::create(m_projectionMatrix, m_viewMatrix, viewportState);
	vsg::ref_ptr<vsg::View>          view          = vsg::View::create(camera, request.scene);

	vsg::ref_ptr<vsg::RenderGraph> renderGraph = vsg::RenderGraph::create();
	renderGraph->framebuffer = framebuffer;
	renderGraph->renderArea  = VkRect2D{{0, 0}, {width, height}};
	renderGraph->setClearValues(request.clearColor.color, VkClearDepthStencilValue{request.clearDepth, 0});
	renderGraph->addChild(view);

	// the 2D overlay is rendered on top of the 3D image, within the same render
	// pass (M5). The orthographic projection is recomputed as the offscreen
	// size may differ from the window one (zoom factor).
	if (request.withOverlay && m_overlayProjection && m_overlayViewMatrix)
	{
		// TODO(M5): the overlay layout (and its orthographic projection) is
		// left in its offscreen state: it is rebuilt on the next on screen
		// frame, but a 1:1 renderToImage() should restore it.
		const double halfW = static_cast<double>(width) * 0.5;
		const double halfH = static_cast<double>(height) * 0.5;
		const double maxS  = std::max(halfW, halfH);

		m_overlayProjection->left         = -halfW;
		m_overlayProjection->right        = halfW;
		m_overlayProjection->bottom       = -halfH;
		m_overlayProjection->top          = halfH;
		m_overlayProjection->nearDistance = -maxS;
		m_overlayProjection->farDistance  = maxS;

		m_overlayBuilder.update(static_cast<int>(width),
		                        static_cast<int>(height),
		                        m_viewMatrix->matrix,
		                        true);

		vsg::ref_ptr<vsg::Camera> overlayCamera = vsg::Camera::create(m_overlayProjection,
		                                                              m_overlayViewMatrix,
		                                                              viewportState);
		renderGraph->addChild(vsg::View::create(overlayCamera, m_overlayBuilder.overlayRoot()));
	}

	// ----------------------------------------------------------------------
	// copy the result back to a CPU visible buffer
	// ----------------------------------------------------------------------
	const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width) * height * elementSize;

	vsg::ref_ptr<vsg::Buffer> buffer = vsg::createBufferAndMemory(device,
	                                                             bufferSize,
	                                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                                             VK_SHARING_MODE_EXCLUSIVE,
	                                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	// the render pass leaves the depth attachment in
	// DEPTH_STENCIL_ATTACHMENT_OPTIMAL: a barrier is required before using it
	// as the source of a transfer
	vsg::ref_ptr<vsg::PipelineBarrier> barrier;
	if (request.readDepth)
	{
		vsg::ref_ptr<vsg::ImageMemoryBarrier> imageBarrier = vsg::ImageMemoryBarrier::create();
		imageBarrier->srcAccessMask    = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		imageBarrier->dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;
		imageBarrier->oldLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		imageBarrier->newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		imageBarrier->image            = depthImage;
		imageBarrier->subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

		barrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
		                                      0,
		                                      imageBarrier);
	}

	vsg::ref_ptr<vsg::CopyImageToBuffer> copyImage = vsg::CopyImageToBuffer::create();
	copyImage->srcImage       = request.readDepth ? depthImage : colorImage;
	copyImage->srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	copyImage->dstBuffer      = buffer;

	VkBufferImageCopy region{};
	region.bufferOffset                    = 0;
	region.bufferRowLength                 = width;
	region.bufferImageHeight               = height;
	region.imageSubresource.aspectMask     = request.readDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel       = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount     = 1;
	region.imageOffset                     = VkOffset3D{0, 0, 0};
	region.imageExtent                     = VkExtent3D{width, height, 1};
	copyImage->regions                     = {region};

	// ----------------------------------------------------------------------
	// render (temporarily replacing the on screen command graph)
	// ----------------------------------------------------------------------
	vsg::ref_ptr<vsg::CommandGraph> offscreenGraph = vsg::CommandGraph::create(window);
	offscreenGraph->addChild(renderGraph);
	if (barrier)
	{
		offscreenGraph->addChild(barrier);
	}
	offscreenGraph->addChild(copyImage);

	m_viewer->assignRecordAndSubmitTaskAndPresentation({offscreenGraph});
	m_viewer->compile(ccVSGResourceHints());

	bool ok = false;

	// same order as vsgQt::Viewer::render(). advanceToNextFrame() is what
	// advances the tasks (and therefore assigns their fences): submitting
	// without it crashes in RecordAndSubmitTask::start(), and it returns
	// false when the frame could not be acquired (window not visible, lost
	// device, ...) - in which case there is nothing to submit at all.
	if (m_viewer->advanceToNextFrame())
	{
		m_viewer->update();
		m_viewer->recordAndSubmit();
		m_viewer->deviceWaitIdle();

		// read the pixels back
		if (vsg::DeviceMemory* memory = buffer->getDeviceMemory(0))
		{
			void* data = nullptr;
			if (memory->map(buffer->getMemoryOffset(0), bufferSize, 0, &data) == VK_SUCCESS && data)
			{
				pixels.resize(static_cast<std::size_t>(bufferSize));
				std::memcpy(pixels.data(), data, static_cast<std::size_t>(bufferSize));
				ok = true;

				memory->unmap();
			}
			else
			{
				ccLog::Warning("[VSG] renderOffscreen: failed to map the output buffer");
			}
		}
	}
	else
	{
		ccLog::Warning("[VSG] renderOffscreen: could not advance to the next frame");
	}

	// restore the on screen rendering (whatever happened above)
	if (m_commandGraph)
	{
		m_viewer->assignRecordAndSubmitTaskAndPresentation({m_commandGraph});
		m_viewer->compile(ccVSGResourceHints());
	}

	return ok;
}

QImage ccVSGWindowInterface::renderToImage(float zoomFactor /*=1.0f*/,
                                          bool  /*dontScaleFeatures*/ /*=false*/,
                                          bool  /*renderOverlayItems*/ /*=false*/,
                                          bool  /*silent*/ /*=false*/)
{
	if (!m_viewer || !m_window || !m_window->windowAdapter || !m_camera || !m_sceneRoot)
	{
		ccLog::Warning("[VSG] renderToImage: the viewer is not initialized");
		return QImage();
	}

	vsg::ref_ptr<vsg::Window> window = m_window->windowAdapter;

	const QSize screenSize = getScreenSize();
	const uint32_t width   = static_cast<uint32_t>(std::max(1, static_cast<int>(std::lround(screenSize.width() * zoomFactor))));
	const uint32_t height  = static_cast<uint32_t>(std::max(1, static_cast<int>(std::lround(screenSize.height() * zoomFactor))));

	OffscreenRequest request;
	request.scene           = m_sceneRoot;
	request.colorFormat     = VK_FORMAT_R8G8B8A8_UNORM;
	request.depthFormat     = window->depthFormat();
	request.clearColor.color = VkClearColorValue{{0.15f, 0.15f, 0.20f, 1.0f}}; // TODO(M6): use the CloudCompare background color (ccGui::Parameters)
	request.width           = width;
	request.height          = height;
	request.withOverlay     = true;

	std::vector<uint8_t> pixels;
	if (!renderOffscreen(request, pixels))
	{
		return QImage();
	}

	QImage result(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGBA8888);
	for (uint32_t y = 0; y < height; ++y)
	{
		std::memcpy(result.scanLine(static_cast<int>(y)),
		            pixels.data() + static_cast<std::size_t>(y) * width * 4,
		            static_cast<std::size_t>(width) * 4);
	}

	return result;
}

void ccVSGWindowInterface::scheduleDeferredAction(std::function<void()> action, int delay_ms /*=0*/)
{
	// the plain interface has no Qt context (see ccVSGWindow, which overrides
	// this with a QTimer::singleShot): the action is simply run right away
	if (action)
	{
		action();
	}
}

void ccVSGWindowInterface::requestPicking(int x, int y)
{
	scheduleDeferredAction([this, x, y]()
	{
		doPicking(x, y);
	});
}

void ccVSGWindowInterface::requestMouseDoubleClick(int x, int y)
{
	scheduleDeferredAction([this, x, y]()
	{
		processMouseDoubleClick(x, y);
	});
}

void ccVSGWindowInterface::unlinkEntitiesFromDisplay()
{
	// as ccGLWindowInterface does: the entities only keep a raw pointer to
	// their display, so they have to be unlinked before this object is
	// destroyed (see scheduleDeferredAction() for the reentrancy warning and
	// ccDrawableObject::prepareDisplayForRefresh() for the symptom: a virtual
	// call on a half destroyed display is a __cxa_pure_virtual abort)
	if (m_globalDBRoot)
	{
		m_globalDBRoot->removeFromDisplay_recursive(this);
	}
	if (m_winDBRoot)
	{
		m_winDBRoot->removeFromDisplay_recursive(this);
	}
}

void ccVSGWindowInterface::doPicking(int x, int y)
{
	if (m_pickingMode == NO_PICKING || !m_signalEmitter)
	{
		return;
	}

	// shift+click = spawn a label on the clicked point or triangle (M6.5), as
	// the OpenGL backend does in its mouse release event
	const bool shiftPressed = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;

	// M6.1 / M6.2: the entity based modes are answered by an offscreen pass
	// that renders the entity IDs (see renderIdPass()). The point / triangle
	// modes keep using the historical CPU routines below.
	if (!shiftPressed
	    && (m_pickingMode == ENTITY_PICKING
	        || m_pickingMode == ENTITY_RECT_PICKING
	        || m_pickingMode == FAST_PICKING))
	{
		doEntityPicking(x, y);
		return;
	}

	const QSize screenSize = getScreenSize();
	const int   height     = screenSize.height();
	if (height <= 0)
	{
		return;
	}

	// CloudCompare works in a Y-up screen space (see the OpenGL backend)
	const CCVector2d clickedPos(static_cast<double>(x), height - 1 - static_cast<double>(y));

	ccGLCameraParameters camera;
	getGLCameraParameters(camera);

	ccHObject* nearestEntity            = nullptr;
	int        nearestElementIndex      = -1;
	double     nearestElementSquareDist = -1.0;
	CCVector3  nearestPoint(0, 0, 0);
	CCVector3d nearestPointBC(0, 0, 0);

	// size of the picked area (in pixels) - same default as the OpenGL backend
	constexpr double pickWidth  = 5.0;
	constexpr double pickHeight = 5.0;

	std::vector<ccHObject*> toProcess;
	if (m_globalDBRoot)
	{
		toProcess.push_back(m_globalDBRoot);
	}
	if (m_winDBRoot)
	{
		toProcess.push_back(m_winDBRoot);
	}

	try
	{
		while (!toProcess.empty())
		{
			ccHObject* ent = toProcess.back();
			toProcess.pop_back();

			if (!ent->isEnabled())
			{
				continue;
			}

			bool ignoreSubmeshes = false;

			// we only consider the entities displayed in this very view
			if (ent->isDisplayedIn(this))
			{
				if (ent->isKindOf(CC_TYPES::POINT_CLOUD))
				{
					auto* cloud = static_cast<ccGenericPointCloud*>(ent);

					int    nearestPointIndex = -1;
					double nearestSquareDist = 0.0;

					// TODO(M6): honor ccGui::Parameters().autoComputeOctree
					// (the OpenGL backend may ask the user and build one)
					if (cloud->pointPicking(clickedPos, camera, nearestPointIndex, nearestSquareDist, pickWidth, pickHeight))
					{
						if (nearestElementIndex < 0 || (nearestPointIndex >= 0 && nearestSquareDist < nearestElementSquareDist))
						{
							nearestElementSquareDist = nearestSquareDist;
							nearestElementIndex      = nearestPointIndex;
							nearestPoint             = *(cloud->getPoint(nearestPointIndex));
							nearestEntity            = cloud;
							nearestPointBC           = CCVector3d(0, 0, 0);
						}
					}
				}
				else if (ent->isKindOf(CC_TYPES::MESH)
				         && !ent->isA(CC_TYPES::MESH_GROUP)       // their children are processed
				         && !ent->isA(CC_TYPES::COORDINATESYSTEM) // ignored by the OpenGL backend too
				)
				{
					ignoreSubmeshes = true;

					auto* mesh = static_cast<ccGenericMesh*>(ent);

					int        nearestTriIndex   = -1;
					double     nearestSquareDist = 0.0;
					CCVector3d P;
					CCVector3d barycentricCoords;

					if (mesh->trianglePicking(clickedPos,
					                          camera,
					                          mesh->isShownAsWire(), // only the edges in this case
					                          nearestTriIndex,
					                          nearestSquareDist,
					                          P,
					                          &barycentricCoords))
					{
						if (nearestElementIndex < 0 || (nearestTriIndex >= 0 && nearestSquareDist < nearestElementSquareDist))
						{
							nearestElementSquareDist = nearestSquareDist;
							nearestElementIndex      = nearestTriIndex;
							nearestPoint             = P.toPC();
							nearestEntity            = mesh;
							nearestPointBC           = barycentricCoords;
						}
					}
				}
			}

			// process the children
			for (unsigned i = 0; i < ent->getChildrenNumber(); ++i)
			{
				ccHObject* child = ent->getChild(i);

				// we ignore the sub-meshes of the current (mesh) entity as
				// their content is the same!
				if (ignoreSubmeshes && child->isKindOf(CC_TYPES::SUB_MESH))
				{
					continue;
				}

				toProcess.push_back(child);
			}
		}
	}
	catch (const std::bad_alloc&)
	{
		ccLog::Warning("[Picking][VSG] Not enough memory!");
	}

	// shift+click turns the entity picking into a label spawning (M6.5)
	const PICKING_MODE mode = (m_pickingMode == ENTITY_PICKING && shiftPressed) ? LABEL_PICKING : m_pickingMode;

	switch (mode)
	{
	case POINT_PICKING:
	case TRIANGLE_PICKING:
	case POINT_OR_TRIANGLE_PICKING:
	case POINT_OR_TRIANGLE_OR_LABEL_PICKING:
		// a signal must always be emitted, even if nothing was picked!
		Q_EMIT m_signalEmitter->itemPicked(nearestEntity,
		                                   static_cast<unsigned>(std::max(nearestElementIndex, 0)),
		                                   x,
		                                   y,
		                                   nearestPoint,
		                                   nearestPointBC);
		break;

	case LABEL_PICKING:
		// mirrors ccGLWindowInterface::processPickingResult(): the picked
		// point (or triangle) automatically spawns a label
		if (m_globalDBRoot && nearestEntity && nearestElementIndex >= 0)
		{
			cc2DLabel* label = nullptr;

			if (nearestEntity->isKindOf(CC_TYPES::POINT_CLOUD))
			{
				label = new cc2DLabel();
				label->addPickedPoint(ccHObjectCaster::ToGenericPointCloud(nearestEntity), nearestElementIndex);
				nearestEntity->addChild(label);
			}
			else if (nearestEntity->isKindOf(CC_TYPES::MESH))
			{
				label = new cc2DLabel();
				label->addPickedPoint(ccHObjectCaster::ToGenericMesh(nearestEntity),
				                      nearestElementIndex,
				                      CCVector2d(nearestPointBC.x, nearestPointBC.y));
				nearestEntity->addChild(label);
			}

			if (label)
			{
				label->setVisible(true);
				label->setDisplay(nearestEntity->getDisplay());
				label->setPosition(static_cast<float>(x + 20) / std::max(1, screenSize.width()),
				                   static_cast<float>(y + 20) / std::max(1, screenSize.height()));

				Q_EMIT m_signalEmitter->newLabel(static_cast<ccHObject*>(label));
				QCoreApplication::processEvents();

				redraw(false, false);
			}
		}
		break;

	default:
		// NOTE: the entity based modes (ENTITY_PICKING / ENTITY_RECT_PICKING /
		// FAST_PICKING) are answered by doEntityPicking() and never reach this
		// switch.
		break;
	}
}

bool ccVSGWindowInterface::renderIdPass(std::vector<uint32_t>& ids, uint32_t& width, uint32_t& height)
{
	ids.clear();
	width  = 0;
	height = 0;

	if (!m_viewer || !m_window || !m_window->windowAdapter || !m_camera || !m_sceneBuilder.idSceneRoot())
	{
		ccLog::Warning("[VSG] renderIdPass: the viewer is not initialized");
		return false;
	}

	vsg::ref_ptr<vsg::Window> window = m_window->windowAdapter;
	vsg::ref_ptr<vsg::Device> device = window->getOrCreateDevice();
	if (!device)
	{
		ccLog::Warning("[VSG] renderIdPass: no Vulkan device");
		return false;
	}

	const QSize    screenSize = getScreenSize();
	const uint32_t w          = static_cast<uint32_t>(std::max(1, screenSize.width()));
	const uint32_t h          = static_cast<uint32_t>(std::max(1, screenSize.height()));

	// The IDs are written as unsigned integers: Vulkan (and Metal) accept
	// R32_UINT as a color attachment and, unlike the RGBA "unique color" of the
	// OpenGL backend, an integer cannot be mangled by blending.
	OffscreenRequest request;
	request.scene       = m_sceneBuilder.idSceneRoot();
	request.colorFormat = VK_FORMAT_R32_UINT;
	request.depthFormat = window->depthFormat();
	request.width       = w;
	request.height      = h;

	// 0 means 'no entity' - the *uint32* member of the union has to be set,
	// otherwise the clear value would be read back as garbage
	request.clearColor.color.uint32[0] = 0;
	request.clearColor.color.uint32[1] = 0;
	request.clearColor.color.uint32[2] = 0;
	request.clearColor.color.uint32[3] = 0;

	std::vector<uint8_t> pixels;
	if (!renderOffscreen(request, pixels))
	{
		return false;
	}

	const auto* src = reinterpret_cast<const uint32_t*>(pixels.data());

	ids.assign(src, src + static_cast<std::size_t>(w) * h);
	width  = w;
	height = h;

	return true;
}

bool ccVSGWindowInterface::renderDepthPass(std::vector<float>& depths, uint32_t& width, uint32_t& height)
{
	depths.clear();
	width  = 0;
	height = 0;

	if (!m_viewer || !m_window || !m_window->windowAdapter || !m_camera || !m_sceneRoot)
	{
		ccLog::Warning("[VSG] renderDepthPass: the viewer is not initialized");
		return false;
	}

	vsg::ref_ptr<vsg::Window> window = m_window->windowAdapter;

	const VkFormat depthFormat = window->depthFormat();
	if (depthFormat != VK_FORMAT_D32_SFLOAT && depthFormat != VK_FORMAT_D16_UNORM)
	{
		ccLog::Warning("[VSG] renderDepthPass: the depth format cannot be read back (only D32_SFLOAT and D16_UNORM are supported)");
		return false;
	}

	const QSize screenSize = getScreenSize();
	const uint32_t w = static_cast<uint32_t>(std::max(1, screenSize.width()));
	const uint32_t h = static_cast<uint32_t>(std::max(1, screenSize.height()));

	OffscreenRequest request;
	request.scene       = m_sceneRoot;
	request.colorFormat = VK_FORMAT_R8G8B8A8_UNORM; // required by the render pass, but never read back
	request.depthFormat = depthFormat;
	request.width       = w;
	request.height      = h;
	request.readDepth   = true;

	std::vector<uint8_t> pixels;
	if (!renderOffscreen(request, pixels))
	{
		return false;
	}

	const std::size_t count = static_cast<std::size_t>(w) * h;
	depths.resize(count);

	if (depthFormat == VK_FORMAT_D32_SFLOAT)
	{
		const auto* src = reinterpret_cast<const float*>(pixels.data());
		std::copy(src, src + count, depths.begin());
	}
	else
	{
		const auto* src = reinterpret_cast<const uint16_t*>(pixels.data());
		for (std::size_t i = 0; i < count; ++i)
		{
			depths[i] = static_cast<float>(src[i]) / 65535.0f;
		}
	}

	width  = w;
	height = h;

	return true;
}

bool ccVSGWindowInterface::getClick3DPos(int x, int y, CCVector3d& P3D)
{
	std::vector<float> depths;
	uint32_t           imgWidth  = 0;
	uint32_t           imgHeight = 0;

	if (!renderDepthPass(depths, imgWidth, imgHeight))
	{
		return false;
	}

	if (x < 0 || y < 0 || x >= static_cast<int>(imgWidth) || y >= static_cast<int>(imgHeight))
	{
		return false;
	}

	// the image is stored top down while CloudCompare works in a Y-up screen
	// space (see the OpenGL backend)
	const uint32_t yUp = imgHeight - 1 - static_cast<uint32_t>(y);

	// the cursor may land one pixel off a thin surface (a point cloud edge, a
	// wireframe), so a small neighborhood is searched and the nearest valid
	// (non background) pixel to the cursor is kept - mirroring the tolerance of
	// the entity picking (M6.6). The exact pixel wins when it is valid (d2 = 0)
	constexpr int kPickRadius = 1; // 3x3; widen for more tolerance

	int      bestD2  = -1;
	float    bestVk  = 0.0f;
	uint32_t bestX   = 0;
	uint32_t bestYUp = 0;

	for (int j = -kPickRadius; j <= kPickRadius; ++j)
	{
		const int yy = y + j;
		if (yy < 0 || yy >= static_cast<int>(imgHeight))
		{
			continue;
		}
		const uint32_t yU = static_cast<uint32_t>(imgHeight - 1 - yy);

		for (int i = -kPickRadius; i <= kPickRadius; ++i)
		{
			const int xx = x + i;
			if (xx < 0 || xx >= static_cast<int>(imgWidth))
			{
				continue;
			}

			const float d = depths[static_cast<std::size_t>(yU) * imgWidth + static_cast<uint32_t>(xx)];

			// reverse depth: 0 is the far plane, i.e. nothing was drawn there
			// (the OpenGL backend tests the very same thing with its
			// INVALID_DEPTH = 1.0)
			if (d <= 0.0f)
			{
				continue;
			}

			const int d2 = i * i + j * j;
			if (bestD2 < 0 || d2 < bestD2)
			{
				bestD2  = d2;
				bestVk  = d;
				bestX   = static_cast<uint32_t>(xx);
				bestYUp = yU;
			}
		}
	}

	if (bestD2 < 0)
	{
		return false;
	}

	// ccGLCameraParameters::unproject() expects an OpenGL window depth:
	// 0 on the near plane and 1 on the far one - exactly the opposite of the
	// Vulkan reverse depth
	CCVector3d P2D(static_cast<double>(bestX), static_cast<double>(bestYUp), 1.0 - static_cast<double>(bestVk));

	ccGLCameraParameters camera;
	getGLCameraParameters(camera);

	return camera.unproject(P2D, P3D);
}

void ccVSGWindowInterface::processMouseDoubleClick(int x, int y)
{
	CCVector3d P;
	if (getClick3DPos(x, y, P))
	{
		setPivotPoint(P, true, true);
	}
}

void ccVSGWindowInterface::showCursorCoordinates(bool state)
{
	m_showCursorCoordinates = state;
}

bool ccVSGWindowInterface::showCursorCoordinates() const
{
	return m_showCursorCoordinates;
}

void ccVSGWindowInterface::processMouseMove(int x, int y)
{
	// the offscreen depth read is relatively expensive, so it is deferred (like
	// the picking) and coalesced: at most one read per event loop turn, even
	// though MOVE events arrive at a much higher rate
	if (!m_showCursorCoordinates || m_cursorCoordScheduled)
	{
		return;
	}

	m_cursorCoordScheduled = true;

	scheduleDeferredAction([this, x, y]()
	                       {
		                       m_cursorCoordScheduled = false;

		                       CCVector3d P;
		                       if (getClick3DPos(x, y, P))
		                       {
			                       Q_EMIT m_signalEmitter->cursorCoordinates(P);
		                       }
	                       });
}

void ccVSGWindowInterface::doEntityPicking(int x, int y)
{
	std::vector<uint32_t> ids;
	uint32_t              imgWidth  = 0;
	uint32_t              imgHeight = 0;

	const bool ok = renderIdPass(ids, imgWidth, imgHeight);

	ccHObject*              pickedEntity = nullptr;
	std::unordered_set<int> selectedIDs;

	if (ok && imgWidth > 0 && imgHeight > 0)
	{
		// same picking area as the OpenGL backend: a few pixels around the
		// cursor, so that thin entities stay clickable
		constexpr int pickWidth  = 5;
		constexpr int pickHeight = 5;

		const int xTop = std::max(0, x - pickWidth / 2);
		const int yTop = std::max(0, y - pickHeight / 2);
		const int xEnd = std::min(static_cast<int>(imgWidth), xTop + pickWidth);
		const int yEnd = std::min(static_cast<int>(imgHeight), yTop + pickHeight);

		int      minSquareDist = -1;
		uint32_t nearestId     = 0;

		for (int j = yTop; j < yEnd; ++j)
		{
			for (int i = xTop; i < xEnd; ++i)
			{
				const uint32_t id = ids[static_cast<std::size_t>(j) * imgWidth + i];

				// 0 = background, i.e. nothing was drawn on this pixel
				if (id == 0)
				{
					continue;
				}

				if (m_pickingMode == ENTITY_RECT_PICKING)
				{
					// the rectangular mode reports every entity of the area
					selectedIDs.insert(static_cast<int>(id));
				}
				else
				{
					// ... while the standard mode keeps the hit that is the
					// closest to the cursor (like the OpenGL backend does)
					const int dX = i - x;
					const int dY = j - y;
					const int d2 = dX * dX + dY * dY;

					if (minSquareDist < 0 || d2 < minSquareDist)
					{
						minSquareDist = d2;
						nearestId     = id;
					}
				}
			}
		}

		if (m_pickingMode != ENTITY_RECT_PICKING && nearestId != 0)
		{
			pickedEntity = m_sceneBuilder.entityForId(nearestId);

			if (pickedEntity)
			{
				selectedIDs.insert(static_cast<int>(nearestId));
			}
		}
	}

	// the OpenGL backend always emits a signal, even when nothing was picked
	switch (m_pickingMode)
	{
	case ENTITY_PICKING:
		Q_EMIT m_signalEmitter->entitySelectionChanged(pickedEntity);
		break;

	case ENTITY_RECT_PICKING:
		Q_EMIT m_signalEmitter->entitiesSelectionChanged(selectedIDs);
		break;

	case FAST_PICKING:
		Q_EMIT m_signalEmitter->itemPickedFast(pickedEntity, -1, x, y);
		break;

	default:
		break;
	}
}
