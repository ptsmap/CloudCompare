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
#include <vsg/ccVSGMeshBuilder.h>
#include <vsg/ccVSGPointCloudBuilder.h>

// VSG
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

// system
#include <set>
#include <unordered_map>

class ccHObject;

namespace vsg
{
	class Group;
	class Node;
}

//! Bin used for the transparent (alpha blended) entities - see M4.5
/** The matching vsg::Bin (sorted back to front) is registered on the view by
    ccVSGWindowInterface::initializeViewer(). **/
constexpr int32_t CC_VSG_TRANSPARENT_BIN = 1;

//! Keeps a VSG scene graph in sync with the ccHObject tree
/** Instead of re-emitting draw calls every frame (as the historical
    ccHObject::draw() did), the entities are converted into VSG nodes and the
    VSG scene graph is then traversed / culled / recorded by the viewer itself.

    Updates are incremental: every entity is fingerprinted (see
    computeSignature()) and only the entities whose fingerprint changed are
    rebuilt. Entities that disappeared from the tree are dropped.

    \warning The fingerprint only covers the *structure* of an entity (size,
    presence of colors / scalar field, transformation, ...). A change of the
    actual values (e.g. editing one point color) is not detected: call
    invalidate() in that case. A proper per entity revision counter will
    replace this heuristic.
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

	//! Synchronizes the VSG graph with the ccHObject tree
	/** Must be called outside of the record traversal.
	    \return true if at least one entity has been (re)built
	 **/
	bool update();

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
	//! Per entity bookkeeping
	struct Entry
	{
		//! Sub tree root: a vsg::Group, or a vsg::MatrixTransform when the
		//! entity has a temporary transformation
		vsg::ref_ptr<vsg::Node> node;
		//! Fingerprint of the entity at the time 'node' was built
		quint64 signature = 0;
	};

	void rebuild();
	bool syncIncremental();
	bool syncChildren(ccHObject* parent, vsg::ref_ptr<vsg::Group> parentGroup, std::set<ccHObject*>& visited);
	bool syncEntity(ccHObject* obj, Entry& entry);

	static quint64 computeSignature(ccHObject* obj);

	ccHObject*             m_root = nullptr;

	//! Stable root: its content is replaced on sync so that the command graph
	//! can keep referencing the very same node.
	vsg::ref_ptr<vsg::Group> m_sceneRoot = vsg::Group::create();

	std::unordered_map<ccHObject*, Entry> m_entries;

	bool m_dirty = false;

	ccVSGPointCloudBuilder m_pointCloudBuilder;
	ccVSGMeshBuilder       m_meshBuilder;
};
