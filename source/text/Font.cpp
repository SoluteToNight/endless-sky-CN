/* Font.cpp
Copyright (c) 2014-2020 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Font.h"

#include "../Color.h"
#include "../GameData.h"
#include "../Point.h"
#include "../Preferences.h"
#include "../Screen.h"
#include "Alignment.h"
#include "DisplayText.h"
#include "Truncate.h"
#include "Translator.h"
#include "Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

using namespace std;

namespace {
bool showUnderlines = false;

/// Shared VAO and VBO quad (0,0) -> (1,1)
GLuint vao = 0;
GLuint vbo = 0;

GLint colorI = 0;
GLint scaleI = 0;

GLint vertI;
GLint texCoordI;

void EnableAttribArrays() {
  // Connect the xy and uv to the attributes.
  constexpr auto stride = 4 * sizeof(GLfloat);
  glEnableVertexAttribArray(vertI);
  glVertexAttribPointer(vertI, 2, GL_FLOAT, GL_FALSE, stride, nullptr);

  glEnableVertexAttribArray(texCoordI);
  glVertexAttribPointer(texCoordI, 2, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const GLvoid *>(2 * sizeof(GLfloat)));
}

FT_Library ftLibrary = nullptr;

void InitFreeType() {
  if (!ftLibrary) {
    if (FT_Init_FreeType(&ftLibrary)) {
      cerr << "Could not initialize freetype library." << endl;
    }
  }
}
} // namespace

Font::Font() noexcept { InitFreeType(); }

Font::~Font() {
  for (auto face : faces)
    FT_Done_Face(face);
}

void Font::Load(const vector<filesystem::path> &fontPaths, int size) {
  this->size = size;

  // Render at renderScale times the nominal size for high-DPI quality.
  // Original PNGs used ~2x oversampling (14px -> 32px tall, 18px -> 36px tall).
  int renderSize = size * renderScale;

  for (const auto &path : fontPaths) {
    FT_Face face;
    if (FT_New_Face(ftLibrary, path.string().c_str(), 0, &face)) {
      cerr << "Failed to load font " << path << endl;
      continue;
    }

    FT_Set_Pixel_Sizes(face, 0, renderSize);
    faces.push_back(face);
  }

  if (faces.empty())
    return;

  // Atlas size to hold several thousand glyphs at the supersampled resolution.
  atlas = make_unique<TextureAtlas>(4096, 4096);

  // Pre-determine line height, ascender, and space width based on primary font.
  // Metrics from FreeType are in 26.6 fixed-point hi-res space.
  // Use ascender-descender (no linegap) to match original PNG cell height.
  FT_Face primary = faces.front();
  float fScale = static_cast<float>(renderScale);
  int ftAscender = primary->size->metrics.ascender >> 6;
  int ftDescender = primary->size->metrics.descender >> 6; // negative
  height = static_cast<int>((ftAscender - ftDescender) / fScale);
  ascender = static_cast<int>(ftAscender / fScale);
  const auto &spaceGlyph = GetGlyph(' ');
  space = static_cast<int>(spaceGlyph.advance);
  if (space <= 0)
    space = size / 3;

  SetUpShader();
  widthEllipses = WidthRawString("...");
}

void Font::Draw(const DisplayText &text, const Point &point,
                const Color &color) const {
  DrawAliased(text, round(point.X()), round(point.Y()), color);
}

void Font::DrawAliased(const DisplayText &text, double x, double y,
                       const Color &color) const {
  int width = -1;
  const string truncText = TruncateText(text, width);
  const auto &layout = text.GetLayout();
  if (width >= 0) {
    if (layout.align == Alignment::CENTER)
      x += (layout.width - width) / 2;
    else if (layout.align == Alignment::RIGHT)
      x += layout.width - width;
  }
  DrawAliased(truncText, x, y, color);
}

void Font::Draw(std::string_view str, const Point &point,
                const Color &color) const {
  DrawAliased(str, round(point.X()), round(point.Y()), color);
}

void Font::DrawAliased(std::string_view str, double x, double y,
                       const Color &color) const {
  str = Translator::Get(str);
  if (!atlas)
    return;

  glUseProgram(shader->Object());
  atlas->Bind();

  if (OpenGL::HasVaoSupport())
    glBindVertexArray(vao);

  glBindBuffer(GL_ARRAY_BUFFER, vbo);

  if (!OpenGL::HasVaoSupport())
    EnableAttribArrays();

  glUniform4fv(colorI, 1, color.Get());

  // Update the scale, only if the screen size has changed.
  if (Screen::Width() != screenWidth || Screen::Height() != screenHeight) {
    screenWidth = Screen::Width();
    screenHeight = Screen::Height();
    scale[0] = 2.f / screenWidth;
    scale[1] = -2.f / screenHeight;
  }
  glUniform2fv(scaleI, 1, scale);

  GLfloat textPos[2] = {static_cast<float>(x - 1.), static_cast<float>(y)};

  bool underlineChar = false;
  const GlyphInfo &underscoreGlyph = GetGlyph('_');

  vector<GLfloat> vertexData;
  // Pre-allocate enough space for a typical string.
  // 6 vertices per glyph, 4 floats per vertex (x,y,u,v).
  vertexData.reserve(str.length() * 24);

  size_t pos = 0;
  while (pos < str.length()) {
    char32_t codepoint = Utf8::DecodeCodePoint(str, pos);

    if (codepoint == '_') {
      underlineChar = showUnderlines;
      continue;
    }

    if (codepoint == 0 || codepoint == static_cast<char32_t>(-1))
      continue;

    const GlyphInfo &info = GetGlyph(codepoint);

    if (!info.isWhitespace && info.bitmapW > 0 && info.bitmapH > 0) {
      float px = textPos[0] + info.bearingX;
      float py = textPos[1] + (ascender - info.bearingY);
      float pw = static_cast<float>(info.width);
      float ph = static_cast<float>(info.height);
      float u = info.uvRect[0];
      float v = info.uvRect[1];
      float uw = info.uvRect[2];
      float vh = info.uvRect[3];

      vertexData.insert(vertexData.end(),
                        {// Triangle 1
                         px, py, u, v, px, py + ph, u, v + vh, px + pw, py,
                         u + uw, v,
                         // Triangle 2
                         px + pw, py, u + uw, v, px, py + ph, u, v + vh,
                         px + pw, py + ph, u + uw, v + vh});
    }

    textPos[0] += info.advance;

    if (underlineChar) {
      if (underscoreGlyph.bitmapW > 0 && underscoreGlyph.bitmapH > 0) {
        float aspect = info.advance / max(underscoreGlyph.advance, 1.f);

        float px = textPos[0] - info.advance + underscoreGlyph.bearingX;
        float py = textPos[1] + (ascender - underscoreGlyph.bearingY);
        float pw = static_cast<float>(underscoreGlyph.width) * aspect;
        float ph = static_cast<float>(underscoreGlyph.height);
        float u = underscoreGlyph.uvRect[0];
        float v = underscoreGlyph.uvRect[1];
        float uw = underscoreGlyph.uvRect[2];
        float vh = underscoreGlyph.uvRect[3];

        vertexData.insert(vertexData.end(),
                          {px,      py,     u,       v,       px,     py + ph,
                           u,       v + vh, px + pw, py,      u + uw, v,
                           px + pw, py,     u + uw,  v,       px,     py + ph,
                           u,       v + vh, px + pw, py + ph, u + uw, v + vh});
      }
      underlineChar = false;
    }
  }

  if (!vertexData.empty()) {
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(GLfloat),
                 vertexData.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertexData.size() / 4);
  }

  if (OpenGL::HasVaoSupport())
    glBindVertexArray(0);
  else {
    glDisableVertexAttribArray(vertI);
    glDisableVertexAttribArray(texCoordI);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
}

int Font::Width(const string &str, char after) const {
  return WidthRawString(str.c_str(), string::npos, after);
}
int Font::Width(const string &str, size_t len, char after) const {
  return WidthRawString(str.c_str(), len, after);
}

int Font::FormattedWidth(const DisplayText &text, char after) const {
  int width = -1;
  const string truncText = TruncateText(text, width);
  return width < 0 ? WidthRawString(truncText.c_str(), after) : width;
}

int Font::Height() const noexcept { return height; }

int Font::Space() const noexcept { return space; }

void Font::ShowUnderlines(bool show) noexcept {
  showUnderlines = show || Preferences::Has("Always underline shortcuts");
}

const GlyphInfo &Font::GetGlyph(char32_t codepoint) const {
  auto it = cache.find(codepoint);
  if (it != cache.end())
    return it->second;

  GlyphInfo info{};
  if (codepoint == ' ' || codepoint == '\n' || codepoint == '\t')
    info.isWhitespace = true;

  FT_Face faceToUse = nullptr;
  FT_UInt glyphIndex = 0;

  for (auto face : faces) {
    glyphIndex = FT_Get_Char_Index(face, codepoint);
    if (glyphIndex != 0) {
      faceToUse = face;
      break;
    }
  }

  if (!faceToUse && !faces.empty())
    faceToUse = faces.front();

  if (faceToUse) {
    if (FT_Load_Glyph(faceToUse, glyphIndex, FT_LOAD_RENDER) == 0) {
      FT_GlyphSlot slot = faceToUse->glyph;

      // Store the full hi-res bitmap dimensions for rendering.
      info.bitmapW = slot->bitmap.width;
      info.bitmapH = slot->bitmap.rows;
      // Store logical (scaled-down) metrics for text layout using float
      // division.
      float fScale = static_cast<float>(renderScale);
      info.width = static_cast<float>(slot->bitmap.width) / fScale;
      info.height = static_cast<float>(slot->bitmap.rows) / fScale;
      info.bearingX = static_cast<float>(slot->bitmap_left) / fScale;
      info.bearingY = static_cast<float>(slot->bitmap_top) / fScale;
      info.advance = static_cast<float>(slot->advance.x >> 6) / fScale;

      if (info.advance == 0.f && info.isWhitespace)
        info.advance = static_cast<float>(size) / 3.f;

      if (info.bitmapW > 0 && info.bitmapH > 0) {
        int startX, startY;
        if (atlas->Allocate(info.bitmapW, info.bitmapH, startX, startY)) {
          atlas->Upload(startX, startY, info.bitmapW, info.bitmapH,
                        slot->bitmap.buffer);
          info.uvRect[0] = static_cast<float>(startX) / atlas->Width();
          info.uvRect[1] = static_cast<float>(startY) / atlas->Height();
          info.uvRect[2] = static_cast<float>(info.bitmapW) / atlas->Width();
          info.uvRect[3] = static_cast<float>(info.bitmapH) / atlas->Height();
        }
      }
    }
  }

  // Double buffering map insertion is safe if initialized.
  cache[codepoint] = info;
  return cache[codepoint];
}

void Font::SetUpShader() {
  shader = GameData::Shaders().Get("font");
  // Initialize the shared parameters only once
  if (!vbo) {
    vertI = shader->Attrib("vert");
    texCoordI = shader->Attrib("texCoordIn");

    glUseProgram(shader->Object());
    glUniform1i(shader->Uniform("tex"), 0);
    glUseProgram(0);

    // Create the VAO and VBO.
    if (OpenGL::HasVaoSupport()) {
      glGenVertexArrays(1, &vao);
      glBindVertexArray(vao);
    }

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    if (OpenGL::HasVaoSupport())
      EnableAttribArrays();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (OpenGL::HasVaoSupport())
      glBindVertexArray(0);

    colorI = shader->Uniform("color");
    scaleI = shader->Uniform("scale");
  }

  // We must update the screen size next time we draw.
  screenWidth = 0;
  screenHeight = 0;
}

int Font::WidthRawString(const char *str, char after) const noexcept {
  return WidthRawString(str, string::npos, after);
}

int Font::WidthRawString(const char *str, size_t len,
                         char after) const noexcept {
  float width = 0.f;

  string_view s(str, (len == string::npos) ? strlen(str) : len);
  s = Translator::Get(s);
  size_t pos = 0;
  // DecodeCodePoint takes std::string, so let's use a loop over bytes if we
  // must, or construct a temporary string to avoid changing
  // Utf8::DecodeCodePoint signature. Given Endless Sky's Utf8::DecodeCodePoint
  // takes `const string&`, we have to create a string for now, but we only copy
  // `len` characters.
  string s_copy(s);
  while (pos < s_copy.length()) {
    char32_t codepoint = Utf8::DecodeCodePoint(s_copy, pos);
    if (codepoint == '_')
      continue;
    const GlyphInfo &info = GetGlyph(codepoint);
    width += info.advance;
  }

  if (after != '\0')
    width += GetGlyph(after).advance;

  return static_cast<int>(width + 0.5f);
}

// Param width will be set to the width of the return value, unless the layout
// width is negative.
string Font::TruncateText(const DisplayText &text, int &width) const {
  width = -1;
  const auto &layout = text.GetLayout();
  const string str { Translator::Get(text.GetText()) };
  if (layout.width < 0 ||
      (layout.align == Alignment::LEFT && layout.truncate == Truncate::NONE))
    return str;
  width = layout.width;
  switch (layout.truncate) {
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

string Font::TruncateBack(const string &str, int &width) const {
  return TruncateEndsOrMiddle(
      str, width,
      [](const string &str, const vector<size_t> &offsets, int charCount) {
        return str.substr(0, offsets[charCount]) + "...";
      });
}

string Font::TruncateFront(const string &str, int &width) const {
  return TruncateEndsOrMiddle(
      str, width,
      [](const string &str, const vector<size_t> &offsets, int charCount) {
        int totalChars = offsets.size() - 1;
        return "..." + str.substr(offsets[totalChars - charCount]);
      });
}

string Font::TruncateMiddle(const string &str, int &width) const {
  return TruncateEndsOrMiddle(
      str, width,
      [](const string &str, const vector<size_t> &offsets, int charCount) {
        int totalChars = offsets.size() - 1;
        int leftChars = (charCount + 1) / 2;
        int rightChars = charCount / 2;
        return str.substr(0, offsets[leftChars]) + "..." +
               str.substr(offsets[totalChars - rightChars]);
      });
}

string Font::TruncateEndsOrMiddle(
    const string &str, int &width,
    function<string(const string &, const vector<size_t> &, int)>
        getResultString) const {
  int firstWidth = WidthRawString(str.c_str());
  if (firstWidth <= width) {
    width = firstWidth;
    return str;
  }

  // Pre-calculate UTF-8 character boundaries to avoid slicing multi-byte chars.
  vector<size_t> offsets;
  size_t pos = 0;
  while (pos < str.length()) {
    offsets.push_back(pos);
    Utf8::DecodeCodePoint(str, pos);
  }
  offsets.push_back(str.length());

  int workingChars = 0;
  int workingWidth = 0;

  // Binary search based on actual UTF-8 character count instead of bytes.
  int low = 0, high = offsets.size() - 1;
  while (low <= high) {
    int nextChars = (low + high) / 2;
    int nextWidth =
        WidthRawString(getResultString(str, offsets, nextChars).c_str());
    if (nextWidth <= width) {
      if (nextChars > workingChars) {
        workingChars = nextChars;
        workingWidth = nextWidth;
      }
      low = nextChars + (nextChars == low);
    } else {
      high = nextChars - 1;
    }
  }
  width = workingWidth;
  return getResultString(str, offsets, workingChars);
}
