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
#include "ccVSGRenderBackend.h"

// qCC_vsgWindow
#include <ccVSGWindow.h>

// qCC_renderCore
#include <ccRenderCapabilities.h>

QString ccVSGRenderBackend::name() const
{
	return QStringLiteral("VSG");
}

const ccRenderCapabilities& ccVSGRenderBackend::capabilities() const
{
	static const ccRenderCapabilities caps = []
	{
		ccRenderCapabilities c;
		c.backendName        = QStringLiteral("VSG");
		c.deviceName         = QStringLiteral("MoltenVK (Metal)");
		// Metal/MoltenVK ignores gl_PointSize (> 1) and has no 'wideLines'
		// feature: the VSG backend must fall back to billboard quads.
		c.pointSizeSupported = false;
		c.maxPointSize       = 1.0f;
		c.wideLinesSupported = false;
		c.maxLineWidth       = 1.0f;
		c.integerPickingSupported = false;
		c.stereoSupported    = false;
		c.msaaSupported      = true;
		c.maxSamples         = 4;
		c.maxTextureSize     = 16384;
		return c;
	}();

	return caps;
}

ccViewInterface* ccVSGRenderBackend::createView(QWidget* parent)
{
	auto* window = new ccVSGWindow(parent);
	return window;
}
