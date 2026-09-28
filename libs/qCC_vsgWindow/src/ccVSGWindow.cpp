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
#include "ccVSGWindow.h"

// VSG
#include <vsg/all.h>

// vsgQt
#include <vsgQt/Window.h>

// Qt
#include <QMdiSubWindow>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

// system
#include <algorithm>

ccVSGWindow::ccVSGWindow(QWidget* parent /*=nullptr*/, bool silentInitialization /*=false*/)
    : QWidget(parent)
    , ccVSGWindowInterface()
{
	Q_UNUSED(silentInitialization);

	setObjectName(QStringLiteral("ccVSGWindow"));

	vsg::ref_ptr<vsg::WindowTraits> traits = vsg::WindowTraits::create();
	traits->windowTitle = "CloudCompare (VSG)";
	traits->width       = static_cast<uint32_t>(std::max(width(), 640));
	traits->height      = static_cast<uint32_t>(std::max(height(), 480));
	traits->decoration  = false; // the window is embedded in a Qt widget

	vsg::ref_ptr<vsgQt::Viewer> viewer = vsgQt::Viewer::create();

	// vsgQt::Window is a QWindow: it is owned by Qt (createWindowContainer
	// reparents it), hence the raw pointer (no vsg::ref_ptr here).
	vsgQt::Window* vsgWindow = new vsgQt::Window(viewer, traits);
	vsgWindow->initializeWindow();

	m_container = QWidget::createWindowContainer(vsgWindow, this);
	// The container's default size policy is derived from the embedded QWindow
	// (often Fixed/Preferred). If we don't make it Expanding, the QVBoxLayout
	// will leave the container top-left aligned with gray space around it after
	// MDI maximize/restore, which is exactly the "rendered image not filling the
	// 3DView" symptom.
	m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_container->setMinimumSize(1, 1);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_container, 1);
	setLayout(layout);

	// Resize events can be compressed by Qt during MDI maximize/restore,
	// leaving the embedded QWindow at an intermediate size. Coalesce them
	// with a single-shot timer and re-sync once the layout has its final
	// geometry.
	m_resizeTimer = new QTimer(this);
	m_resizeTimer->setSingleShot(true);
	connect(m_resizeTimer, &QTimer::timeout, this, &ccVSGWindow::onDeferredResize);

	// Once this widget has been parented into the MDI area, watch the
	// QMdiSubWindow for maximize/restore so we can force a re-sync even when
	// Qt does not deliver a usable resize event for the inner widget.
	QTimer::singleShot(0, this, [this]()
	{
		QWidget* p = parentWidget();
		while (p)
		{
			if (auto* sub = qobject_cast<QMdiSubWindow*>(p))
			{
				connect(sub, &QMdiSubWindow::windowStateChanged,
				        this, [this](Qt::WindowStates) { if (m_resizeTimer) m_resizeTimer->start(30); });
				break;
			}
			p = p->parentWidget();
		}
	});

	initializeViewer(viewer, vsgWindow);
}

ccVSGWindow::~ccVSGWindow()
{
	// the entities of the DB only keep a raw pointer to their display: they
	// must be unlinked while this object is still complete (i.e. here, and
	// not in ~ccVSGWindowInterface, whose vtable is the abstract one)
	unlinkEntitiesFromDisplay();

	if (m_viewer && m_window && m_window->windowAdapter)
	{
		m_viewer->deviceWaitIdle();
		m_viewer->removeWindow(m_window->windowAdapter);
	}

	m_viewer = {};
}

void ccVSGWindow::closeEvent(QCloseEvent* event)
{
	// mirrors ccGLWindowInterface's QEvent::Close handling (M6.6)
	if (m_unclosable)
	{
		event->ignore();
		return;
	}

	Q_EMIT m_signalEmitter->aboutToClose(this);

	QWidget::closeEvent(event);
}

void ccVSGWindow::scheduleDeferredAction(std::function<void()> action)
{
	if (!action)
	{
		return;
	}

	// 'this' is used as the context object: Qt drops the call if this widget
	// is destroyed before the timer fires
	QTimer::singleShot(0, this, std::move(action));
}

QSize ccVSGWindow::getScreenSize() const
{
	return m_container ? m_container->size() : size();
}

void ccVSGWindow::redraw(bool only2D /*=false*/, bool resetLOD /*=true*/)
{
	Q_UNUSED(only2D);
	Q_UNUSED(resetLOD);

	// incremental synchronization: only the entities whose fingerprint changed
	// are rebuilt (see ccVSGSceneBuilder::computeSignature())
	if (m_sceneBuilder.update() && m_viewer)
	{
		// new nodes must be compiled before they can be rendered
		m_viewer->compile();
	}

	// the 2D overlay may have produced new nodes too (color scale, M5.5)
	if (m_overlayNeedsCompile && m_viewer)
	{
		m_viewer->compile();
		m_overlayNeedsCompile = false;
	}

	if (m_viewer)
	{
		m_viewer->request();
	}
}

void ccVSGWindow::toBeRefreshed()
{
	m_shouldBeRefreshed = true;
}

void ccVSGWindow::refresh(bool only2D /*=false*/)
{
	if (m_shouldBeRefreshed)
	{
		m_shouldBeRefreshed = false;
		redraw(only2D);
	}
}

void ccVSGWindow::invalidateViewport()
{
	updateCamera();
}

void ccVSGWindow::deprecate3DLayer()
{
	// TODO(M3): the 3D layer is not cached yet (no offscreen FBO equivalent)
}

void ccVSGWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);

	// Force the embedded QWindow container to fill this widget immediately,
	// synchronously. Relying on the QVBoxLayout alone leaves the container at
	// its previous size right after an MDI maximize/restore, so the VSG
	// swapchain extent is never updated and the rendered image no longer
	// fills the view. Setting the geometry here also re-fires the QWindow's
	// own resize, which is what makes vsgQt::Window update its extent.
	if (m_container)
	{
		m_container->setGeometry(rect());
	}

	if (m_window)
	{
		const QSize sz = m_container ? m_container->size() : size();
		m_window->resize(std::max(sz.width(), 1), std::max(sz.height(), 1));
	}

	// The projection aspect is derived from the widget size (getScreenSize()),
	// so refresh the camera and request a new frame.
	invalidateViewport();
	redraw();

	// Qt may still re-layout the container once more after this event; defer a
	// second sync (using the now-correct container size) so the VSG window is
	// guaranteed to match the final geometry.
	if (m_resizeTimer)
	{
		m_resizeTimer->start(30);
	}
}

void ccVSGWindow::onDeferredResize()
{
	if (!m_window || !m_container)
	{
		return;
	}

	// Re-assert the container geometry, then read its (now settled) size.
	m_container->setGeometry(rect());

	const QSize sz = m_container->size();
	if (sz.isEmpty())
	{
		return;
	}

	m_window->resize(sz.width(), sz.height());
	invalidateViewport();
	redraw();
}
