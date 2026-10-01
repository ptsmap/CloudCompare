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
#include <vsg/core/Inherit.h>
#include <vsg/core/Visitor.h>
#include <vsg/ui/PointerEvent.h>
#include <vsg/ui/ScrollWheelEvent.h>

// CCCoreLib
#include <CCGeom.h>

class ccVSGWindowInterface;

//! Camera manipulator reproducing the CloudCompare mouse semantics
/** CloudCompare does not use the vsg::Trackball model: it rotates a "virtual
    trackball" built by projecting the mouse position on a unit sphere, pans by
    moving the camera center along the screen axes and zooms by changing the
    focal distance. All of these only modify ccViewportParameters - the single
    source of truth - never the vsg::Camera directly.

    Mouse mapping (CloudCompare):
      - left button   : rotate
      - right button  : pan
      - middle button : zoom
      - wheel         : zoom
**/
class CCVSGWINDOW_LIB_API ccVSGCameraManipulator : public vsg::Inherit<vsg::Visitor, ccVSGCameraManipulator>
{
  public:
	explicit ccVSGCameraManipulator(ccVSGWindowInterface* view);

	void apply(vsg::ButtonPressEvent& event) override;
	void apply(vsg::ButtonReleaseEvent& event) override;
	void apply(vsg::MoveEvent& event) override;
	void apply(vsg::ScrollWheelEvent& event) override;

	//! Button mapping (VSG: BUTTON_MASK_1 = left, _2 = middle, _3 = right)
	vsg::ButtonMask rotateButtonMask = vsg::BUTTON_MASK_1;
	vsg::ButtonMask panButtonMask    = vsg::BUTTON_MASK_3;
	vsg::ButtonMask zoomButtonMask   = vsg::BUTTON_MASK_2;

	//! Relative zoom step used by the wheel and the middle button drag
	double zoomStep = 0.1;

	// ----------------------------------------------------------------------
	// Double click (M6.4)
	// ----------------------------------------------------------------------

	//! Maximum delay between two presses for them to be a double click
	vsg::clock::duration doubleClickInterval = std::chrono::milliseconds(300);

	//! Maximum distance (in pixels) between the two presses
	int32_t doubleClickTolerance = 4;

  protected:
	enum class Mode
	{
		None,
		Rotate,
		Pan,
		Zoom
	};

	Mode modeForMask(vsg::ButtonMask mask) const;

	//! Tells whether the press follows the previous one closely enough
	/** VSG has no 'double click' event, while Qt (and therefore the OpenGL
	    backend) has: it is detected here, from the button, the position and
	    the timestamp of the previous press.
	 **/
	bool isDoubleClick(const vsg::ButtonPressEvent& event) const;

	//! Replicates ccGLWindowInterface::convertMousePositionToOrientation()
	CCVector3d convertMousePositionToOrientation(int32_t x, int32_t y);

	//! Converts a VSG (device pixel) coordinate into a view (logical pixel) one
	/** vsgQt::Window scales the Qt coordinates by devicePixelRatio() when it
	    builds the VSG events (see vsgQt/Window.h::convert_coord), while
	    everything in CloudCompare - getScreenSize(), the picking, the 2D
	    overlay, the trackball - works in logical pixels. Not converting them
	    back makes the picking and the rotation wrong by a factor of the DPR on
	    a Retina display (M8 / D.18.5).
	 **/
	int32_t toViewCoord(int32_t c) const;

	void doPan(int32_t dx, int32_t dy);
	void doZoom(double factor);

	ccVSGWindowInterface* m_view = nullptr;

	Mode    m_mode      = Mode::None;
	int32_t m_lastX     = 0;
	int32_t m_lastY     = 0;
	bool    m_mouseMoved = false;

	CCVector3d m_lastOrientation;

	// double click detection (M6.4)
	vsg::clock::time_point m_lastPressTime;
	int32_t                m_lastPressX    = 0;
	int32_t                m_lastPressY    = 0;
	uint32_t               m_lastPressButton = 0;
	vsg::ButtonMask        m_lastPressMask   = vsg::BUTTON_MASK_OFF;

	//! Set when the current press/release pair is the second one of a double
	//! click: the release must then not start a picking
	bool m_ignoreNextPicking = false;
};
