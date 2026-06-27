/* TextureAtlas.cpp
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

#include "TextureAtlas.h"

#include <cstring>

using namespace std;



TextureAtlas::TextureAtlas(int width, int height)
	: atlasWidth(width), atlasHeight(height)
{
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Use GL_RED format for FreeType bitmap data.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasWidth, atlasHeight, 0,
		GL_RED, GL_UNSIGNED_BYTE, nullptr);
}



TextureAtlas::~TextureAtlas()
{
	if(texture)
		glDeleteTextures(1, &texture);
}



bool TextureAtlas::Allocate(int width, int height, int *x, int *y)
{
	// Add 1px padding to prevent bilinear sampling bleed.
	width += 2;
	height += 2;

	// Check if we need to start a new row.
	if(currentX + width > atlasWidth)
	{
		currentX = 0;
		currentY += rowHeight;
		rowHeight = 0;
	}

	// Check if we have enough vertical space.
	if(currentY + height > atlasHeight)
		return false;

	*x = currentX + 1;
	*y = currentY + 1;
	currentX += width;
	if(height > rowHeight)
		rowHeight = height;

	return true;
}



void TextureAtlas::Upload(int x, int y, int width, int height, const unsigned char *data)
{
	glBindTexture(GL_TEXTURE_2D, texture);

	// FreeType uses 1-byte alignment.
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height,
		GL_RED, GL_UNSIGNED_BYTE, data);
}



GLuint TextureAtlas::Texture() const
{
	return texture;
}



int TextureAtlas::Width() const
{
	return atlasWidth;
}



int TextureAtlas::Height() const
{
	return atlasHeight;
}