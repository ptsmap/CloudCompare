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
#include <vsg/ccVSGCameraManipulator.h>
#include <ccVSGWindowInterface.h>

// qCC_db
#include <ccGLMatrix.h>
#include <ccViewportParameters.h>

// VSG
#include <vsg/maths/transform.h>

// Qt
#include <QSize>
#include <QtCore/qnamespace.h>

// system
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace
{
	//! Converts a VSG button mask to the Qt one (M6.6: mouseMoved())
	/** VSG: BUTTON_MASK_1 = left, _2 = middle, _3 = right. **/
	Qt::MouseButtons toQtMouseButtons(vsg::ButtonMask mask)
	{
		Qt::MouseButtons buttons = Qt::NoButton;

		if (mask & vsg::BUTTON_MASK_1)
		{
			buttons |= Qt::LeftButton;
		}
		if (mask & vsg::BUTTON_MASK_2)
		{
			buttons |= Qt::MiddleButton;
		}
		if (mask & vsg::BUTTON_MASK_3)
		{
			buttons |= Qt::RightButton;
		}

		return buttons;
	}
} // namespace

ccVSGCameraManipulator::ccVSGCameraManipulator(ccVSGWindowInterface* view)
    : m_view(view)
{
}

ccVSGCameraManipulator::Mode ccVSGCameraManipulator::modeForMask(vsg::ButtonMask mask) const
{
	if (mask & rotateButtonMask)
	{
		return Mode::Rotate;
	}
	if (mask & panButtonMask)
	{
		return Mode::Pan;
	}
	if (mask & zoomButtonMask)
	{
		return Mode::Zoom;
	}

	return Mode::None;
}

CCVector3d ccVSGCameraManipulator::convertMousePositionToOrientation(int32_t x, int32_t y)
{
	if (!m_view)
	{
		return CCVector3d(0, 0, 1);
	}

	const QSize screenSize = m_view->getScreenSize();
	const double width     = screenSize.width();
	const double height    = screenSize.height();

	if (width <= 0 || height <= 0)
	{
		return CCVector3d(0, 0, 1);
	}

	const double xc = width / 2.0;
	const double yc = height / 2.0;

	CCVector3d Q2D(xc, yc, 0.0);

	if (m_view->viewportParameters().objectCenteredView)
	{
		// project the current pivot point on screen
		const CCVector3d pivot = m_view->viewportParameters().getPivotPoint();

		vsg::dmat4 projView = m_view->projectionMatrix() * m_view->viewMatrix();
		vsg::dvec4 clip     = projView * vsg::dvec4(pivot.x, pivot.y, pivot.z, 1.0);

		if (clip.w != 0.0)
		{
			const double ndcX = clip.x / clip.w;
			const double ndcY = clip.y / clip.w;

			// Vulkan: NDC y = -1 is the top of the viewport
			Q2D.x = (ndcX + 1.0) * 0.5 * width;
			Q2D.y = (ndcY + 1.0) * 0.5 * height;

			// keep the virtual rotation pivot in the central part of the screen
			Q2D.x = std::min(Q2D.x, 3.0 * width / 4.0);
			Q2D.x = std::max(Q2D.x, width / 4.0);
			Q2D.y = std::min(Q2D.y, 3.0 * height / 4.0);
			Q2D.y = std::max(Q2D.y, height / 4.0);
		}
	}

	// CloudCompare works in a Y-up screen space
	const double yUp    = height - 1 - y;
	const double q2dYUp = height - Q2D.y;

	CCVector3d v(x - Q2D.x, yUp - q2dYUp, 0.0);

	v.x = std::max(std::min(v.x / xc, 1.0), -1.0);
	v.y = std::max(std::min(v.y / yc, 1.0), -1.0);

	// projection on the unit sphere
	const double d2 = v.x * v.x + v.y * v.y;
	if (d2 > 1.0)
	{
		const double d = std::sqrt(d2);
		v.x /= d;
		v.y /= d;
	}
	else
	{
		v.z = std::sqrt(1.0 - d2);
	}

	return v;
}

int32_t ccVSGCameraManipulator::toViewCoord(int32_t c) const
{
	const double dpr = m_view ? m_view->devicePixelRatio() : 1.0;
	if (dpr <= 0.0)
	{
		return c;
	}
	return static_cast<int32_t>(std::lround(static_cast<double>(c) / dpr));
}

