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
#include "ccGLRenderBackend.h"

// qCC_glWindow
#include <ccGLWindowInterface.h>

// qCC_renderCore
#include <ccRenderCapabilities.h>

QString ccGLRenderBackend::name() const
{
	return QStringLiteral("OpenGL");
}

const ccRenderCapabilities& ccGLRenderBackend::capabilities() const
{
	static const ccRenderCapabilities caps = []
	{
		ccRenderCapabilities c;
		c.backendName          = QStringLiteral("OpenGL");
		c.pointSizeSupported   = true;
		c.maxPointSize         = 1024.0f;
		c.wideLinesSupported   = true;
		c.maxLineWidth         = 10.0f;
		c.integerPickingSupported = false;
		c.stereoSupported      = true;
		c.msaaSupported        = true;
		c.maxSamples           = 4;
		c.maxTextureSize       = 16384;
		return c;
	}();

	return caps;
}

ccViewInterface* ccGLRenderBackend::createView(QWidget* /*parent*/)
{
	// The OpenGL window widget is parented by the MDI area when it is added
	// (see MainWindow::new3DViewInternal), so the parent argument is ignored
	// here, like the rest of the legacy code does.
	const bool stereo = ccGLWindowInterface::TestStereoSupport();

	ccGLWindowInterface* window = nullptr;
	QWidget*             widget = nullptr;
	ccGLWindowInterface::Create(window, widget, stereo);

	return window;
}
