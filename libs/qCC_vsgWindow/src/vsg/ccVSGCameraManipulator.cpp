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

// system
#include <algorithm>
#include <cmath>

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

void ccVSGCameraManipulator::apply(vsg::ButtonPressEvent& event)
{
	m_lastX      = event.x;
	m_lastY      = event.y;
	m_mode       = modeForMask(event.mask);
	m_mouseMoved = false;

	if (m_mode == Mode::Rotate)
	{
		m_lastOrientation = convertMousePositionToOrientation(event.x, event.y);
	}
}

void ccVSGCameraManipulator::apply(vsg::ButtonReleaseEvent& /*event*/)
{
	m_mode = Mode::None;
}

void ccVSGCameraManipulator::apply(vsg::MoveEvent& event)
{
	if (!m_view || m_mode == Mode::None)
	{
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
		m_view->rotateBaseViewMat(rotMat);

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
