/* Localization.h
Copyright (c) 2026 by the Endless Sky contributors

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <string>

class DataNode;



// A display-time dictionary from exact English UI strings to translations,
// loaded from "translation" root nodes. It covers labels that are also used
// as identifiers (outfit categories, commodities, attributes, settings) and
// therefore cannot be translated in the data or source code directly.
class Localization {
public:
	// Add the "original" "translation" pairs that are children of this node.
	static void Load(const DataNode &node);

	// Get the translation of the given text, or the text itself if there is none.
	static const std::string &Translate(const std::string &text);
};
