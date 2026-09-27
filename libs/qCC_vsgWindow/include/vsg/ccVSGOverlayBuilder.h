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
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/text/Font.h>
#include <vsg/utils/ShaderSet.h>

class ccImage;
class ccScalarField;

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
	bool updateColorScale(const ccScalarField* sf, int width, int height);

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
	bool updateImage(const ccImage* image, int width, int height);

  private:
	//! Creates the X/Y/Z direction axes (built once, then only its matrix changes)
	void createTrihedron();

	//! Creates the X/Y/Z labels of the trihedron
	void createTrihedronLabels();

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

	//! Textured shader set, used by the image overlay
	vsg::ref_ptr<vsg::ShaderSet> m_texturedShaderSet;

	vsg::ref_ptr<vsg::ShaderSet>     m_lineShaderSet;
	vsg::ref_ptr<vsg::ShaderSet>     m_triangleShaderSet;
	vsg::ref_ptr<vsg::SharedObjects> m_sharedObjects;
};