void ccVSGCameraManipulator::doPan(int32_t dx, int32_t dy)
{
	if (!m_view)
	{
		return;
	}

	const ccViewportParameters& params = m_view->viewportParameters();

	// displacement vector (in "3D") - see ccGLWindowInterface (panning)
	const double pixSize = params.computePixelSize(m_view->getScreenSize().width(),
	                                               m_view->getScreenSize().height());

	CCVector3d u(static_cast<double>(dx) * pixSize,
	             -static_cast<double>(dy) * pixSize,
	             0.0);

	// M8 / D.18.5: 'transform entities' mode - the displacement is forwarded
	// to the interactive tools (ccGraphicalTransformationTool) instead of
	// moving the camera (see ccGLWindowInterface::processMouseMoveEvent)
	if (m_view->getInteractionMode() & ccViewInterface::INTERACT_TRANSFORM_ENTITIES)
	{
		// apply the inverse view matrix (same as the OpenGL backend)
		params.viewMat.transposed().applyRotation(u);
		Q_EMIT m_view->signalEmitter()->translation(u);
		return;
	}

	if (params.objectCenteredView)
	{
		// inverse displacement in object-based mode
		u = -u;
	}

	m_view->moveCamera(u);
}

void ccVSGCameraManipulator::doZoom(double factor)
{
	if (!m_view)
	{
		return;
	}

	const double focal = m_view->viewportParameters().getFocalDistance();

	// TODO(M2 acceptance): tune the zoom response against the OpenGL backend
	m_view->setFocalDistance(focal * factor);
}

bool ccVSGCameraManipulator::isDoubleClick(const vsg::ButtonPressEvent& event) const
{
	// both the button and the mask are compared: some window adapters leave
	// 'button' unset, and the mask alone would not tell which button was
	// actually pressed
	return event.button == m_lastPressButton
	    && event.mask == m_lastPressMask
	    && (event.time - m_lastPressTime) < doubleClickInterval
	    && std::abs(event.x - m_lastPressX) <= doubleClickTolerance
	    && std::abs(event.y - m_lastPressY) <= doubleClickTolerance;
}

void ccVSGCameraManipulator::apply(vsg::ButtonPressEvent& event)
{
	// VSG has no 'double click' event: the OpenGL backend gets one from Qt and
	// uses it to set the pivot point under the cursor (M6.4)
	m_ignoreNextPicking = false;

	// vsgQt::Window hands us device pixel coordinates: convert them back to
	// logical ones once, here (see toViewCoord())
	event.x = toViewCoord(event.x);
	event.y = toViewCoord(event.y);

	if (isDoubleClick(event))
	{
		// the release that follows must not start a picking
		m_ignoreNextPicking = true;

		if (m_view)
		{
			// not run from here: the double click needs an extra (offscreen)
			// frame, which must not be rendered from within an event handler
			m_view->requestMouseDoubleClick(event.x, event.y);
		}
	}

	m_lastPressTime   = event.time;
	m_lastPressX      = event.x;
	m_lastPressY      = event.y;
	m_lastPressButton = event.button;
	m_lastPressMask   = event.mask;

	m_lastX      = event.x;
	m_lastY      = event.y;
	m_mode       = modeForMask(event.mask);
	m_mouseMoved = false;

	// M6.6: mirror the OpenGL backend's interaction signals, so that the
	// interactive tools (which derive from ccOverlayDialog) can be driven by a
	// VSG view. Same gating as ccGLWindowInterface::processMousePressEvent()
	if (m_view)
	{
		const ccViewInterface::INTERACTION_FLAGS flags = m_view->getInteractionMode();

		if (event.button == 1)
		{
			if (flags & ccViewInterface::INTERACT_SIG_LB_CLICKED)
			{
				Q_EMIT m_view->signalEmitter()->leftButtonClicked(event.x, event.y);
			}
		}
		else if (event.button == 2)
		{
			if (flags & ccViewInterface::INTERACT_SIG_MB_CLICKED)
			{
				Q_EMIT m_view->signalEmitter()->middleButtonClicked(event.x, event.y);
			}
		}
		else if (event.button == 3)
		{
			if (flags & ccViewInterface::INTERACT_SIG_RB_CLICKED)
			{
				Q_EMIT m_view->signalEmitter()->rightButtonClicked(event.x, event.y);
			}
		}
	}

	if (m_mode == Mode::Rotate)
	{
		m_lastOrientation = convertMousePositionToOrientation(event.x, event.y);
	}
}

