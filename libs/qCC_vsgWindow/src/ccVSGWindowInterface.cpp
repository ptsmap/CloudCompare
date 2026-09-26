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
#include "ccVSGWindowInterface.h"

// qCC_db
#include <ccDrawableObject.h>
#include <ccHObject.h>

// VSG
#include <vsg/all.h>

// vsgQt
#include <vsgQt/Window.h>

ccVSGWindowInterface::ccVSGWindowInterface()
{
	// Conservative defaults: they are refined once the Vulkan device is known
	// (see M0/M3 - point size and wide lines are the two critical features).
	m_renderCapabilities.backendName             = QStringLiteral("VSG");
	m_renderCapabilities.pointSizeSupported      = false;
	m_renderCapabilities.maxPointSize            = 1.0f;
	m_renderCapabilities.wideLinesSupported      = false;
	m_renderCapabilities.maxLineWidth            = 1.0f;
	m_renderCapabilities.integerPickingSupported = true;
	m_renderCapabilities.msaaSupported           = true;
}

ccVSGWindowInterface::~ccVSGWindowInterface() = default;

bool ccVSGWindowInterface::initializeViewer(vsg::ref_ptr<vsgQt::Viewer> viewer, vsgQt::Window* vsgWindow)
{
	if (m_initialized || !viewer || !vsgWindow || !vsgWindow->windowAdapter)
	{
		return false;
	}

	m_viewer = viewer;
	m_window = vsgWindow;

	vsg::ref_ptr<vsg::Window> window = vsgWindow->windowAdapter;
	m_viewer->addWindow(window);

	const VkExtent2D& extent = window->extent2D();
	const double      aspect = (extent.height > 0) ? static_cast<double>(extent.width) / static_cast<double>(extent.height) : 1.0;

	m_perspective = vsg::Perspective::create(30.0, aspect, 0.1, 1000.0);
	m_lookAt      = vsg::LookAt::create(vsg::dvec3(0.0, -5.0, 2.0), vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 0.0, 1.0));
	m_camera      = vsg::Camera::create(m_perspective,
                                        m_lookAt,
                                        vsg::ViewportState::create(0, 0, extent.width, extent.height));

	// Root of the VSG scene graph. It is kept in sync with the ccHObject tree
	// by ccVSGSceneBuilder (see M3).
	// Query the real device capabilities (M0 spike: never hardcode these, they
	// differ per GPU / driver - e.g. wideLines is unavailable on Metal/MoltenVK).
	if (vsg::ref_ptr<vsg::PhysicalDevice> physicalDevice = window->getOrCreatePhysicalDevice())
	{
		const VkPhysicalDeviceProperties& properties = physicalDevice->getProperties();
		const VkPhysicalDeviceFeatures&   features   = physicalDevice->getFeatures();
		const VkPhysicalDeviceLimits&     limits     = properties.limits;

		m_renderCapabilities.deviceName = QString::fromUtf8(properties.deviceName);

		m_renderCapabilities.pointSizeSupported = limits.pointSizeRange[1] > limits.pointSizeRange[0] + 1e-6f;
		m_renderCapabilities.maxPointSize       = limits.pointSizeRange[1];

		m_renderCapabilities.wideLinesSupported = (features.wideLines == VK_TRUE)
		                                          && (limits.lineWidthRange[1] > limits.lineWidthRange[0] + 1e-6f);
		m_renderCapabilities.maxLineWidth = limits.lineWidthRange[1];

		m_renderCapabilities.integerPickingSupported = true;
		m_renderCapabilities.msaaSupported           = true;
	}

	// Root of the VSG scene graph. It is kept in sync with the ccHObject tree
	// by ccVSGSceneBuilder (see M3).
	m_sceneRoot = vsg::Group::create();

	vsg::ref_ptr<vsg::CommandGraph> commandGraph = vsg::createCommandGraphForView(window, m_camera, m_sceneRoot);
	m_viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});

	// TODO(M2): replace by ccVSGCameraManipulator which implements the
	// CloudCompare camera semantics (pivot point, object centered view, ...)
	m_viewer->addEventHandler(vsg::Trackball::create(m_camera));

	m_viewer->compile();

	// Render continuously (a QTimer drives the frames)
	m_viewer->continuousUpdate = true;
	m_viewer->setInterval(16);

	m_initialized = true;

	return true;
}

void ccVSGWindowInterface::updateCamera()
{
	if (!m_camera)
	{
		return;
	}

	// TODO(M2): map ccViewportParameters (pivot, object/viewer centered view,
	// focal distance, near/far, fov, perspective/ortho) onto m_lookAt and
	// m_perspective. Beware: Vulkan NDC z is in [0,1] and not in [-1,1].
}

void ccVSGWindowInterface::setSceneDB(ccHObject* root)
{
	if (m_globalDBRoot == root)
	{
		return;
	}

	m_globalDBRoot = root;

	// TODO(M3): synchronize the ccHObject tree with the VSG scene graph
	//           (ccVSGSceneBuilder)
	redraw();
}

void ccVSGWindowInterface::setViewportParameters(const ccViewportParameters& params)
{
	m_viewportParams = params;

	updateCamera();

	redraw();
}

void ccVSGWindowInterface::setPickingMode(PICKING_MODE mode, Qt::CursorShape defaultCursorShape)
{
	Q_UNUSED(defaultCursorShape);

	if (m_pickingModeLocked)
	{
		return;
	}

	m_pickingMode = mode;
}

void ccVSGWindowInterface::setInteractionMode(INTERACTION_FLAGS flags)
{
	m_interactionFlags = flags;
}

void ccVSGWindowInterface::aboutToBeRemoved(ccDrawableObject* obj)
{
	Q_UNUSED(obj);
	// TODO(M3): drop the corresponding VSG nodes when an entity is removed
}
