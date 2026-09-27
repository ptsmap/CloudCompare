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
#include "ccRenderCapabilities.h"

// Qt
#include <QString>

// system
#include <functional>
#include <memory>
#include <vector>

class ccViewInterface;
class QWidget;

//! A render backend (OpenGL or VSG)
class CC_RENDER_CORE_LIB_API ccRenderBackend
{
  public:
	virtual ~ccRenderBackend() = default;

	//! Returns the backend name (e.g. "OpenGL" / "VSG")
	virtual QString name() const = 0;

	//! Returns the capabilities of this backend
	virtual const ccRenderCapabilities& capabilities() const = 0;

	//! Creates a new 3D view using this backend
	virtual ccViewInterface* createView(QWidget* parent = nullptr) = 0;
};

//! Registry of all the available render backends
/** Backends are registered at startup (OpenGL is always available, VSG only
	when it has been compiled in and when a Vulkan device is available).
**/
class CC_RENDER_CORE_LIB_API ccRenderBackendRegistry
{
  public:
	using Factory = std::function<std::unique_ptr<ccRenderBackend>()>;

	//! Returns the (unique) registry instance
	static ccRenderBackendRegistry& instance();

	//! Registers a backend (later registrations with the same name are ignored)
	void registerBackend(const QString& name, Factory factory);

	//! Returns the names of all the registered backends
	std::vector<QString> availableBackends() const;

	//! Returns the backend with the given name (nullptr if not found)
	ccRenderBackend* backend(const QString& name) const;

	//! Returns the backend that should be used by default
	/** "VSG" if it is registered, "OpenGL" otherwise.
	 **/
	ccRenderBackend* defaultBackend() const;

	//! Returns the name of the backend currently selected for new views
	/** Resolution order (highest priority first):
	    1. the \c CC_RENDER_BACKEND environment variable,
	    2. the persisted choice (QSettings group "RenderBackend", key "Selected"),
	    3. the implicit default: "OpenGL" when it is registered (safest
	       first-run), otherwise defaultBackend().
	 **/
	QString selectedBackendName() const;

	//! Persists the selected backend (no-op if the name is not registered)
	void setSelectedBackendName(const QString& name);

	//! Returns the backend that should be used to create new views
	/** Equivalent to backend(selectedBackendName()), falling back to
	    defaultBackend() when nothing is selected.
	 **/
	ccRenderBackend* currentBackend() const;

  private:
	ccRenderBackendRegistry() = default;

	struct Entry
	{
		QString                     name;
		Factory                     factory;
		std::unique_ptr<ccRenderBackend> instance;
	};

	std::vector<Entry> m_backends;
};
