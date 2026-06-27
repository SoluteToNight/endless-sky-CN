/* TextureAtlas.h
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

#include "../opengl.h"



// Dynamic OpenGL texture atlas for glyph rendering.
class TextureAtlas {
public:
	TextureAtlas(int width, int height);
	~TextureAtlas();

	// Allocate space for a glyph in the atlas.
	// Returns false if there is not enough space.
	bool Allocate(int width, int height, int *x, int *y);

	// Upload glyph bitmap data to the atlas.
	void Upload(int x, int y, int width, int height, const unsigned char *data);

	// Get the texture ID.
	GLuint Texture() const;

	// Get the atlas dimensions.
	int Width() const;
	int Height() const;

private:
	GLuint texture = 0;
	int atlasWidth = 0;
	int atlasHeight = 0;
	int currentX = 0;
	int currentY = 0;
	int rowHeight = 0;
};