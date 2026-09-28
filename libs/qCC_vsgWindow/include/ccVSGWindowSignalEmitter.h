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

// qCC_db
#include <ccViewSignalEmitter.h>

// Qt
#include <QObject>

class ccVSGWindowInterface;

//! ccVSGWindow signal emitter
/** Derives from ccViewSignalEmitter so that the backend agnostic code
    (ccPickingHub, interactive tools, ...) can connect to a VSG view exactly
    like it connects to an OpenGL one.
**/
class CCVSGWINDOW_LIB_API ccVSGWindowSignalEmitter : public ccViewSignalEmitter
{
	Q_OBJECT

  public:
	ccVSGWindowSignalEmitter(ccVSGWindowInterface* associatedView, QObject* parent = nullptr);

Q_SIGNALS:

	//! Signal emitted when the associated view is about to close
	void aboutToClose(ccVSGWindowInterface* view);

	//! Signal emitted with the 3D coordinate under the mouse cursor
	/** Emitted by ccVSGWindowInterface::processMouseMove() when the cursor
	    coordinate display is enabled (see showCursorCoordinates()): the
	    backend reads the depth under the cursor and unprojects it (M6.6).
	 **/
	void cursorCoordinates(const CCVector3d& P3D);

  protected:
	ccVSGWindowInterface* m_associatedView = nullptr;
};
