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
#include <ccBBox.h>
#include <ccDrawableObject.h>
#include <ccGLMatrix.h>
#include <ccGenericMesh.h>
#include <ccGenericPointCloud.h>
#include <ccHObject.h>
#include <ccLog.h>

// CCCoreLib
#include <CCConst.h>

// VSG
#include <vsg/all.h>

// vsgQt
#include <vsgQt/Window.h>

// system
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

ccVSGWindowInterface::ccVSGWindowInterface()
{
	m_signalEmitter = new ccVSGWindowSignalEmitter(this);

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

ccVSGWindowInterface::~ccVSGWindowInterface()
{
	delete m_signalEmitter;
	m_signalEmitter = nullptr;
}

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

	// The camera is driven by the CloudCompare viewport parameters (single
	// source of truth), exposed to VSG through two thin matrix adapters.
	m_viewMatrix       = ccVSGViewMatrix::create();
	m_projectionMatrix = ccVSGProjectionMatrix::create();
	m_camera           = vsg::Camera::create(m_projectionMatrix,
                                             m_viewMatrix,
                                             vsg::ViewportState::create(0, 0, extent.width, extent.height));

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
	m_sceneRoot = m_sceneBuilder.sceneRoot();

	vsg::ref_ptr<vsg::CommandGraph> commandGraph = vsg::createCommandGraphForView(window, m_camera, m_sceneRoot);
	m_viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});

	// CloudCompare camera semantics (virtual trackball, pivot point, ...)
	m_manipulator = ccVSGCameraManipulator::create(this);
	m_viewer->addEventHandler(m_manipulator);

	updateCamera();

	m_viewer->compile();

	// Render continuously (a QTimer drives the frames)
	m_viewer->continuousUpdate = true;
	m_viewer->setInterval(16);

	m_initialized = true;

	return true;
}

vsg::dmat4 ccVSGWindowInterface::viewMatrix() const
{
	return m_viewMatrix ? m_viewMatrix->matrix : vsg::dmat4();
}

vsg::dmat4 ccVSGWindowInterface::projectionMatrix() const
{
	return m_projectionMatrix ? m_projectionMatrix->matrix : vsg::dmat4();
}

void ccVSGWindowInterface::rotateBaseViewMat(const ccGLMatrixd& rotMat)
{
	m_viewportParams.viewMat = rotMat * m_viewportParams.viewMat;

	updateCamera();
	redraw();
}

void ccVSGWindowInterface::moveCamera(const CCVector3d& v)
{
	// current X, Y and Z viewing directions correspond to the 'model view' matrix rows
	CCVector3d u = v;
	if (!m_viewportParams.objectCenteredView)
	{
		m_viewportParams.viewMat.transposed().applyRotation(u);
	}

	setCameraPos(m_viewportParams.getCameraCenter() + u);
}

void ccVSGWindowInterface::setCameraPos(const CCVector3d& P)
{
	m_viewportParams.setCameraCenter(P, true);

	updateCamera();
	redraw();
}

void ccVSGWindowInterface::setFocalDistance(double focalDistance)
{
	m_viewportParams.setFocalDistance(focalDistance);

	updateCamera();
	redraw();
}

