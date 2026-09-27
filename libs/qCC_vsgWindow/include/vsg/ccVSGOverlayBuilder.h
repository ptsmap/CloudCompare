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
#include <vsg/core/Array.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/text/Font.h>
#include <vsg/utils/ShaderSet.h>

class cc2DLabel;
class cc2DViewportLabel;
class ccHObject;
class ccImage;
class ccScalarField;
class ccViewportParameters;

// system
#include <cstdint>
#include <vector>

namespace vsg
{
	class SharedObjects;
}

//! Builds the 2D overlay entities (M5)
/** The overlay is rendered by a second vsg::View that lives in the very same
    RenderGraph as the 3D scene: it is therefore drawn on top of the 3D image
    within the same render pass (no extra clear), with the depth test disabled.

    The 2D coordinate system mirrors the one the OpenGL backend uses for its
    foreground entities (ccGLWindowInterface::setStandardOrthoCenter()): an
    orthographic projection centered on the viewport
    (-halfW..halfW, -halfH..halfH), Y axis pointing up, unit = 1 pixel.

    The geometry of each entity is built once; the per frame updates (camera
    orientation, viewport size) are applied through the node matrices, which
    does not require any recompilation.
**/
class ccVSGOverlayBuilder
{
  public:
	ccVSGOverlayBuilder();

	//! Returns the root of the overlay scene graph
	vsg::ref_ptr<vsg::Group> overlayRoot() const
	{
		return m_root;
	}

	//! Updates the overlay for the current viewport and camera
	/** \param width,height  viewport size in pixels
	    \param viewMatrix    current camera view matrix
	    \param showTrihedron whether the direction axes should be displayed
	 **/
	void update(int width, int height, const vsg::dmat4& viewMatrix, bool showTrihedron = true);

	//! Updates the scalar field color scale (M5.5)
	/** Mirrors ccRenderingTools::DrawColorRamp(): a vertical gradient bar on
	    the right hand side, with the scalar field name on top and the extreme
	    values as labels. The gradient is baked into per-vertex colors, so no
	    texture is needed.

	    The whole group is rebuilt (and not merely moved) whenever the scalar
	    field or the viewport changes, which happens rarely.

	    \return true when the group has been rebuilt (the new nodes then have
	            to be compiled by the caller)
	 **/
	bool updateColorScale(const ccScalarField* sf, int width, int height, float renderZoom = 1.0f);

	//! Sets the device pixel ratio (1.0 on standard displays, 2.0 on Retina)
	/** The overlay coordinate system is logical (the device-resolution VSG
	    surface stretches it), so the layout already matches GL. The only thing
	    that benefits from the DPR is the **glyph atlas resolution**: rasterizing
	    the font at `32 * dpr` keeps the text crisp on HD screens. **/
	void setDevicePixelRatio(float dpr);

	//! Updates the scale bar (M5.4)
	/** Mirrors ccGLWindowInterface::drawScale(): a horizontal bar with a tick
	    at each end and the equivalent width as label, bottom left of the
	    viewport. Only meaningful in **orthographic** mode (in perspective mode
	    a screen distance has no constant world equivalent).

	    \param show       false in perspective mode (or when the scale is off)
	    \param pixelSize  size of one pixel, in world units
	    \return true when the group has been rebuilt
	 **/
	bool updateScaleBar(bool show, double pixelSize, int width, int height);

	//! Updates the 2D image overlay (M5.6)
	/** Mirrors ccImage::drawMeOnly(): the image is drawn as a textured quad
	    centred on the viewport, scaled to fit (ccImage::computeDisplayedSize()),
	    with the global alpha of the entity applied.
	 **/
	bool updateImages(const std::vector<const ccImage*>& images, int width, int height);

	//! Updates the 2D labels (M5.3)
	/** Handles the two flavours of CloudCompare 2D labels:
	    - `cc2DLabel`: the name is displayed next to the projection of its 3D
	      point, with a short leader line between the point and the text
	      (see cc2DLabel::drawMeOnly2D())
	    - `cc2DViewportLabel`: the ROI rectangle, drawn as a dashed line loop
	      (see cc2DViewportLabel::drawMeOnly()). The ROI is only displayed
	      when the current viewport matches the one it was created with: it is
	      then rescaled and shifted so that it keeps covering the same part of
	      the **3D scene** (and not of the screen) when the camera moves.

	    \param viewMatrix,projectionMatrix the current camera matrices (used to
	           project the 3D anchor of each cc2DLabel)
	    \param viewportParams              the current viewport parameters
	           (compared against the ones stored in each cc2DViewportLabel)
	    \param renderZoom                  display zoom factor (1.0 = normal)
	 **/
	bool updateLabels(ccHObject*                   root,
	                  const vsg::dmat4&           viewMatrix,
	                  const vsg::dmat4&           projectionMatrix,
	                  const ccViewportParameters& viewportParams,
	                  int                         width,
	                  int                         height,
	                  float                       renderZoom = 1.0f);

  private:
	//! Creates the X/Y/Z direction axes (built once, then only its matrix changes)
	void createTrihedron();

	//! Creates the X/Y/Z labels of the trihedron
	void createTrihedronLabels();

	//! Builds the textured quad of a single 2D image overlay (M5.6)
	vsg::ref_ptr<vsg::Node> createImageQuad(const ccImage* image, int width, int height);

