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
#include "ccRenderBackend.h"

// system
#include <algorithm>

ccRenderBackendRegistry& ccRenderBackendRegistry::instance()
{
	static ccRenderBackendRegistry registry;
	return registry;
}

void ccRenderBackendRegistry::registerBackend(const QString& name, Factory factory)
{
	auto it = std::find_if(m_backends.begin(),
	                       m_backends.end(),
	                       [&name](const Entry& entry)
	                       {
		                       return entry.name == name;
	                       });

	if (it != m_backends.end())
	{
		// already registered
		return;
	}

	Entry entry;
	entry.name    = name;
	entry.factory = std::move(factory);
	m_backends.emplace_back(std::move(entry));
}

std::vector<QString> ccRenderBackendRegistry::availableBackends() const
{
	std::vector<QString> names;
	names.reserve(m_backends.size());

	for (const Entry& entry : m_backends)
	{
		names.push_back(entry.name);
	}

	return names;
}

ccRenderBackend* ccRenderBackendRegistry::backend(const QString& name) const
{
	// const_cast is required because the backend instance is lazily created
	// (and cached) on first access.
	auto& mutableThis = const_cast<ccRenderBackendRegistry&>(*this);

	for (Entry& entry : mutableThis.m_backends)
	{
		if (entry.name == name)
		{
			if (!entry.instance && entry.factory)
			{
				entry.instance = entry.factory();
			}

			return entry.instance.get();
		}
	}

	return nullptr;
}

ccRenderBackend* ccRenderBackendRegistry::defaultBackend() const
{
	if (ccRenderBackend* vsgBackend = backend(QStringLiteral("VSG")))
	{
		return vsgBackend;
	}

	return backend(QStringLiteral("OpenGL"));
}