void ccVSGWindowInterface::updateCamera()
{
	if (!m_camera || !m_viewMatrix || !m_projectionMatrix)
	{
		return;
	}

	// ----------------------------------------------------------------------
	// Point size (part of the viewport parameters)
	// ----------------------------------------------------------------------
	m_sceneBuilder.pointCloudBuilder().setPointSize(m_viewportParams.defaultPointSize);

	// ----------------------------------------------------------------------
	// View matrix: strictly identical to the OpenGL backend
	// ----------------------------------------------------------------------
	const ccGLMatrixd viewMatd = m_viewportParams.computeViewMatrix();
	m_viewMatrix->matrix       = toVSGMatrix(viewMatd);

	// ----------------------------------------------------------------------
	// Projection matrix
	// Replicates ccGLWindowInterface::computeProjectionMatrix() but builds the
	// matrix with the vsg:: helpers, which produce **Vulkan reverse depth**
	// matrices (near -> NDC z = 1, far -> NDC z = 0) and invert the Y axis.
	// ----------------------------------------------------------------------
	const QSize screenSize = getScreenSize();
	const int   width      = std::max(screenSize.width(), 1);
	const int   height     = std::max(screenSize.height(), 1);

	// bounding box of the visible objects
	ccBBox visibleObjectsBBox;
	if (m_globalDBRoot)
	{
		visibleObjectsBBox = m_globalDBRoot->getBB_recursive(false, true);
	}
	if (m_winDBRoot)
	{
		const ccBBox ownBox = m_winDBRoot->getBB_recursive(false, true);
		visibleObjectsBBox += ownBox;
	}
	if (!visibleObjectsBBox.isValid())
	{
		// default box, as ccGLWindowInterface::computeProjectionMatrix() does
		constexpr PointCoordinateType halfSize = static_cast<PointCoordinateType>(0.5);
		visibleObjectsBBox                     = ccBBox(CCVector3(-halfSize, -halfSize, -halfSize),
                                                       CCVector3(halfSize, halfSize, halfSize),
                                                       true);
	}

	const double bbHalfDiag = visibleObjectsBBox.getDiagNormd() / 2.0;

	// min and max distances in camera space (apply the view matrix to the 8 corners)
	double zMin = std::numeric_limits<double>::quiet_NaN();
	double zMax = zMin;
	{
		const CCVector3&         bbMin = visibleObjectsBBox.minCorner();
		const CCVector3&         bbMax = visibleObjectsBBox.maxCorner();
		std::array<CCVector3, 8> bbCorners{
		    bbMin,
		    CCVector3(bbMin.x, bbMin.y, bbMax.z),
		    CCVector3(bbMin.x, bbMax.y, bbMin.z),
		    CCVector3(bbMax.x, bbMin.y, bbMin.z),
		    bbMax,
		    CCVector3(bbMax.x, bbMax.y, bbMin.z),
		    CCVector3(bbMax.x, bbMin.y, bbMax.z),
		    CCVector3(bbMin.x, bbMax.y, bbMax.z)};

		for (const CCVector3& P : bbCorners)
		{
			const CCVector3d Pd = viewMatd * CCVector3d::fromArray(P.u);
			const double     z  = -Pd.z; // the camera looks toward -Z
			if (std::isnan(zMin) || z < zMin)
			{
				zMin = z;
			}
			if (std::isnan(zMax) || z > zMax)
			{
				zMax = z;
			}
		}
	}

	double       zNear                   = zMin;
	double       zFar                    = zMax;
	const double epsilon                 = std::max(bbHalfDiag / 1000.0, 1.0e-6);
	const double ar                      = static_cast<double>(height) / static_cast<double>(width);
	const double distanceToHalfWidthRatio = m_viewportParams.computeDistanceToHalfWidthRatio();

	if (m_viewportParams.perspectiveView)
	{
		const double minZFar = static_cast<double>(CCCoreLib::ZERO_TOLERANCE_F) / m_viewportParams.zNearCoef;
		if (zFar < minZFar)
		{
			// no object in front of the camera! (or too small)
			zFar  = minZFar;
			zNear = epsilon;
		}
		else
		{
			zNear = std::max(zMin, zFar * m_viewportParams.zNearCoef);
		}

		const double delta = zFar / 2.0;
		if (zFar - zNear < delta)
		{
			// zNear can't be too close to zFar
			zNear -= delta / 2.0;
			zFar += delta / 2.0;
		}

		const double xMax = zNear * distanceToHalfWidthRatio;
		const double yMax = xMax * ar;

		m_projectionMatrix->matrix = vsg::perspective(-xMax, xMax, -yMax, yMax, zNear, zFar);
	}
	else
	{
		zFar += epsilon;
		zNear -= epsilon;

		const double xMax = std::abs(m_viewportParams.getFocalDistance()) * distanceToHalfWidthRatio;
		const double yMax = xMax * ar;

		m_projectionMatrix->matrix = vsg::orthographic(-xMax, xMax, -yMax, yMax, zNear, zFar);
	}

	m_viewportParams.zNear = zNear;
	m_viewportParams.zFar  = zFar;
}

