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
#include "ccVSGWindowSignalEmitter.h"

// NOTE: this file must exist so that AUTOMOC generates the moc code for
// ccVSGWindowSignalEmitter (CMake AUTOMOC matches headers with a same name source).
ccVSGWindowSignalEmitter::ccVSGWindowSignalEmitter(ccVSGWindowInterface* associatedView, QObject* parent)
    : ccViewSignalEmitter(parent)
    , m_associatedView(associatedView)
{
}
