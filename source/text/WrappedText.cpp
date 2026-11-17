/* WrappedText.cpp
Copyright (c) 2014-2020 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "WrappedText.h"

#include "DisplayText.h"
#include "Font.h"
#include "Utf8.h"

#include <cstring>

using namespace std;

namespace {
	// Characters that may be wrapped individually, without surrounding whitespace.
	bool IsCJK(char32_t c)
	{
		return (c >= 0x2010 && c <= 0x206F) ||   // General Punctuation (CJK quotes, dashes, ellipsis)
			(c >= 0x2E80 && c <= 0x9FFF) ||   // CJK Radicals, Kangxi Radicals, CJK Unified Ideographs
			(c >= 0xAC00 && c <= 0xD7A3) ||   // Hangul Syllables
			(c >= 0xF900 && c <= 0xFAFF) ||   // CJK Compatibility Ideographs
			(c >= 0xFE30 && c <= 0xFE4F) ||   // CJK Compatibility Forms
			(c >= 0xFF00 && c <= 0xFFEF);     // Halfwidth and Fullwidth Forms
	}

	// Punctuation that must not begin a line; it may hang past the wrap width instead.
	bool IsNoLineStart(char32_t c)
	{
		static const u32string NO_START = U"，。、；：？！）］｝〕〉》」』】〙〗〟’”…‥—–·・～％";
		return NO_START.find(c) != u32string::npos;
	}

	// Punctuation that must not end a line; it is carried to the next line with what follows it.
	bool IsNoLineEnd(char32_t c)
	{
		static const u32string NO_END = U"（［｛〔〈《「『【〘〖〝‘“";
		return NO_END.find(c) != u32string::npos;
	}
}



WrappedText::WrappedText(const Font &font)
{
	SetFont(font);
}



void WrappedText::SetAlignment(Alignment align)
{
	alignment = align;
}



// Set the truncate mode.
void WrappedText::SetTruncate(Truncate trunc)
{
	truncate = trunc;
}



// Set the wrap width. This does not include any margins.
int WrappedText::WrapWidth() const
{
	return wrapWidth;
}



void WrappedText::SetWrapWidth(int width)
{
	wrapWidth = width;
}



// Set the font to use. This will also set sensible defaults for the tab
// width, line height, and paragraph break. You must specify the wrap width
// and the alignment separately.
void WrappedText::SetFont(const Font &font)
{
	this->font = &font;

	space = font.Space();
	SetTabWidth(4 * space);
	SetLineHeight(font.Height() * 120 / 100);
	SetParagraphBreak(font.Height() * 40 / 100);
}



// Set the width in pixels of a single '\t' character.
int WrappedText::TabWidth() const
{
	return tabWidth;
}



void WrappedText::SetTabWidth(int width)
{
	tabWidth = width;
}



// Set the height in pixels of one line of text within a paragraph.
int WrappedText::LineHeight() const
{
	return lineHeight;
}



void WrappedText::SetLineHeight(int height)
{
	lineHeight = height;
}



// Set the extra spacing in pixels to be added in between paragraphs.
int WrappedText::ParagraphBreak() const
{
	return paragraphBreak;
}



void WrappedText::SetParagraphBreak(int height)
{
	paragraphBreak = height;
}



void WrappedText::Wrap(const string &str)
{
	original = str;
	Rewrap();
}



void WrappedText::Wrap(const char *str)
{
	original.assign(str, strlen(str));
	Rewrap();
}



void WrappedText::Rewrap()
{
	// Copy the original text into a string that will be worked on to determine
	// the spacing for the chosen text alignment.
	text = original;
	// Clear any previous word-wrapping data. It becomes invalid as soon as the
	// underlying text buffer changes.
	words.clear();
	Wrap();
}



/// Get the height of the wrapped text.
/// With trailingBreak, include a paragraph break after the text.
int WrappedText::Height(bool trailingBreak) const
{
	if(!height)
		return 0;
	return height + trailingBreak * paragraphBreak;
}



// Return the width of the longest line of the wrapped text.
int WrappedText::LongestLineWidth() const
{
	return longestLineWidth;
}



// Draw the text.
void WrappedText::Draw(const Point &topLeft, const Color &color) const
{
	if(words.empty())
		return;

	if(truncate == Truncate::NONE)
		for(const Word &w : words)
			font->Draw(string(text.c_str() + w.Index(), w.Length()), w.Pos() + topLeft, color);
	else
	{
		// Currently, we only apply truncation to a line if it contains a single word.
		int h = words[0].y - 1;
		for(size_t i = 0; i < words.size(); ++i)
		{
			const Word &w = words[i];
			if(h == w.y && (i != words.size() - 1 && w.y == words[i + 1].y))
				font->Draw(string(text.c_str() + w.Index(), w.Length()), w.Pos() + topLeft, color);
			else
				font->Draw({string(text.c_str() + w.Index(), w.Length()), {wrapWidth, truncate}}, w.Pos() + topLeft, color);
			h = w.y;
		}
	}
}



size_t WrappedText::Word::Index() const
{
	return index;
}



size_t WrappedText::Word::Length() const
{
	return length;
}



Point WrappedText::Word::Pos() const
{
	return Point(x, y);
}



void WrappedText::Wrap()
{
	height = 0;
	longestLineWidth = 0;

	if(text.empty() || !font)
		return;

	// Do this as a finite state machine with UTF-8 support. "word" holds the
	// pen position and, while traversing, the start of the current word.
	Word word;
	bool traversingWord = false;

	// Keep track of how wide the current line is. This is just so we know how
	// much extra space must be allotted by the alignment code.
	int lineWidth = 0;
	// This is the index in the "words" vector of the first word on this line.
	size_t lineBegin = 0;

	// Finish the current line. An opening bracket or quote at its end moves
	// to the new line, so that it stays with the text it introduces.
	auto newLine = [&]()
	{
		bool carry = false;
		if(words.size() > lineBegin + 1)
		{
			const Word &last = words.back();
			size_t end = last.index;
			carry = IsNoLineEnd(Utf8::DecodeCodePoint(text, end)) && end == last.index + last.length;
		}
		Word carried;
		if(carry)
		{
			carried = words.back();
			words.pop_back();
			lineWidth = carried.x;
		}
		word.y += lineHeight;
		word.x = 0;
		AdjustLine(lineBegin, lineWidth, false);
		if(carry)
		{
			carried.x = 0;
			carried.y = word.y;
			words.push_back(carried);
			word.x = font->Width(text.c_str() + carried.index, carried.length, '\0');
			lineWidth = word.x;
		}
	};
	// Store a word at the pen position, wrapping first if it would overflow.
	auto place = [&](size_t index, size_t length, bool canHang)
	{
		const int width = font->Width(text.c_str() + index, length, '\0');
		if(word.x + width > wrapWidth && word.x > 0 && !canHang)
			newLine();
		Word placed = word;
		placed.index = index;
		placed.length = length;
		words.push_back(placed);
		word.x += width;
		lineWidth = word.x;
	};

	size_t pos = 0;
	while(pos < text.length())
	{
		size_t charStart = pos;
		char32_t c = Utf8::DecodeCodePoint(text, pos);
		bool isWhitespace = c <= ' ';
		bool breakableCJK = !isWhitespace && IsCJK(c);

		// Whitespace or a CJK character ends the current word.
		if(traversingWord && (isWhitespace || breakableCJK))
		{
			traversingWord = false;
			place(word.index, charStart - word.index, false);
		}

		// If that whitespace was a newline, we must handle that, too.
		if(c == '\n')
		{
			// The next word will begin on a new line.
			word.y += lineHeight + paragraphBreak;
			word.x = 0;

			// Adjust the word spacings on the now-completed line.
			AdjustLine(lineBegin, lineWidth, true);
		}
		// Otherwise, whitespace just adds to the x position.
		else if(isWhitespace)
			word.x += Space(c);
		// Each CJK character is a word of its own; closing punctuation never starts a line.
		else if(breakableCJK)
			place(charStart, pos - charStart, IsNoLineStart(c));
		// If we've reached the start of a new word, remember where it begins.
		else if(!traversingWord)
		{
			traversingWord = true;
			word.index = charStart;
		}
	}

	// Handle the final word.
	if(traversingWord)
		place(word.index, text.length() - word.index, false);

	// Ensure the very last line is aligned if it hasn't been yet.
	if(lineBegin < words.size())
	{
		AdjustLine(lineBegin, lineWidth, true);
	}

	// Calculate the total wrapped height, including the last line.
	if(!words.empty())
	{
		height = words.back().y + lineHeight;
	}
}



void WrappedText::AdjustLine(size_t &lineBegin, int &lineWidth, bool isEnd)
{
	int wordCount = static_cast<int>(words.size() - lineBegin);
	int extraSpace = wrapWidth - lineWidth;

	if(lineWidth > longestLineWidth)
		longestLineWidth = lineWidth;

	// Figure out how much space is left over. Depending on the alignment, we
	// will add that space to the left, to the right, to both sides, or to the
	// space in between the words. Exception: the last line of a "justified"
	// paragraph is left aligned, not justified.
	if(alignment == Alignment::JUSTIFIED && !isEnd && wordCount > 1)
	{
		for(int i = 0; i < wordCount; ++i)
			words[lineBegin + i].x += extraSpace * i / (wordCount - 1);
	}
	else if(alignment == Alignment::CENTER || alignment == Alignment::RIGHT)
	{
		int shift = (alignment == Alignment::CENTER) ? extraSpace / 2 : extraSpace;
		for(int i = 0; i < wordCount; ++i)
			words[lineBegin + i].x += shift;
	}

	lineBegin = words.size();
	lineWidth = 0;
}



int WrappedText::Space(char c) const
{
	return (c == ' ') ? space : (c == '\t') ? tabWidth : 0;
}