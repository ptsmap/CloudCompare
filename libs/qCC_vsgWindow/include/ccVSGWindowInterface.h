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
#include "qCC_vsgWindow.h"

// qCC_renderCore
#include <ccViewInterface.h>

// qCC_db
#include <ccViewportParameters.h>

// VSG
#include <vsg/app/Camera.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

// vsgQt
#include <vsgQt/Viewer.h>

// Qt
#include <QSize>
#include <QString>

class ccDrawableObject;
class ccHObject;

namespace vsgQt
{
	class Window;
}

//! VulkanSceneGraph 3D view interface
/** Backend agnostic logic of a 3D view rendered with VulkanSceneGraph.
	It derives from ccViewInterface (like ccGenericGLDisplay does for the
	OpenGL backend) so that both backends share a single root.

	The Qt specific part (widget creation, event forwarding) lives in
	ccVSGWindow.

	See doc/VSG_Rendering_Migration_Plan.md
**/
class CCVSGWINDOW_LIB_API ccVSGWindowInterface : public ccViewInterface
{
  public:
	//! Default constructor
	ccVSGWindowInterface();

	//! Destructor
	~ccVSGWindowInterface() override;

	// ----------------------------------------------------------------------
	// Initialization
	// ----------------------------------------------------------------------

	//! Initializes the VSG viewer / render graph for an already created window
	/** \param viewer    the (Qt driven) VSG viewer
	    \param vsgWindow the Qt window wrapping the vsg::Window
	    \return success
	**/
	bool initializeViewer(vsg::ref_ptr<vsgQt::Viewer> viewer, vsgQt::Window* vsgWindow);

	//! Returns whether the VSG viewer has been successfully initialized
	bool isInitialized() const
	{
		return m_initialized;
	}

	// ----------------------------------------------------------------------
	// ccViewInterface
	// ----------------------------------------------------------------------

	QString backendName() const override
	{
		return QStringLiteral("VSG");
	}

	const ccRenderCapabilities& renderCapabilities() const override
	{
		return m_renderCapabilities;
	}

	void        setSceneDB(ccHObject* root) override;
	ccHObject*  getSceneDB() override
	{
		return m_globalDBRoot;
	}
	ccHObject*  getOwnDB() override
	{
		return m_winDBRoot;
	}

	void         setPickingMode(PICKING_MODE mode = DEFAULT_PICKING, Qt::CursorShape defaultCursorShape = Qt::ArrowCursor) override;
	PICKING_MODE getPickingMode() const override
	{
		return m_pickingMode;
	}

	void              setInteractionMode(INTERACTION_FLAGS flags) override;
	INTERACTION_FLAGS getInteractionMode() const override
	{
		return m_interactionFlags;
	}

	const ccViewportParameters& getViewportParameters() const override
	{
		return m_viewportParams;
	}
	void setViewportParameters(const ccViewportParameters& params) override;

	// ----------------------------------------------------------------------
	// View control (same vocabulary as ccGenericGLDisplay)
	// ----------------------------------------------------------------------

	//! Returns the screen size
	virtual QSize getScreenSize() const = 0;

	//! Redraws display immediately
	virtual void redraw(bool only2D = false, bool resetLOD = true) = 0;

	//! Flags display as 'to be refreshed'
	virtual void toBeRefreshed() = 0;

	//! Redraws display only if flagged as 'to be refreshed'
	virtual void refresh(bool only2D = false) = 0;

	//! Invalidates current viewport setup
	virtual void invalidateViewport() = 0;

	//! Invalidates the 3D layer
	virtual void deprecate3DLayer() = 0;

	//! Warns the display that the entity is about to be removed
	virtual void aboutToBeRemoved(ccDrawableObject* obj);

	// ----------------------------------------------------------------------
	// VSG specifics
	// ----------------------------------------------------------------------

	vsg::ref_ptr<vsgQt::Viewer> viewer() const
	{
		return m_viewer;
	}

	vsg::ref_ptr<vsg::Camera> camera() const
	{
		return m_camera;
	}

	vsg::ref_ptr<vsg::Group> sceneRoot() const
	{
		return m_sceneRoot;
	}

  protected:
	//! Updates the VSG camera from the (backend agnostic) viewport parameters
	/** TODO(M2): full ccViewportParameters -> vsg::Camera mapping
	    (pivot point, object/viewer centered view, focal distance, near/far).
	 **/
	void updateCamera();

	// VSG objects
	vsg::ref_ptr<vsgQt::Viewer>    m_viewer;
	vsgQt::Window*                 m_window = nullptr; // owned by Qt (QWindow)
	vsg::ref_ptr<vsg::Camera>      m_camera;
	vsg::ref_ptr<vsg::LookAt>      m_lookAt;
	vsg::ref_ptr<vsg::Perspective> m_perspective;
	vsg::ref_ptr<vsg::Group>       m_sceneRoot;
	bool                           m_initialized = false;

	// scene
	ccHObject* m_globalDBRoot = nullptr;
	ccHObject* m_winDBRoot    = nullptr;

	// view state
	ccViewportParameters m_viewportParams;
	ccRenderCapabilities m_renderCapabilities;

	PICKING_MODE      m_pickingMode      = NO_PICKING;
	bool              m_pickingModeLocked = false;
	INTERACTION_FLAGS m_interactionFlags   = MODE_TRANSFORM_CAMERA;
	bool              m_shouldBeRefreshed  = false;
};
