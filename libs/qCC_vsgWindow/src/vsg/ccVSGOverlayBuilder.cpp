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

// qCC_glWindow (only for ccGui::Parameters(): the persistent display params)
#include <ccGuiParameters.h>

// system
#include <algorithm>

// qCC_db
#include <cc2DLabel.h>
#include <cc2DViewportLabel.h>
#include <ccColorTypes.h>
#include <ccImage.h>
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

	using PointList = std::vector<vsg::vec3>;
	using ColorList = std::vector<ccColor::Rgba>;

	inline vsg::ubvec4 toColor(const ccColor::Rgba& c)
	{
		return vsg::ubvec4(c.r, c.g, c.b, c.a);
	}

	//! Projects a world point to the overlay coordinate system (centre origin)
	/** \return false when the point is behind the camera **/
	bool projectToOverlay(const vsg::dmat4& projection,
	                      const vsg::dmat4& view,
	                      const vsg::dvec3& p,
	                      int               width,
	                      int               height,
	                      double&           outX,
	                      double&           outY)
	{
		const vsg::dvec4 clip = (projection * view) * vsg::dvec4(p.x, p.y, p.z, 1.0);
		if (std::abs(clip.w) < 1.0e-12)
		{
			return false;
		}

		const double ndcX = clip.x / clip.w;
		const double ndcY = clip.y / clip.w;

		// Vulkan convention: ndc y = +1 is the *bottom* of the viewport
		outX = (ndcX + 1.0) * 0.5 * static_cast<double>(width) - static_cast<double>(width) * 0.5;
		outY = static_cast<double>(height) * 0.5 - (ndcY + 1.0) * 0.5 * static_cast<double>(height);

		return true;
	}

	//! Radius (in pixels) of the 3D marker of a cc2DLabel
	/** ccGui::Parameters().labelMarkerSize is expressed in screen pixels, and
	    the marker is drawn in screen space, so it can be used directly. **/
	inline double labelMarkerRadiusPx()
	{
		const unsigned s = ccGui::Parameters().labelMarkerSize;
		return (s > 0 ? static_cast<double>(s) : 5.0);
	}

	//! Appends a unit sphere (radius 1) as a flat triangle list
	/** \param verts  output positions
	    \param norms  output normals (equal to the positions for a unit sphere) **/
	void appendSphere(PointList& verts, std::vector<vsg::vec3>& norms, int rings, int sectors)
	{
		constexpr float Pi = 3.14159265f;

		auto onSphere = [](float phi, float theta)
		{
			return vsg::vec3(std::sin(phi) * std::cos(theta),
			                 std::sin(phi) * std::sin(theta),
			                 std::cos(phi));
		};

		for (int r = 0; r < rings; ++r)
		{
			const float phi0 = static_cast<float>(r) / rings * Pi;
			const float phi1 = static_cast<float>(r + 1) / rings * Pi;

			for (int s = 0; s < sectors; ++s)
			{
				const float th0 = static_cast<float>(s) / sectors * 2.0f * Pi;
				const float th1 = static_cast<float>(s + 1) / sectors * 2.0f * Pi;

				const vsg::vec3 a = onSphere(phi0, th0);
				const vsg::vec3 b = onSphere(phi1, th0);
				const vsg::vec3 c = onSphere(phi1, th1);
				const vsg::vec3 d = onSphere(phi0, th1);

				const vsg::vec3 tri1[3] = {a, b, c};
				const vsg::vec3 tri2[3] = {a, c, d};

				for (const auto& p : tri1)
				{
					verts.push_back(p);
					norms.push_back(p);
				}
				for (const auto& p : tri2)
				{
					verts.push_back(p);
					norms.push_back(p);
				}
			}
		}
	}

	//! Collects the visible 2D labels of a ccHObject tree
	void collectLabels(ccHObject*                          obj,
	                   std::vector<const cc2DLabel*>&      labels2D,
	                   std::vector<const cc2DViewportLabel*>& roiLabels)
	{
		if (!obj || !obj->isEnabled())
		{
			return;
		}

		if (obj->isVisible() || obj->isSelected())
		{
			if (auto* label = dynamic_cast<const cc2DLabel*>(obj))
			{
				labels2D.push_back(label);
			}
			else if (auto* roi = dynamic_cast<const cc2DViewportLabel*>(obj))
			{
				roiLabels.push_back(roi);
			}
		}

		for (unsigned i = 0; i < obj->getChildrenNumber(); ++i)
		{
			collectLabels(obj->getChild(i), labels2D, roiLabels);
		}
	}

	//! Rounds a displayed width to a readable value
	/** Mirrors ccGLWindowInterface::RoundScale(): avoids labels with a lot of
	    decimals by snapping to a granularity of 0.5 * 10^k. **/
	double roundScale(double equivalentWidth)
	{
		if (equivalentWidth <= 0.0)
		{
			return 0.0;
		}

		const int    k           = static_cast<int>(std::floor(std::log(equivalentWidth) / std::log(10.0)));
		const double granularity = std::pow(10.0, static_cast<double>(k)) / 2.0;

		return std::floor(std::max(equivalentWidth / granularity, 1.0)) * granularity;
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
    , m_texturedShaderSet(ccVSGShaders::createTexturedShaderSet())
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
	// m_labelFont is a superset of m_font (ASCII + the extra code points the
	// labels use, e.g. CJK); it is preferred whenever it is available
	vsg::ref_ptr<vsg::Font> font = (m_labelFont ? m_labelFont : m_font);

	if (!font)
	{
		return {};
	}

	auto text = vsg::Text::create();
	text->font      = font;
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
	// the histogram: one horizontal bar per bin, drawn on the left of the ramp
	// (ccGui::Parameters().colorScaleShowHistogram)
	// ------------------------------------------------------------------
	{
		const ccScalarField::Histogram& hist = sf->getHistogram();

		if (ccGui::Parameters().colorScaleShowHistogram && hist.size() > 1 && hist.maxValue > 0)
		{
			constexpr int HistWidth = 40;

			const std::size_t   bins  = hist.size();
			const vsg::ubvec4   barCol = toColor(ccColor::Rgba(210, 210, 210, 170));
			const float         xRight = static_cast<float>(ovX(xStart));
			const float         xLeft  = static_cast<float>(ovX(xStart - HistWidth));

			auto verts  = vsg::vec3Array::create(bins * 6);
			auto colors = vsg::ubvec4Array::create(bins * 6);

			std::size_t out = 0;
			for (std::size_t i = 0; i < bins; ++i)
			{
				const double t0 = static_cast<double>(i) / static_cast<double>(bins);
				const double t1 = static_cast<double>(i + 1) / static_cast<double>(bins);

				const float y0 = static_cast<float>(ovY(yStart + (yStop - yStart) * t0));
				const float y1 = static_cast<float>(ovY(yStart + (yStop - yStart) * t1));

				// the tallest bin fills the whole width, the others start
				// further to the left
				const float w  = static_cast<float>(hist[i]) / static_cast<float>(hist.maxValue);
				const float x0 = xLeft + (xRight - xLeft) * (1.0f - w);

				const vsg::vec3 tri[6] = {vsg::vec3(x0, y0, 0.0f), vsg::vec3(xRight, y0, 0.0f), vsg::vec3(xRight, y1, 0.0f),
				                          vsg::vec3(x0, y0, 0.0f), vsg::vec3(xRight, y1, 0.0f), vsg::vec3(x0, y1, 0.0f)};

				for (unsigned k = 0; k < 6; ++k)
				{
					(*verts)[out]  = tri[k];
					(*colors)[out] = barCol;
					++out;
				}
			}

			if (auto node = buildGeometry(m_triangleShaderSet, m_sharedObjects, verts, colors))
			{
				group->addChild(node);
			}
		}
	}

	// ------------------------------------------------------------------
	// intermediate ticks. With a logarithmic scalar field the ticks are
	// log spaced (and not linearly), so that they match the ramp.
	// ------------------------------------------------------------------
	{
		constexpr int NTicks = 3; // at 25%, 50% and 75%

		const double vMin = static_cast<double>(sf->displayRange().start());
		const double vMax = static_cast<double>(sf->displayRange().stop());

		auto verts  = vsg::vec3Array::create(NTicks * 2);
		auto colors = vsg::ubvec4Array::create(NTicks * 2);

		const vsg::ubvec4 tickCol = toColor(ccGui::Parameters().textDefaultCol);

		for (int i = 0; i < NTicks; ++i)
		{
			const double f = static_cast<double>(i + 1) / static_cast<double>(NTicks + 1);

			double t = f;
			if (sf->logScale() && vMin > 0.0 && vMax > vMin)
			{
				// the tick shows a value that is log spaced between vMin and
				// vMax; its position on the (linear) ramp follows
				const double lv = std::log10(vMin) + (std::log10(vMax) - std::log10(vMin)) * f;
				t               = (std::pow(10.0, lv) - vMin) / (vMax - vMin);
			}

			const float y  = static_cast<float>(ovY(yStart + (yStop - yStart) * t));
			const float xa = static_cast<float>(ovX(xEnd));
			const float xb = static_cast<float>(ovX(xEnd + 6));

			(*verts)[static_cast<std::size_t>(i) * 2].set(xa, y, 0.0f);
			(*verts)[static_cast<std::size_t>(i) * 2 + 1].set(xb, y, 0.0f);
			(*colors)[static_cast<std::size_t>(i) * 2]      = tickCol;
			(*colors)[static_cast<std::size_t>(i) * 2 + 1]  = tickCol;
		}

		if (auto node = buildGeometry(m_lineShaderSet, m_sharedObjects, verts, colors))
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

bool ccVSGOverlayBuilder::updateScaleBar(bool show, double pixelSize, int width, int height)
{
	// ------------------------------------------------------------------
	// fingerprint
	// ------------------------------------------------------------------
	quint64 signature = 17;
	auto    mix       = [&signature](quint64 v) { signature = signature * 1000003 + v; };

	mix(show ? 1 : 2);
	mix(static_cast<quint64>(pixelSize * 1.0e9));
	mix(static_cast<quint64>(width));
	mix(static_cast<quint64>(height));

	if (signature == m_scaleBarSignature)
	{
		return false;
	}
	m_scaleBarSignature = signature;

	if (m_scaleBarMounted)
	{
		unmount(m_root, m_scaleBar);
		m_scaleBarMounted = false;
	}
	m_scaleBar = nullptr;

	if (!show || pixelSize <= 0.0 || !m_font)
	{
		return true;
	}

	// ------------------------------------------------------------------
	// geometry (see ccGLWindowInterface::drawScale())
	// ------------------------------------------------------------------
	const float  scaleMaxW        = static_cast<float>(width) / 4.0f;
	const double equivalentWidth  = roundScale(scaleMaxW * pixelSize);
	const float  scaleW_pix       = static_cast<float>(equivalentWidth / pixelSize);

	const float trihedronLength = TrihedronAxesLength + TrihedronTextMargin + TrihedronLabelAdvance;
	const float dW              = 2.0f * trihedronLength + 20.0f;
	const float dH              = std::max(LabelHeightPx * 1.25f, trihedronLength + 5.0f);
	const float w               = static_cast<float>(width) / 2.0f - dW;
	const float h               = static_cast<float>(height) / 2.0f - dH;
	const float tick            = 3.0f;

	// drawScale() uses the very same centred coordinate system as the overlay
	auto group = vsg::Group::create();

	// TODO(M5.4): use ccGui::Parameters().textDefaultCol
	const ccColor::Rgba barColor(255, 255, 255, 255);

	const vsg::vec3 segments[6] = {vsg::vec3(w - scaleW_pix, -h, 0.0f),
	                               vsg::vec3(w, -h, 0.0f),
	                               vsg::vec3(w - scaleW_pix, -h - tick, 0.0f),
	                               vsg::vec3(w - scaleW_pix, -h + tick, 0.0f),
	                               vsg::vec3(w, -h + tick, 0.0f),
	                               vsg::vec3(w, -h - tick, 0.0f)};

	auto verts  = vsg::vec3Array::create(6);
	auto colors = vsg::ubvec4Array::create(6);
	const vsg::ubvec4 c = toColor(barColor);

	for (unsigned i = 0; i < 6; ++i)
	{
		(*verts)[i]  = segments[i];
		(*colors)[i] = c;
	}

	if (auto node = buildGeometry(m_lineShaderSet, m_sharedObjects, verts, colors))
	{
		group->addChild(node);
	}

	// ---- the equivalent width, below the bar ----
	const QString text = QString::number(equivalentWidth);

	if (auto node = createLabel(text.toUtf8().constData(), barColor))
	{
		if (auto* transform = dynamic_cast<vsg::MatrixTransform*>(node.get()))
		{
			transform->matrix = vsg::translate(w - scaleW_pix * 0.5, -h - LabelHeightPx * 0.4, 0.0);
		}
		group->addChild(node);
	}

	m_scaleBar = group;
	m_root->addChild(m_scaleBar);
	m_scaleBarMounted = true;

	return true;
}

bool ccVSGOverlayBuilder::updateImages(const std::vector<const ccImage*>& images, int width, int height)
{
	quint64 signature = 17;
	auto    mix       = [&signature](quint64 v) { signature = signature * 1000003 + v; };

	mix(static_cast<quint64>(width));
	mix(static_cast<quint64>(height));

	for (const ccImage* image : images)
	{
		mix(static_cast<quint64>(reinterpret_cast<quintptr>(image)));
		if (image)
		{
			mix(static_cast<quint64>(image->getAlpha() * 1.0e6));
			mix(static_cast<quint64>(image->data().width()));
			mix(static_cast<quint64>(image->data().height()));
		}
	}

	if (signature == m_imageSignature)
	{
		return false;
	}
	m_imageSignature = signature;

	if (m_imageMounted)
	{
		unmount(m_root, m_imageNode);
		m_imageMounted = false;
	}
	m_imageNode = nullptr;

	// one textured quad per image - CloudCompare draws all the visible ones
	auto group = vsg::Group::create();

	for (const ccImage* image : images)
	{
		if (auto node = createImageQuad(image, width, height))
		{
			group->addChild(node);
		}
	}

	if (group->children.empty())
	{
		return true;
	}

	m_imageNode = group;
	m_root->addChild(m_imageNode);
	m_imageMounted = true;

	return true;
}

vsg::ref_ptr<vsg::Node> ccVSGOverlayBuilder::createImageQuad(const ccImage* image, int width, int height)
{
	if (!image || image->data().isNull() || !m_texturedShaderSet)
	{
		return {};
	}

	const QSizeF displayedSize = image->computeDisplayedSize(width, height);
	if (displayedSize.width() <= 0 || displayedSize.height() <= 0)
	{
		return {};
	}

	const float w = static_cast<float>(displayedSize.width() / 2);
	const float h = static_cast<float>(displayedSize.height() / 2);
	const float a = image->getAlpha();

	// ---- the image, converted to a RGBA array ----
	const QImage rgba = image->data().convertToFormat(QImage::Format_RGBA8888);
	if (rgba.isNull())
	{
		return {};
	}

	auto pixels = vsg::ubvec4Array2D::create(static_cast<uint32_t>(rgba.width()), static_cast<uint32_t>(rgba.height()));
	for (int y = 0; y < rgba.height(); ++y)
	{
		for (int x = 0; x < rgba.width(); ++x)
		{
			const QRgb p = rgba.pixel(x, y);
			pixels->at(static_cast<uint32_t>(x), static_cast<uint32_t>(y)) =
			    vsg::ubvec4(static_cast<uint8_t>(qRed(p)),
			                static_cast<uint8_t>(qGreen(p)),
			                static_cast<uint8_t>(qBlue(p)),
			                static_cast<uint8_t>(qAlpha(p)));
		}
	}

	// ---- geometry: a quad centred on the viewport ----
	// the texture coordinates are the ones of ccImage::drawMeOnly(), which
	// already match the Vulkan convention (v increases downwards)
	auto verts     = vsg::vec3Array::create(6);
	auto texcoords = vsg::vec2Array::create(6);
	auto colors    = vsg::ubvec4Array::create(6);

	const vsg::vec3 tri[6]  = {vsg::vec3(-w, -h, 0.0f), vsg::vec3(w, -h, 0.0f), vsg::vec3(w, h, 0.0f),
	                           vsg::vec3(-w, -h, 0.0f), vsg::vec3(w, h, 0.0f),  vsg::vec3(-w, h, 0.0f)};
	const vsg::vec2 uv[6]   = {vsg::vec2(0.0f, 1.0f), vsg::vec2(1.0f, 1.0f), vsg::vec2(1.0f, 0.0f),
	                           vsg::vec2(0.0f, 1.0f), vsg::vec2(1.0f, 0.0f), vsg::vec2(0.0f, 0.0f)};
	const vsg::ubvec4 white(255, 255, 255, static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f));

	for (unsigned i = 0; i < 6; ++i)
	{
		(*verts)[i]     = tri[i];
		(*texcoords)[i] = uv[i];
		(*colors)[i]    = white;
	}

	auto config = vsg::GraphicsPipelineConfigurator::create(m_texturedShaderSet);

	config->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
	config->enableArray("vsg_TexCoord0", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec2), VK_FORMAT_R32G32_SFLOAT);
	config->enableArray("vsg_Color", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::ubvec4), VK_FORMAT_R8G8B8A8_UNORM);

	vsg::DataList arrays;
	arrays.push_back(verts);
	arrays.push_back(texcoords);
	arrays.push_back(colors);

	config->enableTexture("diffuseMap");

	auto sampler = vsg::Sampler::create();
	sampler->magFilter = VK_FILTER_LINEAR;
	sampler->minFilter = VK_FILTER_LINEAR;
	config->assignTexture("diffuseMap", pixels, sampler);

	// the overlay is drawn on top of the 3D image, without depth
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
		else if (auto* cbs = dynamic_cast<vsg::ColorBlendState*>(state.get()))
		{
			// the image has an alpha channel (ccImage::m_texAlpha)
			cbs->configureAttachments(true);
		}
	}

	config->init();

	auto stateGroup = vsg::StateGroup::create();
	config->copyTo(stateGroup, m_sharedObjects);

	auto draw = vsg::VertexDraw::create();
	draw->assignArrays(arrays);
	draw->vertexCount   = 6;
	draw->instanceCount = 1;

	stateGroup->addChild(draw);

	return stateGroup;
}

