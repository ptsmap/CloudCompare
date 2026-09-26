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

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_container);
	setLayout(layout);

	initializeViewer(viewer, vsgWindow);
}

ccVSGWindow::~ccVSGWindow()
{
	if (m_viewer && m_window && m_window->windowAdapter)
	{
		m_viewer->deviceWaitIdle();
		m_viewer->removeWindow(m_window->windowAdapter);
	}

	m_viewer = {};
}

QSize ccVSGWindow::getScreenSize() const
{
	return m_container ? m_container->size() : size();
}

void ccVSGWindow::redraw(bool only2D /*=false*/, bool resetLOD /*=true*/)
{
	Q_UNUSED(only2D);
	Q_UNUSED(resetLOD);

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
