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

// VSG
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>

//! The nodes built for a single entity
/** Every builder returns **two** nodes:

      - `display`: the node of the scene graph that is drawn on screen
      - `ids`:     the node of the **entity picking** pass (M6.1), which
                   renders the very same geometry into an offscreen
                   `VK_FORMAT_R32_UINT` attachment, where each entity writes
                   the unique ID of the `ccHObject` it stands for

    Both nodes share the same vertex arrays (positions, quad corners, ...):
    only the pipeline and the per instance/vertex ID attribute differ, so the
    picking pass costs no extra CPU geometry and stays pixel-exact with what
    is displayed ("what you see is what you pick").

    `ids` may be null for the entities that are not pickable.
**/
struct ccVSGBuiltNodes
{
	//! Node of the displayed scene graph
	vsg::ref_ptr<vsg::Node> display;

	//! Node of the picking scene graph (R32_UINT pass)
	vsg::ref_ptr<vsg::Node> ids;
};