	//! Builds (and caches) the sphere used as the 3D marker of a cc2DLabel
	/** The shading is baked into the vertex colors, so the unlit flat shader is
	    enough to make it read as a sphere. **/
	vsg::ref_ptr<vsg::Node> createLabelMarker(const ccColor::Rgba& color);

	//! Returns the font used by the labels, (re)building it when needed
	/** The label text may contain characters outside the pre-baked ASCII range
	    (CJK, accents...). Such a font cannot be pre-baked - the CJK range alone
	    holds tens of thousands of glyphs - so the atlas is built from the code
	    points the labels actually use. The atlas is only rebuilt when the set
	    of characters grows, which in practice happens once. **/
	vsg::ref_ptr<vsg::Font> ensureLabelFont(const std::vector<uint32_t>& needed);

	//! A polyline / triangle whose vertices are refreshed every frame
	/** The vertex data is updated in place and marked dirty (vsg::Data::dirty()),
	    which is far cheaper than rebuilding the geometry (that would trigger a
	    full recompilation every frame). **/
	struct LabelLink
	{
		const cc2DLabel*            label = nullptr;
		vsg::ref_ptr<vsg::vec3Array> verts;
		unsigned                    count = 0; //!< vertices actually drawn
	};

	//! Builds a text node, wrapped in a transform so that it can be moved cheaply
	vsg::ref_ptr<vsg::Node> createLabel(const char* text, const ccColor::Rgba& color);

	vsg::ref_ptr<vsg::Group>           m_root;
	vsg::ref_ptr<vsg::MatrixTransform> m_trihedron;
	//! Whether the trihedron is currently mounted on the overlay root
	bool                               m_trihedronMounted = false;

	//! Glyph atlas used by the overlay text (M5.2)
	vsg::ref_ptr<vsg::Font> m_font;
	//! X / Y / Z axis labels, each movable through its own transform
	vsg::ref_ptr<vsg::MatrixTransform> m_axisLabels[3];

	//! Scalar field color scale (M5.5)
	vsg::ref_ptr<vsg::Node> m_colorScale;
	quint64                 m_colorScaleSignature = 0;
	bool                    m_colorScaleMounted   = false;

	//! Scale bar (M5.4)
	vsg::ref_ptr<vsg::Node> m_scaleBar;
	quint64                 m_scaleBarSignature = 0;
	bool                    m_scaleBarMounted   = false;

	//! 2D image overlay (M5.6)
	vsg::ref_ptr<vsg::Node> m_imageNode;
	quint64                 m_imageSignature = 0;
	bool                    m_imageMounted   = false;

	//! 2D labels (M5.3)
	vsg::ref_ptr<vsg::Node> m_labelsNode;
	quint64                 m_labelsSignature = 0;
	bool                    m_labelsMounted   = false;

	//! One entry per cc2DLabel: the anchor and the transform that positions it
	/** The geometry of a label is expressed *relative* to its anchor, so that
	    moving the camera only requires updating the transform matrix (no
	    rebuild, and therefore no recompilation). **/
	std::vector<const cc2DLabel*>                  m_2DLabels;
	std::vector<vsg::ref_ptr<vsg::MatrixTransform>> m_2DLabelTransforms;

	//! 3D markers (one sphere per picked point of each cc2DLabel)
	std::vector<const cc2DLabel*>                   m_markerLabels;
	std::vector<unsigned>                           m_markerPointIndex;
	std::vector<vsg::ref_ptr<vsg::MatrixTransform>> m_markerTransforms;

	//! Cached unit spheres (plain / selected) shared by all the markers
	vsg::ref_ptr<vsg::Node> m_markerSphere;
	vsg::ref_ptr<vsg::Node> m_markerSphereSelected;

	//! cc2DViewportLabel ROIs (M5.3)
	/** The rectangle is built once, in the (fixed) ROI coordinates, and is
	    moved and scaled by \c m_roiTransforms every frame. The title is a
	    separate node: it must be translated only, so that its size does not
	    follow the zoom compensation. **/
	std::vector<const cc2DViewportLabel*>           m_roiLabels;
	std::vector<vsg::ref_ptr<vsg::MatrixTransform>> m_roiTransforms;
	std::vector<vsg::ref_ptr<vsg::MatrixTransform>> m_roiTitleTransforms;

	//! Label font (ASCII + the code points used by the labels) and its charset
	vsg::ref_ptr<vsg::Font> m_labelFont;
	std::vector<uint32_t>   m_labelFontChars;

	//! Device pixel ratio (1.0 standard, 2.0 Retina). See setDevicePixelRatio().
	float m_devicePixelRatio = 1.0f;

	//! Segments / triangles whose vertices are refreshed every frame
	std::vector<LabelLink> m_labelLinks;
	//! LINE_STRIP shader set: the connecting line of a multi-point label
	vsg::ref_ptr<vsg::ShaderSet> m_lineStripShaderSet;

	//! Textured shader set, used by the image overlay
	vsg::ref_ptr<vsg::ShaderSet> m_texturedShaderSet;

	vsg::ref_ptr<vsg::ShaderSet>     m_lineShaderSet;
	vsg::ref_ptr<vsg::ShaderSet>     m_triangleShaderSet;
	vsg::ref_ptr<vsg::SharedObjects> m_sharedObjects;
};
