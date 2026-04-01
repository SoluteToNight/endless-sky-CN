/* Translator.cpp
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

#include "Translator.h"

#include "../DataFile.h"
#include "../DataNode.h"

using namespace std;

unordered_map<string, string, Translator::string_hash, equal_to<>> Translator::dictionary;

void Translator::Init(const vector<std::filesystem::path> &sources) {
	dictionary.clear();

	for (const auto &source : sources) {
		std::filesystem::path dictDir = source / "_ui";
		if (!std::filesystem::exists(dictDir) || !std::filesystem::is_directory(dictDir))
			continue;

		for (const auto &entry : std::filesystem::directory_iterator(dictDir)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".txt")
				continue;

			DataFile file(entry.path().string());
			for (const DataNode &node : file) {
				// Parse dictionary formats such as:
				// "Combat Rating:" "战斗评级:"
				// Or:
				// translate "Cancel" "取消"
				
				if (node.Size() >= 2) {
					if (node.Token(0) == "translate" && node.Size() >= 3)
						dictionary[node.Token(1)] = node.Token(2);
					else
						dictionary[node.Token(0)] = node.Token(1);
				}
			}
		}
	}
}

string_view Translator::Get(string_view source) {
	if (dictionary.empty() || source.empty())
		return source;

	auto it = dictionary.find(source);
	if (it != dictionary.end())
		return it->second;

	return source;
}
