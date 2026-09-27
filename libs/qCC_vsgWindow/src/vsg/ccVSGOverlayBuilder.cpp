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
#include <vsg/ccVSGFontBuilder.h>
#include <vsg/ccVSGOverlayBuilder.h>
#include <vsg/ccVSGShaders.h>

// qCC_db
#include <ccColorTypes.h>
#include <ccScalarField.h>

// VSG
#include <vsg/all.h>

// system
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	//! Length of the trihedron axes
	/** ccGLWindowInterface: CC_DISPLAYED_TRIHEDRON_AXES_LENGTH **/
	constexpr float TrihedronAxesLength = 25.0f;

	//! Margin between the axis tip and its label
	/** ccGLWindowInterface: CC_TRIHEDRON_TEXT_MARGIN **/
	constexpr float TrihedronTextMargin = 5.0f;

	//! Rough horizontal advance of the 'X' label
	/** TODO(M5.2): use the real font metrics of the built atlas **/
	constexpr float TrihedronLabelAdvance = 8.0f;

	//! Height of the axis labels, in pixels
	constexpr float LabelHeightPx = 14.0f;

	//! Distance between the axis tip and its label, in pixels
	constexpr float LabelOffsetPx = 7.0f;

	inline vsg::ubvec4 toColor(const ccColor::Rgba& c)
	{
		return vsg::ubvec4(c.r, c.g, c.b, c.a);
	}

	//! Removes a node from a group (vsg::Group has no removeChild())
	void unmount(vsg::Group* root, vsg::Node* node)
	{
		if (!root || !node)
		{
			return;
		}

		auto& children = root->children;
		children.erase(std::remove_if(children.begin(),
		                              children.end(),
		                              [node](const vsg::ref_ptr<vsg::Node>& n)
		                              {
			                              return n.get() == node;
		                              }),
		               children.end());
	}

	//! Builds a draw node for the overlay: no lighting, no depth test
	/** The overlay is drawn after the 3D pass inside the same render pass, so
	    it must never be occluded by - nor occlude - the 3D geometry. **/
	vsg::ref_ptr<vsg::Node> buildGeometry(vsg::ref_ptr<vsg::ShaderSet>     shaderSet,
	                                      vsg::ref_ptr<vsg::SharedObjects> sharedObjects,
	                                      vsg::ref_ptr<vsg::vec3Array>     vertices,
	                                      vsg::ref_ptr<vsg::ubvec4Array>   colors)
	{
		if (!shaderSet || !vertices || !colors || vertices->empty())
		{
			return {};
		}

		auto config = vsg::GraphicsPipelineConfigurator::create(shaderSet);

		// enableArray() only declares the pipeline vertex layout: the arrays are
		// then pushed in the very same order (see ccVSGMeshBuilder).
		config->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
		config->enableArray("vsg_Color", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::ubvec4), VK_FORMAT_R8G8B8A8_UNORM);

		vsg::DataList arrays;
		arrays.push_back(vertices);
		arrays.push_back(colors);

		for (auto& state : config->pipelineStates)
		{
			if (auto* dss = dynamic_cast<vsg::DepthStencilState*>(state.get()))
			{
				dss->depthTestEnable  = VK_FALSE;
				dss->depthWriteEnable = VK_FALSE;
			}
			else if (auto* rs = dynamic_cast<vsg::RasterizationState*>(state.get()))
			{
				rs->cullMode = VK_CULL_MODE_NONE;
			}
		}

		config->init();

		auto stateGroup = vsg::StateGroup::create();
		config->copyTo(stateGroup, sharedObjects);

		auto draw = vsg::VertexDraw::create();
		draw->assignArrays(arrays);
		draw->vertexCount   = static_cast<uint32_t>(vertices->size());
		draw->instanceCount = 1;

		stateGroup->addChild(draw);

		return stateGroup;
	}
} // namespace