void ccVSGWindowInterface::setSceneDB(ccHObject* root)
{
	if (m_globalDBRoot == root)
	{
		return;
	}

	m_globalDBRoot = root;

	// synchronize the ccHObject tree with the VSG scene graph
	m_sceneBuilder.setRoot(root);

	// the new nodes have to be compiled before they can be rendered
	if (m_sceneBuilder.update() && m_viewer)
	{
		m_viewer->compile();
	}

	// mirrors ccGLWindowInterface::setSceneDB(): adapt the zoom (and hence the
	// near/far planes) to the new scene contents
	zoomGlobal();

	// in case the zoom could not be applied (empty DB)
	updateCamera();
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

void ccVSGWindowInterface::zoomGlobal()
{
	// mirrors ccGLWindowInterface::updateConstellationCenterAndZoom()

	// bounding box of the visible objects
	ccBBox zoomedBox;
	if (m_globalDBRoot)
	{
		zoomedBox = m_globalDBRoot->getBB_recursive(false, true);
	}
	if (m_winDBRoot)
	{
		zoomedBox += m_winDBRoot->getBB_recursive(false, true);
	}
	if (!zoomedBox.isValid())
	{
		return;
	}

	double bbDiag = zoomedBox.getDiagNorm();
	if (CCCoreLib::LessThanEpsilon(bbDiag))
	{
		ccLog::Warning("[ccVSGWindow] Entity/DB has a null bounding-box!");
		bbDiag = 1.0;
	}

	// the pivot point is set on the box center
	const CCVector3d P = zoomedBox.getCenter();
	m_viewportParams.setPivotPoint(P, false);

	// distance required for the camera to see the whole bounding box
	const QSize screenSize = getScreenSize();
	const int   width      = std::max(screenSize.width(), 1);
	const int   height     = std::max(screenSize.height(), 1);
	const double focalDistance = bbDiag / m_viewportParams.computeDistanceToWidthRatio(width, height);

	setCameraPos(P);

	CCVector3d v(0, 0, focalDistance);
	moveCamera(v);

	// just in case
	updateCamera();
	redraw();
}

void ccVSGWindowInterface::getGLCameraParameters(ccGLCameraParameters& params) const
{
	const QSize screenSize = getScreenSize();
	const int   width      = std::max(screenSize.width(), 1);
	const int   height     = std::max(screenSize.height(), 1);

	// The view matrix is strictly the same as the OpenGL one: it is derived
	// from ccViewportParameters, which is the single source of truth.
	params.modelViewMat = m_viewportParams.computeViewMatrix();

	// The projection matrix, on the other hand, has to be converted back from
	// the Vulkan convention (reverse depth + inverted Y axis).
	params.projectionMat = fromVSGMatrix(vulkanToGLProjection(projectionMatrix()));

	params.viewport[0] = 0;
	params.viewport[1] = 0;
	params.viewport[2] = width;
	params.viewport[3] = height;

	params.pixelSize = m_viewportParams.computePixelSize(width, height);

	params.perspective       = m_viewportParams.perspectiveView;
	params.fov_deg           = m_viewportParams.fov_deg;
	params.nearClippingDepth = m_viewportParams.nearClippingDepth;
	params.farClippingDepth  = m_viewportParams.farClippingDepth;
}

void ccVSGWindowInterface::doPicking(int x, int y)
{
	if (m_pickingMode == NO_PICKING || !m_signalEmitter)
	{
		return;
	}

	const QSize screenSize = getScreenSize();
	const int   height     = screenSize.height();
	if (height <= 0)
	{
		return;
	}

	// CloudCompare works in a Y-up screen space (see the OpenGL backend)
	const CCVector2d clickedPos(static_cast<double>(x), height - 1 - static_cast<double>(y));

	ccGLCameraParameters camera;
	getGLCameraParameters(camera);

	ccHObject* nearestEntity            = nullptr;
	int        nearestElementIndex      = -1;
	double     nearestElementSquareDist = -1.0;
	CCVector3  nearestPoint(0, 0, 0);
	CCVector3d nearestPointBC(0, 0, 0);

	// size of the picked area (in pixels) - same default as the OpenGL backend
	constexpr double pickWidth  = 5.0;
	constexpr double pickHeight = 5.0;

	std::vector<ccHObject*> toProcess;
	if (m_globalDBRoot)
	{
		toProcess.push_back(m_globalDBRoot);
	}
	if (m_winDBRoot)
	{
		toProcess.push_back(m_winDBRoot);
	}

	try
	{
		while (!toProcess.empty())
		{
			ccHObject* ent = toProcess.back();
			toProcess.pop_back();

			if (!ent->isEnabled())
			{
				continue;
			}

			bool ignoreSubmeshes = false;

			// we only consider the entities displayed in this very view
			if (ent->isDisplayedIn(this))
			{
				if (ent->isKindOf(CC_TYPES::POINT_CLOUD))
				{
					auto* cloud = static_cast<ccGenericPointCloud*>(ent);

					int    nearestPointIndex = -1;
					double nearestSquareDist = 0.0;

					// TODO(M6): honor ccGui::Parameters().autoComputeOctree
					// (the OpenGL backend may ask the user and build one)
					if (cloud->pointPicking(clickedPos, camera, nearestPointIndex, nearestSquareDist, pickWidth, pickHeight))
					{
						if (nearestElementIndex < 0 || (nearestPointIndex >= 0 && nearestSquareDist < nearestElementSquareDist))
						{
							nearestElementSquareDist = nearestSquareDist;
							nearestElementIndex      = nearestPointIndex;
							nearestPoint             = *(cloud->getPoint(nearestPointIndex));
							nearestEntity            = cloud;
							nearestPointBC           = CCVector3d(0, 0, 0);
						}
					}
				}
				else if (ent->isKindOf(CC_TYPES::MESH)
				         && !ent->isA(CC_TYPES::MESH_GROUP)       // their children are processed
				         && !ent->isA(CC_TYPES::COORDINATESYSTEM) // ignored by the OpenGL backend too
				)
				{
					ignoreSubmeshes = true;

					auto* mesh = static_cast<ccGenericMesh*>(ent);

					int        nearestTriIndex   = -1;
					double     nearestSquareDist = 0.0;
					CCVector3d P;
					CCVector3d barycentricCoords;

					if (mesh->trianglePicking(clickedPos,
					                          camera,
					                          mesh->isShownAsWire(), // only the edges in this case
					                          nearestTriIndex,
					                          nearestSquareDist,
					                          P,
					                          &barycentricCoords))
					{
						if (nearestElementIndex < 0 || (nearestTriIndex >= 0 && nearestSquareDist < nearestElementSquareDist))
						{
							nearestElementSquareDist = nearestSquareDist;
							nearestElementIndex      = nearestTriIndex;
							nearestPoint             = P.toPC();
							nearestEntity            = mesh;
							nearestPointBC           = barycentricCoords;
						}
					}
				}
			}

			// process the children
			for (unsigned i = 0; i < ent->getChildrenNumber(); ++i)
			{
				ccHObject* child = ent->getChild(i);

				// we ignore the sub-meshes of the current (mesh) entity as
				// their content is the same!
				if (ignoreSubmeshes && child->isKindOf(CC_TYPES::SUB_MESH))
				{
					continue;
				}

				toProcess.push_back(child);
			}
		}
	}
	catch (const std::bad_alloc&)
	{
		ccLog::Warning("[Picking][VSG] Not enough memory!");
	}

	switch (m_pickingMode)
	{
	case POINT_PICKING:
	case TRIANGLE_PICKING:
	case POINT_OR_TRIANGLE_PICKING:
	case POINT_OR_TRIANGLE_OR_LABEL_PICKING:
		// a signal must always be emitted, even if nothing was picked!
		Q_EMIT m_signalEmitter->itemPicked(nearestEntity,
		                                   static_cast<unsigned>(std::max(nearestElementIndex, 0)),
		                                   x,
		                                   y,
		                                   nearestPoint,
		                                   nearestPointBC);
		break;

	case ENTITY_PICKING:
	case ENTITY_RECT_PICKING:
	case FAST_PICKING:
	case LABEL_PICKING:
	default:
		// TODO(M6): these modes rely on a color based (GPU) picking pass in the
		// OpenGL backend. They need a CPU ray/entity intersection here.
		break;
	}
}
