#pragma once

// ##########################################################################
// #                                                                        #
// #                              CLOUDCOMPARE                              #
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
// #          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
// #                                                                        #
// ##########################################################################

// local
#include "qCC_glWindow.h"

// qCC_db
#include <ccHObject.h>
#include <ccViewSignalEmitter.h>

// Qt
#include <QObject>
#include <QStringList>

// system
#include <unordered_set>

class ccGLWindowInterface;

//! ccGLWindow Signal emitter
/** Derives from ccViewSignalEmitter so that the backend agnostic code can
    connect to 'itemPicked' / 'aboutToClose' without knowing the backend.
 **/
class CCGLWINDOW_LIB_API ccGLWindowSignalEmitter : public ccViewSignalEmitter
{
	Q_OBJECT

  public:
	//! Default constructor
	ccGLWindowSignalEmitter(ccGLWindowInterface* associatedWindow, QObject* parent);

	//! Returns the associated window
	inline ccGLWindowInterface* getAssociatedWindow()
	{
		return m_associatedWindow;
	}

  Q_SIGNALS:

	// NOTE: entitySelectionChanged() / entitiesSelectionChanged() /
	// itemPickedFast() (M6.1) and pivotPointChanged() (M6.4) now live in
	// ccViewSignalEmitter (the backend agnostic base class) so that a VSG view
	// can emit them too.

	//! Signal emitted when fast picking is finished (FAST_PICKING mode only)
	void fastPickingFinished();

	/*** Camera link mode (interactive modifications of the view/camera are echoed to other windows) ***/

	//! Signal emitted when the window 'model view' matrix is interactively changed
	void viewMatRotated(const ccGLMatrixd& rotMat);
	//! Signal emitted when the mouse wheel is rotated
	void mouseWheelRotated(float wheelDelta_deg);

	//! Signal emitted when the perspective state changes (see setPerspectiveState)
	void perspectiveStateChanged();

	//! Signal emitted when the window 'base view' matrix is changed
	void baseViewMatChanged(const ccGLMatrixd& newViewMat);

	//! Signal emitted when the f.o.v. changes
	void fovChanged(float fov);

	//! Signal emitted when the near clipping depth has been changed
	void nearClippingDepthChanged(double depth);

	//! Signal emitted when the far clipping depth has been changed
	void farClippingDepthChanged(double depth);

	//! Signal emitted when the clipping planes enability has been changed
	void clippingPlanesToggled(bool state);

	//! Signal emitted when the camera position is changed
	void cameraPosChanged(const CCVector3d&);

	// NOTE: translation() / rotation() now live in ccViewSignalEmitter (the
	// backend agnostic base class) so that the transformation tool can be
	// driven by a VSG view as well (M8 / D.18.5).

	// NOTE: leftButtonClicked() / rightButtonClicked() / middleButtonClicked() /
	// mouseMoved() / buttonReleased() (M6.6) now live in ccViewSignalEmitter
	// (the backend agnostic base class) so that the interactive tools, which
	// derive from ccOverlayDialog, can be driven by a VSG view as well.

	//! Signal emitted during 3D pass of OpenGL display process
	/** Any object connected to this slot can draw additional stuff in 3D.
	    Depth buffering, lights and shaders are enabled by default.
	**/
	void drawing3D();

	//! Signal emitted when files are dropped on the window
	void filesDropped(const QStringList& filenames);

	//! (newLabel now lives in the backend agnostic ccViewSignalEmitter - M6.5)

	//! Signal emitted when the exclusive fullscreen is toggled
	void exclusiveFullScreenToggled(bool exclusive);

	// (aboutToClose now lives in the backend agnostic ccViewSignalEmitter, so
	// that ccOverlayDialog can connect to any backend - M6.6)

  protected:
	ccGLWindowInterface* m_associatedWindow;
};
