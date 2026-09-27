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

// qCC_renderCore
#include <ccRenderBackend.h>

// qCC_glWindow (defines CCGLWINDOW_LIB_API)
#include <qCC_glWindow.h>

//! OpenGL render backend
/** Implements ccRenderBackend for the legacy Qt/OpenGL view (ccGLWindow).
    See doc/VSG_Rendering_Migration_Plan.md.
**/
class CCGLWINDOW_LIB_API ccGLRenderBackend : public ccRenderBackend
{
  public:
	QString name() const override;
	const ccRenderCapabilities& capabilities() const override;
	ccViewInterface* createView(QWidget* parent = nullptr) override;
};
