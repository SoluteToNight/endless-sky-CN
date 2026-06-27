/* Font.cpp
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

#include "Font.h"

#include "Alignment.h"
#include "../Color.h"
#include "DisplayText.h"
#include "../GameData.h"
#include "../Point.h"
#include "../Preferences.h"
#include "../Screen.h"
#include "TextureAtlas.h"
#include "Truncate.h"
#include "Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace std;

namespace {
	bool showUnderlines = false;

	FT_Library ftLibrary = nullptr;

	void InitFreeType()
	{
		if(!ftLibrary)
			FT_Init_FreeType(&ftLibrary);
	}

	/// Shared VAO and VBO for batched glyph rendering.
	GLuint vao = 0;
	GLuint vbo = 0;

	GLint colorI = 0;
	GLint scaleI = 0;
	GLint vertI = 0;
	GLint texCoordI = 0;
}



void Font::Load(const vector<filesystem::path> &fontPaths, int size)
{
	InitFreeType();

	int renderSize = size * renderScale;

	atlas = make_unique<TextureAtlas>(ATLAS_WIDTH, ATLAS_HEIGHT);

	for(const auto &path : fontPaths)
	{
		FT_Face face = nullptr;
		if(FT_New_Face(ftLibrary, path.string().c_str(), 0, &face))
			continue;
		FT_Set_Pixel_Sizes(face, 0, renderSize);
		faces.push_back(face);
	}

	if(faces.empty())
		return;

	// Extract metrics from the primary font.
	FT_Face primary = faces[0];
	height = (primary->size->metrics.height >> 6) / renderScale;
	ascender = (primary->size->metrics.ascender >> 6) / renderScale;

	// Measure space width.
	FT_UInt spaceIndex = FT_Get_Char_Index(primary, ' ');
	if(spaceIndex)
	{
		FT_Load_Glyph(primary, spaceIndex, FT_LOAD_RENDER);
		space = (primary->glyph->advance.x >> 6) / renderScale;
	}

	widthEllipses = WidthRawString("...");
	SetUpShader();
}



const GlyphCache &Font::GetGlyph(char32_t codepoint) const
{
	static const GlyphCache empty;

	auto it = cache.find(codepoint);
	if(it != cache.end())
		return it->second;

	// Try each face in the font stack.
	for(auto &face : faces)
	{
		FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);
		if(!glyphIndex)
			continue;

		if(FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
			continue;

		FT_GlyphSlot slot = face->glyph;

		GlyphCache glyph;
		glyph.advance = (slot->advance.x >> 6) / renderScale;
		glyph.bearingX = slot->bitmap_left / renderScale;
		glyph.bearingY = slot->bitmap_top / renderScale;
		glyph.width = slot->bitmap.width / renderScale;
		glyph.height = slot->bitmap.rows / renderScale;
		glyph.bitmapW = slot->bitmap.width;
		glyph.bitmapH = slot->bitmap.rows;
		glyph.isWhitespace = (slot->bitmap.width == 0 || slot->bitmap.rows == 0);

		if(!glyph.isWhitespace && atlas)
		{
			int ax = 0, ay = 0;
			if(atlas->Allocate(glyph.bitmapW, glyph.bitmapH, &ax, &ay))
			{
				atlas->Upload(ax, ay, glyph.bitmapW, glyph.bitmapH, slot->bitmap.buffer);

				float u0 = static_cast<float>(ax) / atlas->Width();
				float v0 = static_cast<float>(ay) / atlas->Height();
				float u1 = static_cast<float>(ax + glyph.bitmapW) / atlas->Width();
				float v1 = static_cast<float>(ay + glyph.bitmapH) / atlas->Height();
				glyph.uvRect[0] = u0;
				glyph.uvRect[1] = v0;
				glyph.uvRect[2] = u1;
				glyph.uvRect[3] = v1;
			}
		}

		cache[codepoint] = glyph;
		return cache[codepoint];
	}

	return empty;
}



void Font::SetUpShader()
{
	shader = GameData::Shaders().Get("font");

	if(!vbo)
	{
		vertI = shader->Attrib("vert");
		texCoordI = shader->Attrib("texCoordIn");

		glUseProgram(shader->Object());
		glUniform1i(shader->Uniform("tex"), 0);
		glUseProgram(0);

		if(OpenGL::HasVaoSupport())
		{
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);
		}

		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);

		if(OpenGL::HasVaoSupport())
		{
			constexpr auto stride = 4 * sizeof(GLfloat);
			glEnableVertexAttribArray(vertI);
			glVertexAttribPointer(vertI, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
			glEnableVertexAttribArray(texCoordI);
			glVertexAttribPointer(texCoordI, 2, GL_FLOAT, GL_FALSE,
				stride, reinterpret_cast<const GLvoid *>(2 * sizeof(GLfloat)));
		}

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		if(OpenGL::HasVaoSupport())
			glBindVertexArray(0);

		colorI = shader->Uniform("color");
		scaleI = shader->Uniform("scale");
	}

	screenWidth = 0;
	screenHeight = 0;
}



void Font::Draw(const DisplayText &text, const Point &point, const Color &color) const
{
	DrawAliased(text, round(point.X()), round(point.Y()), color);
}



void Font::DrawAliased(const DisplayText &text, double x, double y, const Color &color) const
{
	int width = -1;
	const string truncText = TruncateText(text, width);
	const auto &layout = text.GetLayout();
	if(width >= 0)
	{
		if(layout.align == Alignment::CENTER)
			x += (layout.width - width) / 2;
		else if(layout.align == Alignment::RIGHT)
			x += layout.width - width;
	}
	DrawAliased(truncText, x, y, color);
}



void Font::Draw(const string &str, const Point &point, const Color &color) const
{
	DrawAliased(str, round(point.X()), round(point.Y()), color);
}



void Font::DrawAliased(const string &str, double x, double y, const Color &color) const
{
	if(!shader || faces.empty())
		return;

	glUseProgram(shader->Object());
	glBindTexture(GL_TEXTURE_2D, atlas->Texture());
	if(OpenGL::HasVaoSupport())
		glBindVertexArray(vao);
	else
	{
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		constexpr auto stride = 4 * sizeof(GLfloat);
		glEnableVertexAttribArray(vertI);
		glVertexAttribPointer(vertI, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
		glEnableVertexAttribArray(texCoordI);
		glVertexAttribPointer(texCoordI, 2, GL_FLOAT, GL_FALSE,
			stride, reinterpret_cast<const GLvoid *>(2 * sizeof(GLfloat)));
	}

	glUniform4fv(colorI, 1, color.Get());

	if(Screen::Width() != screenWidth || Screen::Height() != screenHeight)
	{
		screenWidth = Screen::Width();
		screenHeight = Screen::Height();
		scale[0] = 2.f / screenWidth;
		scale[1] = -2.f / screenHeight;
	}
	glUniform2fv(scaleI, 1, scale);

	// Build vertex data for all glyphs.
	vector<GLfloat> vertices;
	float textX = static_cast<float>(x - 1.);
	float textY = static_cast<float>(y);
	bool underlineChar = false;

	size_t pos = 0;
	while(pos < str.length())
	{
		char32_t c = Utf8::DecodeCodePoint(str, pos);

		if(c == '_')
		{
			underlineChar = showUnderlines;
			continue;
		}

		if(c == '\r' || c == '\n')
			continue;

		const GlyphCache &glyph = GetGlyph(c);
		if(glyph.isWhitespace)
		{
			textX += space;
			continue;
		}

		float gx = textX + glyph.bearingX;
		float gy = textY + ascender - glyph.bearingY;
		float gw = static_cast<float>(glyph.bitmapW) / renderScale;
		float gh = static_cast<float>(glyph.bitmapH) / renderScale;

		float u0 = glyph.uvRect[0];
		float v0 = glyph.uvRect[1];
		float u1 = glyph.uvRect[2];
		float v1 = glyph.uvRect[3];

		// Two triangles per glyph.
		vertices.insert(vertices.end(), {gx, gy, u0, v0});
		vertices.insert(vertices.end(), {gx, gy + gh, u0, v1});
		vertices.insert(vertices.end(), {gx + gw, gy, u1, v0});
		vertices.insert(vertices.end(), {gx + gw, gy, u1, v0});
		vertices.insert(vertices.end(), {gx, gy + gh, u0, v1});
		vertices.insert(vertices.end(), {gx + gw, gy + gh, u1, v1});

		// Draw underline if needed.
		if(underlineChar)
		{
			const GlyphCache &usGlyph = GetGlyph('_');
			if(!usGlyph.isWhitespace)
			{
				float ux = textX + usGlyph.bearingX;
				float uy = textY + ascender - usGlyph.bearingY;
				float uw = static_cast<float>(usGlyph.bitmapW) / renderScale;
				float uh = static_cast<float>(usGlyph.bitmapH) / renderScale;

				float uu0 = usGlyph.uvRect[0];
				float uv0 = usGlyph.uvRect[1];
				float uu1 = usGlyph.uvRect[2];
				float uv1 = usGlyph.uvRect[3];

				vertices.insert(vertices.end(), {ux, uy, uu0, uv0});
				vertices.insert(vertices.end(), {ux, uy + uh, uu0, uv1});
				vertices.insert(vertices.end(), {ux + uw, uy, uu1, uv0});
				vertices.insert(vertices.end(), {ux + uw, uy, uu1, uv0});
				vertices.insert(vertices.end(), {ux, uy + uh, uu0, uv1});
				vertices.insert(vertices.end(), {ux + uw, uy + uh, uu1, uv1});
			}
			underlineChar = false;
		}

		textX += glyph.advance;
	}

	// Upload and draw.
	if(!vertices.empty())
	{
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat),
			vertices.data(), GL_STREAM_DRAW);

		if(!OpenGL::HasVaoSupport())
		{
			constexpr auto stride = 4 * sizeof(GLfloat);
			glEnableVertexAttribArray(vertI);
			glVertexAttribPointer(vertI, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
			glEnableVertexAttribArray(texCoordI);
			glVertexAttribPointer(texCoordI, 2, GL_FLOAT, GL_FALSE,
				stride, reinterpret_cast<const GLvoid *>(2 * sizeof(GLfloat)));
		}

		glDrawArrays(GL_TRIANGLES, 0, vertices.size() / 4);
	}

	if(OpenGL::HasVaoSupport())
		glBindVertexArray(0);
	else
	{
		glDisableVertexAttribArray(vertI);
		glDisableVertexAttribArray(texCoordI);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
	glUseProgram(0);
}



int Font::Width(const string &str, char after) const
{
	return WidthRawString(str.c_str(), after);
}



int Font::Width(const char *str, size_t length, char after) const
{
	string substr(str, length);
	return WidthRawString(substr.c_str(), after);
}



int Font::FormattedWidth(const DisplayText &text, char after) const
{
	int width = -1;
	const string truncText = TruncateText(text, width);
	return width < 0 ? WidthRawString(truncText.c_str(), after) : width;
}



int Font::Height() const noexcept
{
	return height;
}



int Font::Space() const noexcept
{
	return space;
}



void Font::ShowUnderlines(bool show) noexcept
{
	showUnderlines = show || Preferences::Has("Always underline shortcuts");
}



int Font::WidthRawString(const char *str, char after) const noexcept
{
	int width = 0;
	size_t pos = 0;
	string_view sv(str);

	while(pos < sv.length())
	{
		char32_t c = Utf8::DecodeCodePoint(sv, pos);
		if(c == '_')
			continue;

		if(c == '\r' || c == '\n')
			continue;

		const GlyphCache &glyph = GetGlyph(c);
		if(glyph.isWhitespace)
			width += space;
		else
			width += glyph.advance;
	}

	// Add advance for the 'after' character if provided.
	if(after && after != ' ')
	{
		char32_t afterC = static_cast<unsigned char>(after);
		const GlyphCache &afterGlyph = GetGlyph(afterC);
		if(!afterGlyph.isWhitespace)
			width += afterGlyph.advance;
	}

	return width;
}



string Font::TruncateText(const DisplayText &text, int &width) const
{
	width = -1;
	const auto &layout = text.GetLayout();
	const string &str = text.GetText();
	if(layout.width < 0 || (layout.align == Alignment::LEFT && layout.truncate == Truncate::NONE))
		return str;
	width = layout.width;
	switch(layout.truncate)
	{
		case Truncate::NONE:
			width = WidthRawString(str.c_str());
			return str;
		case Truncate::FRONT:
			return TruncateFront(str, width);
		case Truncate::MIDDLE:
			return TruncateMiddle(str, width);
		case Truncate::BACK:
		default:
			return TruncateBack(str, width);
	}
}



string Font::TruncateBack(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, const vector<size_t> &offsets, int charCount)
		{
			return str.substr(0, offsets[charCount]) + "...";
		});
}



string Font::TruncateFront(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, const vector<size_t> &offsets, int charCount)
		{
			return "..." + str.substr(offsets[offsets.size() - 1 - charCount]);
		});
}



string Font::TruncateMiddle(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, const vector<size_t> &offsets, int charCount)
		{
			int frontCount = (charCount + 1) / 2;
			int backCount = charCount / 2;
			size_t frontEnd = offsets[frontCount];
			size_t backStart = offsets[offsets.size() - 1 - backCount];
			return str.substr(0, frontEnd) + "..." + str.substr(backStart);
		});
}



string Font::TruncateEndsOrMiddle(const string &str, int &width,
	function<string(const string &, const vector<size_t> &, int)> getResultString) const
{
	int firstWidth = WidthRawString(str.c_str());
	if(firstWidth <= width)
	{
		width = firstWidth;
		return str;
	}

	// Pre-compute UTF-8 character boundary offsets.
	vector<size_t> offsets;
	size_t pos = 0;
	while(pos < str.length())
	{
		offsets.push_back(pos);
		Utf8::DecodeCodePoint(str, pos);
	}
	offsets.push_back(str.length());

	int workingChars = 0;
	int workingWidth = 0;

	int low = 0, high = offsets.size() - 2;
	while(low <= high)
	{
		int nextChars = (low + high) / 2;
		int nextWidth = WidthRawString(getResultString(str, offsets, nextChars).c_str());
		if(nextWidth <= width)
		{
			if(nextChars > workingChars)
			{
				workingChars = nextChars;
				workingWidth = nextWidth;
			}
			low = nextChars + (nextChars == low);
		}
		else
			high = nextChars - 1;
	}
	width = workingWidth;
	return getResultString(str, offsets, workingChars);
}