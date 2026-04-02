/* WrappedText.cpp
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

#include "WrappedText.h"

#include "DisplayText.h"
#include "Font.h"
#include "Translator.h"

#include "Utf8.h"

#include <cstring>
#include <string_view>

using namespace std;

// Helper to determine if a character is a CJK ideograph/character
// which can be broken after even without whitespace.
static bool IsCJK(char32_t c) {
  // Simplified ranges for CJK Unified Ideographs, Hiragana, Katakana, Hangul,
  // etc.
  return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7A3) ||
         (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF);
}

WrappedText::WrappedText(const Font &font) { SetFont(font); }

void WrappedText::SetAlignment(Alignment align) { alignment = align; }

// Set the truncate mode.
void WrappedText::SetTruncate(Truncate trunc) { truncate = trunc; }

// Set the wrap width. This does not include any margins.
int WrappedText::WrapWidth() const { return wrapWidth; }

void WrappedText::SetWrapWidth(int width) { wrapWidth = width; }

// Set the font to use. This will also set sensible defaults for the tab
// width, line height, and paragraph break. You must specify the wrap width
// and the alignment separately.
void WrappedText::SetFont(const Font &font) {
  this->font = &font;

  space = font.Space();
  SetTabWidth(4 * space);
  SetLineHeight(font.Height() * 120 / 100);
  SetParagraphBreak(font.Height() * 40 / 100);
}

// Set the width in pixels of a single '\t' character.
int WrappedText::TabWidth() const { return tabWidth; }

void WrappedText::SetTabWidth(int width) { tabWidth = width; }

// Set the height in pixels of one line of text within a paragraph.
int WrappedText::LineHeight() const { return lineHeight; }

void WrappedText::SetLineHeight(int height) { lineHeight = height; }

// Set the extra spacing in pixels to be added in between paragraphs.
int WrappedText::ParagraphBreak() const { return paragraphBreak; }

void WrappedText::SetParagraphBreak(int height) { paragraphBreak = height; }

// Get the word positions when wrapping the given text. The coordinates
// always begin at (0, 0).
void WrappedText::Wrap(const string &str) {
  const string_view translated = Translator::Get(str);
  SetText(translated.data(), translated.length());

  Wrap();
}

void WrappedText::Wrap(const char *str) {
  const string_view translated = Translator::Get(str ? string_view(str) : string_view());
  SetText(translated.data(), translated.length());

  Wrap();
}

/// Get the height of the wrapped text.
/// With trailingBreak, include a paragraph break after the text.
int WrappedText::Height(bool trailingBreak) const {
  if (!height)
    return 0;
  return height + trailingBreak * paragraphBreak;
}

// Return the width of the longest line of the wrapped text.
int WrappedText::LongestLineWidth() const { return longestLineWidth; }

// Draw the text.
void WrappedText::Draw(const Point &topLeft, const Color &color) const {
  if (words.empty())
    return;

  if (truncate == Truncate::NONE)
    for (const Word &w : words)
      font->Draw(std::string_view(text.c_str() + w.Index(), w.Length()),
                 w.Pos() + topLeft, color);
  else {
    // Currently, we only apply truncation to a line if it contains a single
    // word.
    int h = words[0].y - 1;
    for (size_t i = 0; i < words.size(); ++i) {
      const Word &w = words[i];
      if (h == w.y && (i != words.size() - 1 && w.y == words[i + 1].y))
        font->Draw(std::string_view(text.c_str() + w.Index(), w.Length()),
                   w.Pos() + topLeft, color);
      else
        font->Draw({std::string(text.c_str() + w.Index(), w.Length()),
                    {wrapWidth, truncate}},
                   w.Pos() + topLeft, color);
      h = w.y;
    }
  }
}

size_t WrappedText::Word::Index() const { return index; }
size_t WrappedText::Word::Length() const { return length; }

Point WrappedText::Word::Pos() const { return Point(x, y); }

void WrappedText::SetText(const char *it, size_t length) {
  // Clear any previous word-wrapping data. It becomes invalid as soon as the
  // underlying text buffer changes.
  words.clear();

  // Reallocate that buffer.
  text.assign(it, length);
}

void WrappedText::Wrap() {
  height = 0;
  longestLineWidth = 0;

  if (text.empty() || !font)
    return;

  // Do this as a finite state machine.
  Word word;
  bool traversingWord = false;
  bool currentLineHasWords = false;

  // Keep track of how wide the current line is. This is just so we know how
  // much extra space must be allotted by the alignment code.
  int lineWidth = 0;
  // This is the index in the "words" vector of the first word on this line.
  size_t lineBegin = 0;

  size_t pos = 0;
  size_t wordStartPos = 0;

  while (pos < text.length()) {
    size_t charStart = pos;
    char32_t c = Utf8::DecodeCodePoint(text, pos);

    bool isWhitespace = (c <= ' ');
    bool isNewline = (c == '\n');
    bool breakableCJK = IsCJK(c);

    // If we are traversing a word, check if it should end BEFORE this
    // character.
    if (traversingWord && (isWhitespace || breakableCJK)) {
      // If it's a breakable CJK character but it is the ONLY character in the
      // word so far, we don't end the word yet (we let the CJK character be the
      // word itself).
      if (breakableCJK && wordStartPos == charStart) {
        // Keep going, the word is just this CJK character.
      } else {
        traversingWord = false;

        // Measure the word from wordStartPos up to charStart
        size_t wordLen = charStart - wordStartPos;
        const int width = font->Width(text.c_str() + word.index, wordLen, '\0');

        if (word.x + width > wrapWidth && word.x > 0) {
          // Move to next line (only if we are not already at the start of a
          // line)
          word.y += lineHeight;
          word.x = 0;
          AdjustLine(lineBegin, lineWidth, false);
        }

        words.push_back(word);
        words.back().length = wordLen;
        word.x += width;
        lineWidth = word.x;

        // If the reason we broke the word was a CJK character,
        // we must immediately start a new word WITH this character.
        if (breakableCJK && !isWhitespace) {
          traversingWord = true;
          currentLineHasWords = true;
          word.index = charStart;
          wordStartPos = charStart;
          // Note: we don't advance the position, because the character has
          // already been consumed by DecodeCodePoint, but it belongs to this
          // newly started word.
        }
      }
    }

    // Handle the character that just ended a word, or characters outside words.
    if (isNewline) {
      // The next word will begin on a newline.
      word.y += lineHeight + paragraphBreak;
      word.x = 0;
      AdjustLine(lineBegin, lineWidth, true);
      currentLineHasWords = false;
      traversingWord = false; // Just to be safe
    } else if (isWhitespace && c != '\n') {
      // Whitespace just adds to the x position of the NEXT word.
      word.x += Space(c);
    } else if (!traversingWord) {
      // Start of a new word.
      traversingWord = true;
      currentLineHasWords = true;
      word.index = charStart;
      wordStartPos = charStart;
    }
  }

  // Handle the final word if the text doesn't end in whitespace.
  if (traversingWord) {
    size_t wordLen = text.length() - wordStartPos;
    const int width = font->Width(text.c_str() + word.index, wordLen, '\0');

    if (word.x + width > wrapWidth && word.x > 0) {
      word.y += lineHeight;
      word.x = 0;
      AdjustLine(lineBegin, lineWidth, false);
    }
    words.push_back(word);
    words.back().length = wordLen;
    word.x += width;
    lineWidth = word.x;
  }

  // Ensure the very last line is aligned if it hasn't been yet.
  if (lineBegin < words.size()) {
    AdjustLine(lineBegin, lineWidth, true);
  }

  // Calculate the total wrapped height, including the last line.
  if (!words.empty()) {
    height = words.back().y + lineHeight;
  }
}

void WrappedText::AdjustLine(size_t &lineBegin, int &lineWidth, bool isEnd) {
  int wordCount = static_cast<int>(words.size() - lineBegin);
  int extraSpace = wrapWidth - lineWidth;

  if (lineWidth > longestLineWidth)
    longestLineWidth = lineWidth;

  // Figure out how much space is left over. Depending on the alignment, we
  // will add that space to the left, to the right, to both sides, or to the
  // space in between the words. Exception: the last line of a "justified"
  // paragraph is left aligned, not justified.
  if (alignment == Alignment::JUSTIFIED && !isEnd && wordCount > 1) {
    for (int i = 0; i < wordCount; ++i)
      words[lineBegin + i].x += extraSpace * i / (wordCount - 1);
  } else if (alignment == Alignment::CENTER || alignment == Alignment::RIGHT) {
    int shift = (alignment == Alignment::CENTER) ? extraSpace / 2 : extraSpace;
    for (int i = 0; i < wordCount; ++i)
      words[lineBegin + i].x += shift;
  }

  lineBegin = words.size();
  lineWidth = 0;
}

int WrappedText::Space(char c) const {
  return (c == ' ') ? space : (c == '\t') ? tabWidth : 0;
}
