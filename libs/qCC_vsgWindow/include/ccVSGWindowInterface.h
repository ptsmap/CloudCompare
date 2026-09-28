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
#include <functional>
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

  //! Sets the pivot point (as ccGLWindowInterface::setPivotPoint)
  /** \param P                    the new rotation center
      \param autoUpdateCameraPos  move the camera so that the point of view
                                  does not change (used by the double click)
      \param verbose              log the new rotation center
   **/
  void setPivotPoint(const CCVector3d& P, bool autoUpdateCameraPos = false, bool verbose = false);

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

  //! Queues a picking request (see scheduleDeferredAction)
  /** Same as doPicking(), but run once the VSG event handling is over: the
      picking needs an extra frame, which must not be rendered from within an
      event handler.
   **/
  void requestPicking(int x, int y);

  // ----------------------------------------------------------------------
  // Depth unprojection (M6.4)
  // ----------------------------------------------------------------------

  //! Unprojects a (window) position in 3D using the depth buffer
  /** The scene is rendered a second time into an offscreen framebuffer whose
      **depth** attachment is read back on the CPU (see renderDepthPass()), and
      the depth of the pixel under the cursor is unprojected with the very same
      helper as the OpenGL backend (ccGLCameraParameters::unproject).

      \param x   horizontal position (window coordinates, origin = top left)
      \param y   vertical position (window coordinates, origin = top left)
      \param P3D output: the 3D position of the point displayed at (x,y)
      \return false when the depth is undefined (i.e. the background) or when
              the offscreen pass could not be run

      \warning The whole view is rendered again: this is fine for a double
      click, but it must not be called on every mouse move.
   **/
  bool getClick3DPos(int x, int y, CCVector3d& P3D);

  //! Handles a mouse double click: sets the pivot point under the cursor
  /** Called by ccVSGCameraManipulator, which detects the double click itself
      (VSG has no such event). Mirrors
      ccGLWindowInterface::processMouseDoubleClickEvent().
   **/
  void processMouseDoubleClick(int x, int y);

  //! Queues a double click request (see scheduleDeferredAction)
  void requestMouseDoubleClick(int x, int y);

  //! Handles a hover (no button) mouse move: shows the 3D point under cursor
  /** Called by ccVSGCameraManipulator for MOVE events with no button pressed
      (VSG has no "hover" concept of its own). When the cursor coordinate
      display is enabled (see showCursorCoordinates()) the depth under the
      cursor is read (deferred, like the picking) and unprojected, then emitted
      through ccVSGWindowSignalEmitter::cursorCoordinates (M6.6).
   **/
  void processMouseMove(int x, int y);

  //! Enables/disables the 3D coordinate display under the mouse cursor
  /** Mirrors ccGLWindowInterface::showCursorCoordinates(). Off by default, so
      the (relatively expensive) offscreen depth read is only run on demand.
   **/
  void showCursorCoordinates(bool state);

  //! Whether the 3D coordinate display under the mouse cursor is enabled
  bool showCursorCoordinates() const;

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

  //! Runs an action once the VSG event handling is over
  /** Both the picking and the double click need an extra (offscreen) frame.
      They are triggered from a VSG event handler, i.e. from
      vsg::Viewer::handleEvents(): rendering there would re-enter the viewer
      (advanceToNextFrame() clears the very event queue that is being
      iterated) and would submit a frame within a frame.
      The Qt based view overrides this with a QTimer::singleShot() so that the
      action runs on the next event loop iteration, between two frames.
   **/
  virtual void scheduleDeferredAction(std::function<void()> action);

  //! Unlinks every entity that points at this view (called before destruction)
  /** The entities keep a raw pointer to their display (see
      ccDrawableObject::m_currentDisplay): as the OpenGL backend does in its
      own destructor, they have to be unlinked before this object is destroyed
      (a dangling display is a virtual call on a half destroyed object, i.e.
      __cxa_pure_virtual / a crash).
   **/
  void unlinkEntitiesFromDisplay();

  // ----------------------------------------------------------------------
  // Offscreen rendering (M6)
  // ----------------------------------------------------------------------

  //! Description of an offscreen render + read back pass
  /** renderToImage(), renderIdPass() and renderDepthPass() only differ by the
      format of the attachments and by the one they read back, so the whole
      Vulkan boilerplate lives in renderOffscreen().
   **/
  struct OffscreenRequest
  {
      //! Scene graph to render (the display root, or the picking one)
      vsg::ref_ptr<vsg::Node> scene;

      //! Format of the color attachment (R8G8B8A8_UNORM or R32_UINT)
      VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

      //! Format of the depth attachment (the one of the on screen window)
      VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

      //! Clear value of the color attachment
      /** \warning VkClearColorValue is a **union**: an integer attachment
          (R32_UINT) must be cleared through its `uint32` members.
       **/
      VkClearValue clearColor{};

      //! Clear value of the depth attachment (0 = far plane: reverse depth)
      float clearDepth = 0.0f;

      uint32_t width  = 0;
      uint32_t height = 0;

      //! Read the depth attachment back instead of the color one
      bool readDepth = false;

      //! Draw the 2D overlay on top of the 3D image (renderToImage only)
      bool withOverlay = false;
  };

  //! Renders a scene graph into an offscreen framebuffer and reads it back
  /** \param request  what to render and which attachment to read back
      \param pixels   output: raw pixels, row 0 = **top** of the image. The
                      size is width * height * (4 for a color attachment,
                      4 for D32_SFLOAT, 2 for D16_UNORM).
      \return false when the pass could not be run
   **/
  bool renderOffscreen(const OffscreenRequest& request, std::vector<uint8_t>& pixels);

  //! Reads the depth buffer of the view back on the CPU (M6.4)
  /** \param depths output: one depth per pixel ([0..1], 1 = near plane)
      \param width  output: width of the returned image
      \param height output: height of the returned image
   **/
  bool renderDepthPass(std::vector<float>& depths, uint32_t& width, uint32_t& height);

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

	// 3D coordinate display under the cursor (M6.6)
	bool m_showCursorCoordinates = false;
	//! coalescing flag: at most one deferred depth read per event loop turn
	bool m_cursorCoordScheduled  = false;
};
