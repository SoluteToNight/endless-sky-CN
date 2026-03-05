/* Font.h
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

#pragma once

#include "../shader/Shader.h"

#include "../opengl.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "GlyphCache.h"
#include "TextureAtlas.h"

class Color;
class DisplayText;
class ImageBuffer;
class Point;

// Class for drawing text in OpenGL using FreeType.
class Font {
public:
  Font() noexcept;
  ~Font();

  // No copying.
  Font(const Font &) = delete;
  Font &operator=(const Font &) = delete;

  Font(Font &&) noexcept = default;
  Font &operator=(Font &&) noexcept = default;

  // Note: First path is the primary font, following paths are fallbacks.
  void Load(const std::vector<std::filesystem::path> &fontPaths, int size);

  // Draw a text string, subject to the given layout and truncation strategy.
  void Draw(const DisplayText &text, const Point &point,
            const Color &color) const;
  void DrawAliased(const DisplayText &text, double x, double y,
                   const Color &color) const;
  // Draw the given text string, e.g. post-formatting (or without regard to
  // formatting).
  void Draw(std::string_view str, const Point &point, const Color &color) const;
  void DrawAliased(std::string_view str, double x, double y,
                   const Color &color) const;

  // Determine the string's width, without considering formatting.
  int Width(const std::string &str, char after = ' ') const;
  int Width(const std::string &str, size_t len, char after = ' ') const;
  // Get the width of the text while accounting for the desired layout and
  // truncation strategy.
  int FormattedWidth(const DisplayText &text, char after = ' ') const;

  int Height() const noexcept;

  int Space() const noexcept;

  static void ShowUnderlines(bool show) noexcept;

private:
  // Find and load a glyph from the font stack into the cache and atlas.
  const GlyphInfo &GetGlyph(char32_t codepoint) const;

  void SetUpShader();

  int WidthRawString(const char *str, char after = ' ') const noexcept;
  int WidthRawString(const char *str, size_t len,
                     char after = ' ') const noexcept;

  std::string TruncateText(const DisplayText &text, int &width) const;
  std::string TruncateBack(const std::string &str, int &width) const;
  std::string TruncateFront(const std::string &str, int &width) const;
  std::string TruncateMiddle(const std::string &str, int &width) const;

  std::string TruncateEndsOrMiddle(
      const std::string &str, int &width,
      std::function<std::string(const std::string &,
                                const std::vector<size_t> &, int)>
          getResultString) const;

private:
  const Shader *shader = nullptr;

  int height = 0;
  int space = 0;
  int size = 14;
  int renderScale = 2;
  int ascender = 0;
  mutable int screenWidth = 0;
  mutable int screenHeight = 0;
  mutable GLfloat scale[2]{0.f, 0.f};

  mutable std::unique_ptr<TextureAtlas> atlas;
  mutable std::unordered_map<char32_t, GlyphInfo> cache;

  std::vector<FT_Face> faces;

  int widthEllipses = 0;
};