vsg::ref_ptr<vsg::Node> ccVSGOverlayBuilder::createLabelMarker(const ccColor::Rgba& color)
{
	PointList              verts;
	std::vector<vsg::vec3> norms;
	appendSphere(verts, norms, 12, 12);

	if (verts.empty())
	{
		return {};
	}

	auto v = vsg::vec3Array::create(verts.size());
	auto c = vsg::ubvec4Array::create(verts.size());

	for (std::size_t i = 0; i < verts.size(); ++i)
	{
		(*v)[i] = verts[i];

		// cheap headlight from +Z, baked into the vertex colors
		const float shade = 0.35f + 0.65f * std::max(0.0f, norms[i].z);

		const vsg::ubvec4 base = toColor(color);
		(*c)[i] = vsg::ubvec4(static_cast<uint8_t>(std::min(255.0f, base.r * shade)),
		                      static_cast<uint8_t>(std::min(255.0f, base.g * shade)),
		                      static_cast<uint8_t>(std::min(255.0f, base.b * shade)),
		                      base.a);
	}

	return buildGeometry(m_triangleShaderSet, m_sharedObjects, v, c);
}

vsg::ref_ptr<vsg::Font> ccVSGOverlayBuilder::ensureLabelFont(const std::vector<uint32_t>& needed)
{
	// ASCII is always required: the trihedron, the scale bar and the color
	// scale all use the very same font
	std::vector<uint32_t> chars = needed;
	for (uint32_t c = ccVSGFontBuilder::DefaultFirstChar; c <= ccVSGFontBuilder::DefaultLastChar; ++c)
	{
		chars.push_back(c);
	}

	std::sort(chars.begin(), chars.end());
	chars.erase(std::unique(chars.begin(), chars.end()), chars.end());

	// the atlas is only rebuilt when the character set grows, which in
	// practice happens once, when the first CJK label shows up
	if (m_labelFont && chars == m_labelFontChars)
	{
		return m_labelFont;
	}

	auto font = ccVSGFontBuilder::buildFontFromChars(ccVSGFontBuilder::defaultFontFile(), chars);
	if (!font)
	{
		// keep the ASCII font (the CJK characters will simply be missing)
		return m_labelFont;
	}

	m_labelFont      = font;
	m_labelFontChars = chars;

	return font;
}

