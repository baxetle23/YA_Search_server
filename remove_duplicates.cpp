// #include "remove_duplicates.h"

// void RemoveDuplicates(SearchServer& search_server) {
// 	std::set<std::set<std::string>> words_set;
// 	std::vector<int> remove_documents;

// 	for (auto document : search_server) {
// 		std::set<std::string> document_words_set;
// 		auto word_freq = search_server.GetWordFrequencies(document);
// 		std::transform(word_freq.begin(), word_freq.end(), std::inserter(document_words_set, document_words_set.begin()),
// 		[](auto pair){
// 			return pair.first;
// 		});
// 		if (words_set.count(document_words_set)) {
// 			remove_documents.emplace_back(document);
// 		}
// 		else {
// 			words_set.emplace(document_words_set);
// 		}
// 	}
// 	for (auto document : remove_documents) {
// 		search_server.RemoveDocument(document);

// 		std::cout << "Found duplicate document id " << document << std::endl;
// 	}
// }