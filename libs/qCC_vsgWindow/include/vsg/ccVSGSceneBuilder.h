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
#include <qCC_vsgWindow.h>
#include <vsg/ccVSGPointCloudBuilder.h>

// VSG
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

// system
#include <unordered_map>

class ccHObject;

namespace vsg
{
	class Group;
	class Node;
}

//! Keeps a VSG scene graph in sync with the ccHObject tree
/** This is the counterpart of the historical "recursive draw()" of ccHObject:
    instead of re-emitting draw calls every frame, the entities are converted
    once into VSG nodes and the VSG scene graph is then traversed / culled /
    recorded by the viewer itself.

    The first implementation rebuilds the whole graph when it is invalidated;
    incremental (per entity) updates will be added once the entity revision
    mechanism is in place.
**/
class ccVSGSceneBuilder
{
  public:
	ccVSGSceneBuilder() = default;

	//! Sets the root of the ccHObject tree to mirror
	void setRoot(ccHObject* root);

	//! Returns the root of the ccHObject tree
	ccHObject* root() const
	{
		return m_root;
	}

	//! Flags the whole graph as needing a rebuild
	void invalidate();

	//! Rebuilds the VSG graph if it has been invalidated
	/** Must be called outside of the record traversal.
	 **/
	void update();

	//! Returns the VSG scene graph root
	vsg::ref_ptr<vsg::Group> sceneRoot() const
	{
		return m_sceneRoot;
	}

	//! Returns the point cloud builder (to control the point size, etc.)
	ccVSGPointCloudBuilder& pointCloudBuilder()
	{
		return m_pointCloudBuilder;
	}

  private:
	void rebuild();
	void addChildren(ccHObject* parent, vsg::ref_ptr<vsg::Group> parentGroup);

	ccHObject*             m_root = nullptr;

	//! Stable root: its content is replaced on rebuild so that the command
	//! graph can keep referencing the very same node.
	vsg::ref_ptr<vsg::Group> m_sceneRoot = vsg::Group::create();

	//! ccHObject -> VSG sub graph (used for incremental updates later on)
	std::unordered_map<ccHObject*, vsg::ref_ptr<vsg::Group>> m_entries;

	bool m_dirty = false;

	ccVSGPointCloudBuilder m_pointCloudBuilder;
};