bool ccVSGOverlayBuilder::updateLabels(ccHObject*         root,
                                      const vsg::dmat4& viewMatrix,
                                      const vsg::dmat4& projectionMatrix,
                                      int               width,
                                      int               height)
{
	std::vector<const cc2DLabel*>           labels2D;
	std::vector<const cc2DViewportLabel*>   roiLabels;
	collectLabels(root, labels2D, roiLabels);

	// ------------------------------------------------------------------
	// the group is only rebuilt when the set of labels changes; moving the
	// camera merely updates the anchor transforms
	// ------------------------------------------------------------------
	quint64 signature = 17;
	auto    mix       = [&signature](quint64 v) { signature = signature * 1000003 + v; };

	for (auto* label : labels2D)
	{
		mix(static_cast<quint64>(reinterpret_cast<quintptr>(label)));
	}
	for (auto* roi : roiLabels)
	{
		mix(static_cast<quint64>(reinterpret_cast<quintptr>(roi)));
	}
	mix(static_cast<quint64>(width));
	mix(static_cast<quint64>(height));

	bool rebuilt = false;

	if (signature != m_labelsSignature)
	{
		m_labelsSignature = signature;

		if (m_labelsMounted)
		{
			unmount(m_root, m_labelsNode);
			m_labelsMounted = false;
		}
		m_labelsNode = nullptr;
		m_2DLabels.clear();
		m_2DLabelTransforms.clear();
		m_markerLabels.clear();
		m_markerPointIndex.clear();
		m_markerTransforms.clear();
		m_labelLinks.clear();

		auto group = vsg::Group::create();

		// ---- the label text may contain CJK: extend the font atlas ----
		{
			std::vector<uint32_t> chars;
			for (auto* label : labels2D)
			{
				for (uint c : label->getName().toUcs4())
				{
					chars.push_back(static_cast<uint32_t>(c));
				}
			}
			ensureLabelFont(chars);
		}

		const ccColor::Rgba labelColor        = ccGui::Parameters().textDefaultCol;
		const ccColor::Rgba defaultMarkerColor = ccGui::Parameters().labelMarkerCol;

		// offset between the 3D anchor and its name
		constexpr float LeaderDX = 10.0f;
		constexpr float LeaderDY = 14.0f;

		for (auto* label : labels2D)
		{
			auto transform = vsg::MatrixTransform::create();

			// the leader line, from the anchor to the text
			{
				auto verts  = vsg::vec3Array::create(2);
				auto colors = vsg::ubvec4Array::create(2);
				const vsg::ubvec4 c = toColor(labelColor);

				(*verts)[0].set(0.0f, 0.0f, 0.0f);
				(*verts)[1].set(LeaderDX, LeaderDY, 0.0f);
				(*colors)[0] = c;
				(*colors)[1] = c;

				if (auto node = buildGeometry(m_lineShaderSet, m_sharedObjects, verts, colors))
				{
					transform->addChild(node);
				}
			}

			// the name itself
			if (auto node = createLabel(label->getName().toUtf8().constData(), labelColor))
			{
				if (auto* textTransform = dynamic_cast<vsg::MatrixTransform*>(node.get()))
				{
					textTransform->matrix = vsg::translate(static_cast<double>(LeaderDX),
					                                        static_cast<double>(LeaderDY) + LabelHeightPx * 0.6,
					                                        0.0);
				}
				transform->addChild(node);
			}

			group->addChild(transform);
			m_2DLabels.push_back(label);
			m_2DLabelTransforms.push_back(transform);

			// ---- 3D marker: one sphere per picked point ----
			// (mirrors cc2DLabel::drawMeOnly3D(), case 1 - the sphere is drawn
			// in screen space, so its size stays constant)
			if (!m_markerSphere)
			{
				m_markerSphere = createLabelMarker(defaultMarkerColor);
			}

			if (label->isSelected() && !m_markerSphereSelected)
			{
				m_markerSphereSelected = createLabelMarker(ccColor::Rgba(255, 0, 0, 255));
			}

			vsg::ref_ptr<vsg::Node> markerNode = (label->isSelected() ? m_markerSphereSelected : m_markerSphere);

			if (markerNode)
			{
				for (unsigned p = 0; p < label->size(); ++p)
				{
					auto marker = vsg::MatrixTransform::create();
					marker->addChild(markerNode);

					group->addChild(marker);
					m_markerLabels.push_back(label);
					m_markerPointIndex.push_back(p);
					m_markerTransforms.push_back(marker);
				}
			}

			// ---- multi-point label: the connecting line, plus (3 points) the
			// semi-transparent triangle of cc2DLabel::drawMeOnly3D(). Both are
			// refreshed in place every frame (see the update loop below). ----
			const unsigned ptCount = label->size();

			if (ptCount >= 2)
			{
				if (!m_lineStripShaderSet)
				{
					m_lineStripShaderSet = ccVSGShaders::createFlatShaderSet(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
				}

				const vsg::ubvec4 linkColor = toColor(label->isSelected() ? ccColor::Rgba(255, 0, 0, 255) : labelColor);

				auto verts  = vsg::vec3Array::create(ptCount);
				auto colors = vsg::ubvec4Array::create(ptCount);
				for (unsigned k = 0; k < ptCount; ++k)
				{
					(*colors)[k] = linkColor;
				}

				if (auto node = buildGeometry(m_lineStripShaderSet, m_sharedObjects, verts, colors))
				{
					group->addChild(node);
					m_labelLinks.push_back(LabelLink{label, verts, ptCount});
				}
			}

			if (ptCount == 3)
			{
				// CC: static ccColor::Rgba DefaultTriangleColor(255,255,0,128)
				const vsg::ubvec4 triColor = toColor(ccColor::Rgba(255, 255, 0, 128));

				auto verts  = vsg::vec3Array::create(3);
				auto colors = vsg::ubvec4Array::create(3);
				for (unsigned k = 0; k < 3; ++k)
				{
					(*colors)[k] = triColor;
				}

				if (auto node = buildGeometry(m_triangleShaderSet, m_sharedObjects, verts, colors))
				{
					group->addChild(node);
					m_labelLinks.push_back(LabelLink{label, verts, 3});
				}
			}
		}

		// ---- the ROI rectangles, drawn as dashed line loops ----
		for (auto* roi : roiLabels)
		{
			const auto& r = roi->roi();

			const float x0 = r[0], y0 = r[1], x1 = r[2], y1 = r[3];

			PointList   pts;
			constexpr int dashesPerEdge = 10;

			auto addEdge = [&pts](float ax, float ay, float bx, float by)
			{
				for (int k = 0; k < dashesPerEdge; ++k)
				{
					if (k % 2)
					{
						// every other dash is skipped (GL_LINE_STIPPLE, 0xAAAA)
						continue;
					}

					const float t0 = static_cast<float>(k) / dashesPerEdge;
					const float t1 = static_cast<float>(k + 1) / dashesPerEdge;

					pts.push_back(vsg::vec3(ax + (bx - ax) * t0, ay + (by - ay) * t0, 0.0f));
					pts.push_back(vsg::vec3(ax + (bx - ax) * t1, ay + (by - ay) * t1, 0.0f));
				}
			};

			addEdge(x0, y0, x1, y0);
			addEdge(x1, y0, x1, y1);
			addEdge(x1, y1, x0, y1);
			addEdge(x0, y1, x0, y0);

			if (pts.empty())
			{
				continue;
			}

			auto verts  = vsg::vec3Array::create(pts.size());
			auto colors = vsg::ubvec4Array::create(pts.size());
			const vsg::ubvec4 c = toColor(roi->isSelected() ? ccColor::Rgba(255, 0, 0, 255) : labelColor);

			for (std::size_t i = 0; i < pts.size(); ++i)
			{
				(*verts)[i]  = pts[i];
				(*colors)[i] = c;
			}

			if (auto node = buildGeometry(m_lineShaderSet, m_sharedObjects, verts, colors))
			{
				group->addChild(node);
			}
		}

		m_labelsNode = group;
		m_root->addChild(m_labelsNode);
		m_labelsMounted = true;
		rebuilt         = true;
	}

	// ------------------------------------------------------------------
	// move every anchor to the projection of its 3D point
	// ------------------------------------------------------------------
	for (std::size_t i = 0; i < m_2DLabels.size() && i < m_2DLabelTransforms.size(); ++i)
	{
		// a cc2DLabel can hold several picked points; the anchor follows the
		// first one (CC displays one marker per point)
		double ox = 0.0;
		double oy = 0.0;
		bool   ok = false;

		if (m_2DLabels[i]->size() > 0)
		{
			const CCVector3 P = m_2DLabels[i]->getPickedPoint(0).getPointPosition();

			ok = projectToOverlay(projectionMatrix, viewMatrix, vsg::dvec3(P.x, P.y, P.z), width, height, ox, oy);
		}

		if (ok)
		{
			m_2DLabelTransforms[i]->matrix = vsg::translate(ox, oy, 0.0);
		}
		else
		{
			// behind the camera (or no point): park it far outside the viewport
			m_2DLabelTransforms[i]->matrix = vsg::translate(1.0e6, 1.0e6, 0.0);
		}
	}

	// ---- and the 3D markers ----
	for (std::size_t i = 0; i < m_markerTransforms.size() && i < m_markerLabels.size(); ++i)
	{
		const cc2DLabel* label = m_markerLabels[i];
		const unsigned   p     = (i < m_markerPointIndex.size() ? m_markerPointIndex[i] : 0);

		double ox = 0.0;
		double oy = 0.0;
		bool   ok = false;

		if (p < label->size())
		{
			const CCVector3 P = label->getPickedPoint(p).getPointPosition();
			ok = projectToOverlay(projectionMatrix, viewMatrix, vsg::dvec3(P.x, P.y, P.z), width, height, ox, oy);
		}

		const vsg::dmat4 markerScale = vsg::scale(labelMarkerRadiusPx());

		if (ok)
		{
			m_markerTransforms[i]->matrix = vsg::translate(ox, oy, 0.0) * markerScale;
		}
		else
		{
			m_markerTransforms[i]->matrix = vsg::translate(1.0e6, 1.0e6, 0.0) * markerScale;
		}
	}

	// ---- the connecting lines / triangles: the vertices are rewritten in
	// place, then the array is marked dirty so that VSG uploads it again.
	// This is much cheaper than rebuilding the geometry (no recompilation).
	for (auto& link : m_labelLinks)
	{
		if (!link.verts || !link.label)
		{
			continue;
		}

		for (unsigned k = 0; k < link.count; ++k)
		{
			double ox = 1.0e6;
			double oy = 1.0e6;

			if (k < link.label->size())
			{
				const CCVector3 P = link.label->getPickedPoint(k).getPointPosition();

				double px = 0.0;
				double py = 0.0;
				if (projectToOverlay(projectionMatrix, viewMatrix, vsg::dvec3(P.x, P.y, P.z), width, height, px, py))
				{
					ox = px;
					oy = py;
				}
			}

			(*link.verts)[k].set(static_cast<float>(ox), static_cast<float>(oy), 0.0f);
		}

		link.verts->dirty();
	}

	return rebuilt;
}
