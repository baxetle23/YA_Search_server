#include "string_processing.h"

std::vector<std::string_view> SplitIntoWords(std::string_view text) {
	std::vector<std::string_view> words;
	while (true) {
		auto space = text.find(' ', 0);
		words.push_back(text.substr(0, space));
		if (space == text.npos) {
			break;
		}
		text.remove_prefix(space + 1);
	}
	return words;
}