#pragma once
// ##########################################################################
// #                                                                        #
// #                              CLOUDCOMPARE                              #
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
// #          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
// #                                                                        #
// ##########################################################################

// Local
#include "qCC_db.h"

// CCCoreLib
#include <CCGeom.h>

// Qt
#include <QObject>

class ccHObject;
class ccViewInterface;

//! Signals that any 3D view must expose, whatever the render backend
/** ccGLWindowSignalEmitter and ccVSGWindowSignalEmitter both derive from this
    class so that backend agnostic code (ccPickingHub, the interactive tools,
    ...) can connect to a view without knowing which backend is in use.

    \note It lives in qCC_db because it needs the CCCoreLib geometry types and
    because qCC_db is the lowest level library shared by both backends.
**/
class QCC_DB_LIB_API ccViewSignalEmitter : public QObject
{
	Q_OBJECT

  public:
	explicit ccViewSignalEmitter(QObject* parent = nullptr);

Q_SIGNALS:

	//! Signal emitted when a point (or a triangle) is picked
	/** \param entity 'picked' entity
	    \param subEntityID point or triangle index in entity
	    \param x mouse cursor x position
	    \param y mouse cursor y position
	    \param P the picked point
	    \param uvw barycentric coordinates of the point (if picked on a mesh)
	**/
	void itemPicked(ccHObject* entity, unsigned subEntityID, int x, int y, const CCVector3& P, const CCVector3d& uvw);
};
