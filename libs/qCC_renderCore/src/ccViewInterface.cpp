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
#include <ccViewInterface.h>

// Qt
#include <QWidget>

ccViewInterface* ccViewInterface::FromWidget(QWidget* widget)
{
	if (!widget)
	{
		return nullptr;
	}

	// both ccGLWindow and ccVSGWindow derive from QWidget *and* from
	// ccViewInterface, so a cross cast does the job
	return dynamic_cast<ccViewInterface*>(widget);
}
