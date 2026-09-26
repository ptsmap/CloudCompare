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
#include "qCC_renderCore.h"

// Qt
#include <QString>

//! Capabilities of a render backend
/** The OpenGL and Vulkan (VSG) backends do not support the same feature set.
	The application code must query this structure instead of assuming that a
	given feature is always available (this is especially important on macOS
	where Vulkan runs on top of Metal/MoltenVK).
**/
struct CC_RENDER_CORE_LIB_API ccRenderCapabilities
{
	//! Backend name (e.g. "OpenGL" / "VSG")
	QString backendName;

	//! Human readable description of the device actually used
	QString deviceName;

	//! Whether gl_PointSize (or its Vulkan equivalent) can be set dynamically
	/** MoltenVK/Metal do not support point sizes > 1. When false the backend
		must fall back to billboarded quads to render points.
	**/
	bool pointSizeSupported = true;

	//! Maximum point size (when supported)
	float maxPointSize = 64.0f;

	//! Whether line widths greater than 1.0 are supported
	/** The Vulkan 'wideLines' feature is optional and often unavailable.
	 **/
	bool wideLinesSupported = true;

	//! Maximum line width (when supported)
	float maxLineWidth = 1.0f;

	//! Whether an integer (R32_UINT) attachment can be used for picking
	/** Allows picking IDs to be encoded on 32 bits instead of the 24 bits
		of the historical RGB color based picking.
	 **/
	bool integerPickingSupported = false;

	//! Whether quad-buffer stereo rendering is available
	bool stereoSupported = false;

	//! Whether multi-sampling is available
	bool msaaSupported = true;

	//! Maximum number of samples (when MSAA is supported)
	int maxSamples = 1;

	//! Maximum texture dimension
	unsigned maxTextureSize = 4096;
};