ccVSGOverlayBuilder::ccVSGOverlayBuilder()
    : m_root(vsg::Group::create())
    , m_lineShaderSet(ccVSGShaders::createFlatShaderSet(VK_PRIMITIVE_TOPOLOGY_LINE_LIST))
    , m_triangleShaderSet(ccVSGShaders::createFlatShaderSet(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
    , m_sharedObjects(vsg::SharedObjects::create())
{
	// glyph atlas used by the overlay text (M5.2) - may be null when freetype
	// is not available or when no system font was found
	m_font = ccVSGFontBuilder::buildFont(ccVSGFontBuilder::defaultFontFile());

	createTrihedron();

	if (m_trihedron)
	{
		m_root->addChild(m_trihedron);
		m_trihedronMounted = true;
	}

	createTrihedronLabels();
}

vsg::ref_ptr<vsg::Node> ccVSGOverlayBuilder::createLabel(const char* str, const ccColor::Rgba& color)
{
	if (!m_font)
	{
		return {};
	}

	auto text = vsg::Text::create();
	text->font      = m_font;
	text->shaderSet = vsg::createTextShaderSet();
	text->technique = vsg::CpuLayoutTechnique::create();
	text->text      = vsg::stringValue::create(str);

	auto layout = vsg::StandardLayout::create();
	// the glyph metrics are normalized to a line height of 1.0, so the layout
	// vectors give the text size directly in overlay pixels
	layout->horizontal          = vsg::vec3(LabelHeightPx, 0.0f, 0.0f);
	layout->vertical            = vsg::vec3(0.0f, LabelHeightPx, 0.0f);
	layout->position            = vsg::vec3(0.0f, 0.0f, 0.0f);
	layout->horizontalAlignment = vsg::StandardLayout::CENTER_ALIGNMENT;
	layout->verticalAlignment   = vsg::StandardLayout::CENTER_ALIGNMENT;
	layout->color               = vsg::vec4(static_cast<float>(color.r) / 255.0f,
	                                        static_cast<float>(color.g) / 255.0f,
	                                        static_cast<float>(color.b) / 255.0f,
	                                        1.0f);
	text->layout = layout;

	// builds the rendering subgraph (vertex arrays + text pipeline)
	text->setup(0, {});

	// the label is moved through a transform: changing the matrix does not
	// require any recompilation, whereas rebuilding the text quads would
	auto transform = vsg::MatrixTransform::create();
	transform->addChild(text);

	return transform;
}

void ccVSGOverlayBuilder::createTrihedronLabels()
{
	if (!m_font)
	{
		return;
	}

	static const char*      labelText[3]  = {"X", "Y", "Z"};
	const ccColor::Rgba     labelColors[3] = {
	    ccColor::Rgba(ccColor::red.r, ccColor::red.g, ccColor::red.b, 255),
	    ccColor::Rgba(ccColor::green.r, ccColor::green.g, ccColor::green.b, 255),
	    ccColor::Rgba(ccColor::blueCC.r, ccColor::blueCC.g, ccColor::blueCC.b, 255)};

	for (unsigned k = 0; k < 3; ++k)
	{
		m_axisLabels[k] = dynamic_cast<vsg::MatrixTransform*>(createLabel(labelText[k], labelColors[k]).get());
		if (m_axisLabels[k])
		{
			m_root->addChild(m_axisLabels[k]);
		}
	}
}

void ccVSGOverlayBuilder::createTrihedron()
{
	auto verts  = vsg::vec3Array::create(6);
	auto colors = vsg::ubvec4Array::create(6);

	const vsg::vec3 tips[3] = {vsg::vec3(TrihedronAxesLength, 0.0f, 0.0f),
	                           vsg::vec3(0.0f, TrihedronAxesLength, 0.0f),
	                           vsg::vec3(0.0f, 0.0f, TrihedronAxesLength)};

	// same colors as ccGLWindowInterface::drawTrihedron()
	const ccColor::Rgba axisColors[3] = {
	    ccColor::Rgba(ccColor::red.r, ccColor::red.g, ccColor::red.b, 255),
	    ccColor::Rgba(ccColor::green.r, ccColor::green.g, ccColor::green.b, 255),
	    ccColor::Rgba(ccColor::blueCC.r, ccColor::blueCC.g, ccColor::blueCC.b, 255)};

	for (unsigned k = 0; k < 3; ++k)
	{
		(*verts)[2 * k].set(0.0f, 0.0f, 0.0f);
		(*verts)[2 * k + 1] = tips[k];

		const vsg::ubvec4 c = toColor(axisColors[k]);
		(*colors)[2 * k]     = c;
		(*colors)[2 * k + 1] = c;
	}

	vsg::ref_ptr<vsg::Node> content = buildGeometry(m_lineShaderSet, m_sharedObjects, verts, colors);
	if (!content)
	{
		return;
	}

	// the axes are expressed in world coordinates and rotated by the camera
	// view matrix, exactly like the OpenGL backend does
	m_trihedron = vsg::MatrixTransform::create();
	m_trihedron->addChild(content);
}

void ccVSGOverlayBuilder::update(int width, int height, const vsg::dmat4& viewMatrix, bool showTrihedron)
{
	if (!m_root)
	{
		return;
	}

	const float halfW = static_cast<float>(width) * 0.5f;
	const float halfH = static_cast<float>(height) * 0.5f;

	// mirrors ccGLWindowInterface::computeTrihedronLength()
	const float trihedronLength = TrihedronAxesLength + TrihedronTextMargin + TrihedronLabelAdvance;

	// mirrors ccGLWindowInterface::drawTrihedron():
	//   glTranslatef(centerX, -centerY, 0) then glMultMatrixd(viewMat)
	const float centerX = halfW - trihedronLength - 10.0f;
	const float centerY = halfH - trihedronLength - 5.0f;

	if (m_trihedron)
	{
		m_trihedron->matrix = vsg::translate(static_cast<double>(centerX),
		                                     static_cast<double>(-centerY),
		                                     0.0)
		                      * viewMatrix;

		// axis labels: placed just beyond the (projected) axis tips
		static const float axisTips[3][3] = {{TrihedronAxesLength, 0.0f, 0.0f},
		                                     {0.0f, TrihedronAxesLength, 0.0f},
		                                     {0.0f, 0.0f, TrihedronAxesLength}};

		for (unsigned k = 0; k < 3; ++k)
		{
			if (!m_axisLabels[k])
			{
				continue;
			}

			const vsg::dvec4 tip = viewMatrix * vsg::dvec4(axisTips[k][0], axisTips[k][1], axisTips[k][2], 1.0);

			double labelX = centerX + tip.x;
			double labelY = -centerY + tip.y;

			// push the label a bit further along the axis direction
			const double len = std::sqrt(tip.x * tip.x + tip.y * tip.y);
			if (len > 1.0e-6)
			{
				labelX += tip.x / len * LabelOffsetPx;
				labelY += tip.y / len * LabelOffsetPx;
			}

			m_axisLabels[k]->matrix = vsg::translate(labelX, labelY, 0.0);
		}

		// show / hide: the nodes are simply (un)mounted from the overlay root
		if (showTrihedron && !m_trihedronMounted)
		{
			m_root->addChild(m_trihedron);
			for (auto& label : m_axisLabels)
			{
				if (label)
				{
					m_root->addChild(label);
				}
			}
			m_trihedronMounted = true;
		}
		else if (!showTrihedron && m_trihedronMounted)
		{
			for (auto& label : m_axisLabels)
			{
				unmount(m_root, label);
			}
			unmount(m_root, m_trihedron);
			m_trihedronMounted = false;
		}
	}
}

bool ccVSGOverlayBuilder::updateColorScale(const ccScalarField* sf, int width, int height)
{
	// ------------------------------------------------------------------
	// fingerprint: the group is only rebuilt when something visible changed
	// ------------------------------------------------------------------
	quint64 signature = 17;
	auto    mix       = [&signature](quint64 v) { signature = signature * 1000003 + v; };

	mix(static_cast<quint64>(reinterpret_cast<quintptr>(sf)));
	mix(static_cast<quint64>(width));
	mix(static_cast<quint64>(height));

	if (sf)
	{
		mix(static_cast<quint64>(sf->displayRange().start() * 1.0e6));
		mix(static_cast<quint64>(sf->displayRange().stop() * 1.0e6));
		mix(static_cast<quint64>(sf->getName().size()));
	}

	if (signature == m_colorScaleSignature)
	{
		return false;
	}
	m_colorScaleSignature = signature;

	// drop the previous color scale
	if (m_colorScaleMounted)
	{
		unmount(m_root, m_colorScale);
		m_colorScaleMounted = false;
	}
	m_colorScale = nullptr;

	if (!sf || !sf->getColorScale() || !m_font)
	{
		return true;
	}

	constexpr int ScaleWidth  = 30;
	constexpr int RightMargin = 20;
	constexpr int Steps       = 32;

	const int xEnd   = width - 1 - RightMargin;
	const int xStart = xEnd - ScaleWidth;
	const int yStart = 90;          // bottom, in GL pixel coordinates
	const int yStop  = height - 60; // top

	if (yStop - yStart < ScaleWidth)
	{
		// not enough room to display the color scale
		return true;
	}

	// GL pixel coordinates (origin: bottom left) -> overlay coordinates
	// (origin: centre of the viewport)
	const double halfW = width * 0.5;
	const double halfH = height * 0.5;
	auto         ovX   = [halfW](double x) { return x - halfW; };
	auto         ovY   = [halfH](double y) { return y - halfH; };

	auto group = vsg::Group::create();

	// ------------------------------------------------------------------
	// the gradient: one quad per step, coloured with the scalar field ramp
	// ------------------------------------------------------------------
	{
		const double vMin = static_cast<double>(sf->displayRange().start());
		const double vMax = static_cast<double>(sf->displayRange().stop());

		auto verts  = vsg::vec3Array::create(static_cast<std::size_t>(Steps) * 6);
		auto colors = vsg::ubvec4Array::create(static_cast<std::size_t>(Steps) * 6);

		std::size_t out = 0;
		for (int i = 0; i < Steps; ++i)
		{
			const double t0 = static_cast<double>(i) / Steps;
			const double t1 = static_cast<double>(i + 1) / Steps;

			const ccColor::Rgb* c0   = sf->getColor(static_cast<ScalarType>(vMin + (vMax - vMin) * t0));
			const ccColor::Rgb* c1   = sf->getColor(static_cast<ScalarType>(vMin + (vMax - vMin) * t1));
			const ccColor::Rgb  rgb0 = c0 ? *c0 : ccColor::lightGreyRGB;
			const ccColor::Rgb  rgb1 = c1 ? *c1 : ccColor::lightGreyRGB;
			const ccColor::Rgba ca0(rgb0.r, rgb0.g, rgb0.b, 255);
			const ccColor::Rgba ca1(rgb1.r, rgb1.g, rgb1.b, 255);

			const float y0 = static_cast<float>(ovY(yStart + (yStop - yStart) * t0));
			const float y1 = static_cast<float>(ovY(yStart + (yStop - yStart) * t1));
			const float xa = static_cast<float>(ovX(xStart));
			const float xb = static_cast<float>(ovX(xEnd));

			const vsg::vec3     tri[6] = {vsg::vec3(xa, y0, 0.0f), vsg::vec3(xb, y0, 0.0f), vsg::vec3(xb, y1, 0.0f),
			                              vsg::vec3(xa, y0, 0.0f), vsg::vec3(xb, y1, 0.0f), vsg::vec3(xa, y1, 0.0f)};
			const ccColor::Rgba col[6] = {ca0, ca0, ca1, ca0, ca1, ca1};

			for (unsigned k = 0; k < 6; ++k)
			{
				(*verts)[out]  = tri[k];
				(*colors)[out] = toColor(col[k]);
				++out;
			}
		}

		if (auto node = buildGeometry(m_triangleShaderSet, m_sharedObjects, verts, colors))
		{
			group->addChild(node);
		}
	}

	// ------------------------------------------------------------------
	// the scalar field name, above the ramp, and the extreme values
	// ------------------------------------------------------------------
	const ccColor::Rgba textColor(255, 255, 255, 255);
	const std::string&  sfName = sf->getName();
	const QString       title  = sfName.empty() ? QStringLiteral("Unnamed") : QString::fromStdString(sfName);

	if (auto node = createLabel(title.toUtf8().constData(), textColor))
	{
		if (auto* transform = dynamic_cast<vsg::MatrixTransform*>(node.get()))
		{
			transform->matrix = vsg::translate(ovX(xStart + ScaleWidth * 0.5), ovY(yStop + 18), 0.0);
		}
		group->addChild(node);
	}

	const QString minStr = QString::number(static_cast<double>(sf->displayRange().start()), 'g', 4);
	const QString maxStr = QString::number(static_cast<double>(sf->displayRange().stop()), 'g', 4);

	if (auto node = createLabel(minStr.toUtf8().constData(), textColor))
	{
		if (auto* transform = dynamic_cast<vsg::MatrixTransform*>(node.get()))
		{
			transform->matrix = vsg::translate(ovX(xStart - 26), ovY(yStart), 0.0);
		}
		group->addChild(node);
	}

	if (auto node = createLabel(maxStr.toUtf8().constData(), textColor))
	{
		if (auto* transform = dynamic_cast<vsg::MatrixTransform*>(node.get()))
		{
			transform->matrix = vsg::translate(ovX(xStart - 26), ovY(yStop), 0.0);
		}
		group->addChild(node);
	}

	m_colorScale = group;
	m_root->addChild(m_colorScale);
	m_colorScaleMounted = true;

	return true;
}
