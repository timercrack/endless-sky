/* Clipboard.cpp
Copyright (c) 2025 by TomGoodIdea

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Clipboard.h"

#include "../SDL.h"
#include "Utf8.h"

using namespace std;



// Handle keys used for clipboard operations on inputBuffer. Return false if the keys
// don't have any functionality assigned to them. Optionally, size limit of the input buffer
// and a filter of allowed characters can be provided.
bool Clipboard::KeyDown(string &inputBuffer, SDL_Keycode key, Uint16 mod, size_t maxSize,
	const std::function<bool(char32_t)> &allowed)
{
	if(!(mod & KMOD_CTRL))
		return false;

	if(key == 'c')
		Set(inputBuffer);
	else if(key == 'x')
	{
		Set(inputBuffer);
		inputBuffer.clear();
	}
	else if(key == 'v' && inputBuffer.size() < maxSize)
		inputBuffer += Get(maxSize - inputBuffer.size(), allowed);
	else
		return false;

	return true;
}



// Replace the current contents with the provided string.
void Clipboard::Set(const string &text)
{
	SDL_SetClipboardText(text.c_str());
}



// Get the current clipboard contents, excluding characters we don't want.
// maxSize is in bytes; a multi-byte character is never split.
string Clipboard::Get(size_t maxSize, const function<bool(char32_t)> &allowed)
{
	if(!SDL_HasClipboardText())
		return {};

	char *clipboardBuffer = SDL_GetClipboardText();
	const string clipboard = clipboardBuffer;
	SDL_free(clipboardBuffer);

	string clipboardString;
	size_t pos = 0;
	while(pos < clipboard.size())
	{
		size_t start = pos;
		char32_t c = Utf8::DecodeCodePoint(clipboard, pos);
		if(c < ' ' || c == 0x7F || c == 0xFFFFFFFF || (allowed && !allowed(c)))
			continue;
		if(clipboardString.size() + (pos - start) > maxSize)
			break;
		clipboardString.append(clipboard, start, pos - start);
	}
	return clipboardString;
}
