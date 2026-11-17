/* Localization.cpp
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

#include "Localization.h"

#include "../DataNode.h"

#include <mutex>
#include <unordered_map>

using namespace std;

namespace {
	// Data loads on a worker thread while the loading screen draws text.
	mutex dictionaryMutex;
	unordered_map<string, string> dictionary;
}



void Localization::Load(const DataNode &node)
{
	lock_guard<mutex> lock(dictionaryMutex);
	for(const DataNode &child : node)
	{
		if(child.Size() == 2)
			dictionary[child.Token(0)] = child.Token(1);
		else
			child.PrintTrace("Expected an original and a translated string:");
	}
}



const string &Localization::Translate(const string &text)
{
	lock_guard<mutex> lock(dictionaryMutex);
	auto it = dictionary.find(text);
	return it == dictionary.end() ? text : it->second;
}
