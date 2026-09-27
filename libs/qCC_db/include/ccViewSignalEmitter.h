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

// system
#include <unordered_set>

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

	//! Signal emitted when an entity is selected in the 3D view
	/** Emitted by the ENTITY_PICKING mode (see ccViewInterface::PICKING_MODE).

	    \note It used to be declared by ccGLWindowSignalEmitter only, which
	    made it impossible to connect to a VSG view. It now lives here so that
	    both backends expose the very same signal (M6.1).
	 **/
	void entitySelectionChanged(ccHObject* entity);

	//! Signal emitted when several entities are selected in the 3D view
	/** Emitted by the ENTITY_RECT_PICKING mode: the set holds the unique IDs
	    of the selected entities (see the OpenGL backend).
	 **/
	void entitiesSelectionChanged(std::unordered_set<int> entIDs);

	//! Signal emitted when an item is picked in FAST_PICKING mode
	/** \param entity      entity
	    \param subEntityID point or triangle index in entity (-1 if unknown)
	    \param x           mouse cursor x position
	    \param y           mouse cursor y position
	 **/
	void itemPickedFast(ccHObject* entity, int subEntityID, int x, int y);

	//! Signal emitted when the pivot point is changed
	/** \note It used to be declared by ccGLWindowSignalEmitter only, which
	    made it impossible to connect to a VSG view. It now lives here so that
	    both backends expose the very same signal (M6.4).
	 **/
	void pivotPointChanged(const CCVector3d&);
};