void ccVSGCameraManipulator::apply(vsg::ButtonReleaseEvent& event)
{
	// vsgQt::Window hands us device pixel coordinates: convert them back to
	// logical ones once, here (see toViewCoord())
	event.x = toViewCoord(event.x);
	event.y = toViewCoord(event.y);

	if (m_ignoreNextPicking)
	{
		// the second click of a double click is not a picking request (the
		// OpenGL backend cancels its deferred picking in the same way)
		m_ignoreNextPicking = false;
		m_mode              = Mode::None;
		return;
	}

	// a 'click' (i.e. a press/release pair without any drag) must trigger the
	// picking process - dragging still controls the camera, as in the OpenGL
	// backend
	if (m_view
	    && !m_mouseMoved
	    && m_view->getPickingMode() != ccViewInterface::NO_PICKING)
	{
		// not run from here: the entity picking needs an extra (offscreen)
		// frame, which must not be rendered from within an event handler
		m_view->requestPicking(event.x, event.y);
	}

	// M6.6: mirror the OpenGL backend (same gating, and not emitted for the
	// release of a double click, which returns above)
	if (m_view && (m_view->getInteractionMode() & ccViewInterface::INTERACT_SIG_BUTTON_RELEASED))
	{
		Q_EMIT m_view->signalEmitter()->buttonReleased();
	}

	m_mode = Mode::None;
}

void ccVSGCameraManipulator::apply(vsg::MoveEvent& event)
{
	if (!m_view)
	{
		return;
	}

	// vsgQt::Window hands us device pixel coordinates: convert them back to
	// logical ones once, here (see toViewCoord())
	event.x = toViewCoord(event.x);
	event.y = toViewCoord(event.y);

	// M6.6: mirror the OpenGL backend, which emits mouseMoved() on every move
	// (hover included), before any button test
	if (m_view->getInteractionMode() & ccViewInterface::INTERACT_SIG_MOUSE_MOVED)
	{
		Q_EMIT m_view->signalEmitter()->mouseMoved(event.x, event.y, toQtMouseButtons(event.mask));
	}

	// hover (no button pressed): feed the cursor coordinate display, but do
	// not touch the camera. The depth read is deferred by the view (it needs
	// an extra offscreen frame), so it is safe to call from here
	if (m_mode == Mode::None)
	{
		m_view->processMouseMove(event.x, event.y);
		m_lastX = event.x;
		m_lastY = event.y;
		return;
	}

	if (!m_mouseMoved)
	{
		// on the first move we recompute the previous orientation
		if (m_mode == Mode::Rotate)
		{
			m_lastOrientation = convertMousePositionToOrientation(m_lastX, m_lastY);
		}
	}

	const int32_t dx = event.x - m_lastX;
	const int32_t dy = event.y - m_lastY;

	switch (m_mode)
	{
	case Mode::Rotate:
	{
		const CCVector3d currentOrientation = convertMousePositionToOrientation(event.x, event.y);

		// unconstrained rotation following the mouse position (as CloudCompare does)
		ccGLMatrixd rotMat = ccGLMatrixd::FromToRotation(m_lastOrientation, currentOrientation);

		// M8 / D.18.5: 'transform entities' mode - the rotation is forwarded to
		// the interactive tools instead of rotating the camera (see
		// ccGLWindowInterface::processMouseMoveEvent)
		if (m_view->getInteractionMode() & ccViewInterface::INTERACT_TRANSFORM_ENTITIES)
		{
			const ccViewportParameters& params = m_view->viewportParameters();
			rotMat                             = params.viewMat.transposed() * rotMat * params.viewMat;
			Q_EMIT m_view->signalEmitter()->rotation(rotMat);
		}
		else
		{
			m_view->rotateBaseViewMat(rotMat);
		}

		m_lastOrientation = currentOrientation;
	}
	break;

	case Mode::Pan:
		doPan(dx, dy);
		break;

	case Mode::Zoom:
		// dragging the mouse down zooms out, up zooms in
		doZoom(1.0 + static_cast<double>(dy) * zoomStep * 0.05);
		break;

	case Mode::None:
	default:
		break;
	}

	m_lastX      = event.x;
	m_lastY      = event.y;
	m_mouseMoved = true;
}

void ccVSGCameraManipulator::apply(vsg::ScrollWheelEvent& event)
{
	if (!m_view)
	{
		return;
	}

	// delta.y > 0 = scroll up = zoom in
	// TODO(M2): Alt = point size, Ctrl = near/far clipping, Shift = FOV
	doZoom(event.delta.y > 0.0 ? (1.0 + zoomStep) : (1.0 - zoomStep));
}
