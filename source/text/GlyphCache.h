/* GlyphCache.h
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

#pragma once



// Metadata for a single rendered glyph in the texture atlas.
class GlyphCache {
public:
	float uvRect[4] = {0.f, 0.f, 0.f, 0.f};
	float advance = 0.f;
	float bearingX = 0.f;
	float bearingY = 0.f;
	float width = 0.f;
	float height = 0.f;
	int bitmapW = 0;
	int bitmapH = 0;
	bool isWhitespace = false;
};