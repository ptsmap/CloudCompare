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
#include "vsg/ccVSGCameraAdapter.h"
#include "vsg/ccVSGCameraManipulator.h"
#include "ccVSGWindowSignalEmitter.h"
#include "vsg/ccVSGOverlayBuilder.h"
#include "vsg/ccVSGSceneBuilder.h"

// qCC_renderCore
#include <ccViewInterface.h>

// qCC_db
#include <ccGenericGLDisplay.h>
#include <ccViewportParameters.h>

// VSG
#include <vsg/app/Camera.h>
#include <vsg/app/CommandGraph.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/View.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/nodes/Group.h>

// vsgQt
#include <vsgQt/Viewer.h>
#include <vsgQt/Window.h>

// Qt
#include <QImage>
#include <QSize>
#include <QString>

// system
#include <cstdint>
#include <vector>

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

	//! Device pixel ratio (1.0 standard, 2.0 on Retina)
	/** vsgQt already sizes the swapchain in device pixels, so the overlay
	    coordinate system stays logical; this is only forwarded to the overlay
	    builder for the glyph atlas resolution. **/
	float devicePixelRatio() const
	{
		return m_window ? static_cast<float>(m_window->devicePixelRatio()) : 1.0f;
	}

	// ----------------------------------------------------------------------
	// View control (same vocabulary as ccGenericGLDisplay)
	// ----------------------------------------------------------------------

	//! Returns the screen size
	QSize getScreenSize() const override = 0;

	//! Redraws display immediately
	void redraw(bool only2D = false, bool resetLOD = true) override = 0;

	//! Flags display as 'to be refreshed'
	void toBeRefreshed() override = 0;

	//! Redraws display only if flagged as 'to be refreshed'
	void refresh(bool only2D = false) override = 0;

	//! Invalidates current viewport setup
	void invalidateViewport() override = 0;

	//! Invalidates the 3D layer
	void deprecate3DLayer() override = 0;

	//! Warns the display that the entity is about to be removed
	void aboutToBeRemoved(ccDrawableObject* obj) override;

	//! Applies a 1:1 global zoom (mirrors ccGLWindowInterface::zoomGlobal)
	void zoomGlobal() override;

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

  // ----------------------------------------------------------------------
  // Camera API (mirrors ccGLWindowInterface)
  // ----------------------------------------------------------------------

  //! Returns the viewport parameters (modifiable)
  ccViewportParameters& viewportParameters()
  {
  	return m_viewportParams;
  }

  //! Returns the current view matrix (world -> camera)
  vsg::dmat4 viewMatrix() const;

  //! Returns the current projection matrix
  /** \warning this is a **Vulkan reverse depth** matrix: the near plane maps
      to NDC z = 1 and the far plane to NDC z = 0 (OpenGL uses -1 / +1).
   **/
  vsg::dmat4 projectionMatrix() const;

  //! Rotates the base view matrix (as ccGLWindowInterface::rotateBaseViewMat)
  void rotateBaseViewMat(const ccGLMatrixd& rotMat);

  //! Displaces the camera (as ccGLWindowInterface::moveCamera)
  void moveCamera(const CCVector3d& v);

  //! Sets the focal distance (as ccGLWindowInterface::setFocalDistance)
  void setFocalDistance(double focalDistance);

  //! Sets the camera center
  void setCameraPos(const CCVector3d& P);

  // ----------------------------------------------------------------------
  // Picking
  // ----------------------------------------------------------------------

  //! Returns the camera parameters in the form expected by the CloudCompare
  //! (= OpenGL) picking helpers
  /** The projection matrix is converted back from the Vulkan reverse depth
      convention (see vulkanToGLProjection()).
   **/
  void getGLCameraParameters(ccGLCameraParameters& params) const;

  //! Renders the view to an image (offscreen)
  /** The scene is rendered a second time into an offscreen framebuffer, which
      is then copied back to CPU memory. The on screen rendering is restored
      afterwards.

      \param zoomFactor resolution multiplier (2.0 = twice the screen size)
      \return the rendered image, or a null image on failure
   **/
  QImage renderToImage(float zoomFactor         = 1.0f,
                       bool  dontScaleFeatures  = false,
                       bool  renderOverlayItems = false,
                       bool  silent             = false);

  //! Performs a CPU based picking at the given (window) position
  /** Reuses the historical CloudCompare routines
      (ccGenericPointCloud::pointPicking / ccGenericMesh::trianglePicking),
      which guarantees the exact same results as the OpenGL backend. The VSG
      scene graph is not involved at all: it is only used for rendering.

      \param x horizontal position (window coordinates, origin = top left)
      \param y vertical position (window coordinates, origin = top left)
   **/
  void doPicking(int x, int y);

  // ----------------------------------------------------------------------
  // Signals
  // ----------------------------------------------------------------------

  ccVSGWindowSignalEmitter* signalEmitter() override
  {
  	return m_signalEmitter;
  }

  protected:
  //! Updates the VSG camera from the (backend agnostic) viewport parameters
  /** Computes:
      - the view matrix from ccViewportParameters::computeViewMatrix()
      - the projection matrix with the CloudCompare near/far heuristics, built
        with the vsg:: perspective()/orthographic() helpers so that the result
        is a Vulkan (reverse depth, Y flipped) matrix.
   **/
  void updateCamera();

  // ----------------------------------------------------------------------
  // Entity picking (M6.1 / M6.2)
  // ----------------------------------------------------------------------

  //! Renders the entity IDs of the whole view into an offscreen R32_UINT
  //! attachment and reads them back on the CPU
  /** The scene is rendered a second time (see renderToImage(), which does the
      same thing for the colors) using the **picking** scene graph built by
      ccVSGSceneBuilder: every entity writes its CloudCompare unique ID, so
      'ids' can be handed to the picking hub as-is.

      \param ids    output: one ID per pixel (0 = nothing), row 0 = top
      \param width  output: width of the returned image
      \param height output: height of the returned image
      \return false when the pass could not be run
   **/
  bool renderIdPass(std::vector<uint32_t>& ids, uint32_t& width, uint32_t& height);

  //! Answers the entity based picking modes (ENTITY_PICKING, ENTITY_RECT_PICKING
  //! and FAST_PICKING) by rendering the ID pass and emitting the same signals
  //! as the OpenGL backend
  /** \param x horizontal position (window coordinates, origin = top left)
      \param y vertical position (window coordinates, origin = top left)
   **/
  void doEntityPicking(int x, int y);

  // VSG objects
  vsg::ref_ptr<vsgQt::Viewer>          m_viewer;
  vsgQt::Window*                       m_window = nullptr; // owned by Qt (QWindow)
  //! Command graph used for the on screen rendering
  /** Kept so that renderToImage() can temporarily replace it (and restore it).
   **/
  vsg::ref_ptr<vsg::CommandGraph>      m_commandGraph;
  vsg::ref_ptr<vsg::Camera>            m_camera;
  vsg::ref_ptr<ccVSGViewMatrix>        m_viewMatrix;
  vsg::ref_ptr<ccVSGProjectionMatrix>  m_projectionMatrix;
  vsg::ref_ptr<ccVSGCameraManipulator> m_manipulator;
  vsg::ref_ptr<vsg::Group>             m_sceneRoot;
  bool                                 m_initialized = false;

  // ----------------------------------------------------------------------
  // 2D overlay (M5)
  // ----------------------------------------------------------------------

  //! Second view, sharing the RenderGraph of the 3D scene
  /** It is therefore drawn after the 3D image, within the same render pass. **/
  vsg::ref_ptr<vsg::View>              m_overlayView;
  vsg::ref_ptr<vsg::Camera>            m_overlayCamera;
  vsg::ref_ptr<ccVSGViewMatrix>        m_overlayViewMatrix;
  vsg::ref_ptr<vsg::Orthographic>      m_overlayProjection;

  //! Builds the 2D entities (trihedron, color scale, and later the scale bar...)
  ccVSGOverlayBuilder m_overlayBuilder;

  //! Set when the overlay produced new nodes that have to be compiled
  bool m_overlayNeedsCompile = false;

  //! Keeps the VSG scene graph in sync with the ccHObject tree
  ccVSGSceneBuilder m_sceneBuilder;

  //! Qt signal emitter (the interface itself is not a QObject)
  ccVSGWindowSignalEmitter* m_signalEmitter = nullptr;

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
