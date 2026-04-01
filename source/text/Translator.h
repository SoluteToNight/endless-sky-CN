/* Translator.h
Copyright (c) 2014-2024 by Michael Zahniser

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

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Class to handle hardcoded string translations dynamically at render time.
class Translator {
public:
	// Initializes the dictionary by parsing all translation files.
	static void Init(const std::vector<std::filesystem::path> &sources);
	
	// Returns the translated string if found, otherwise returns the original string_view.
	static std::string_view Get(std::string_view source);

private:
	// Transparent string hash to allow querying the unordered_map with string_view
	// without allocating temporary std::string objects.
	struct string_hash {
		using is_transparent = void;
		std::size_t operator()(std::string_view sv) const {
			return std::hash<std::string_view>{}(sv);
		}
	};

	static std::unordered_map<std::string, std::string, string_hash, std::equal_to<>> dictionary;
};
