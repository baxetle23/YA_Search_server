#include "search_server.h"


SearchServer::SearchServer(std::string_view stop_words_text)
: SearchServer(SplitIntoWords(stop_words_text)) {
}

SearchServer::SearchServer(const std::string& stop_words_text)
: SearchServer(SplitIntoWords(stop_words_text)) {
}

void SearchServer::AddDocument(int document_id, const std::string_view& document, DocumentStatus status, const std::vector<int>& ratings) {
	if ((document_id < 0) || (documents_.count(document_id) > 0)) {
		throw std::invalid_argument("Invalid document_id"s);
	}
	const auto words = SplitIntoWordsNoStop(document);
	const double inv_word_count = 1.0 / words.size();
	for (const std::string& word : words) {
		word_to_document_freqs_[word][document_id] += inv_word_count;
		id_freqs_word_[document_id][word] +=inv_word_count;
	}
	documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
	document_ids_.insert(document_id);
}

std::vector<Document> SearchServer::FindTopDocuments(const std::string_view raw_query, DocumentStatus status) const {
	return FindTopDocuments(raw_query, [status](int, DocumentStatus document_status, int) {
		return document_status == status;
	});
}
std::vector<Document> SearchServer::FindTopDocuments(const std::string_view raw_query) const {
	return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

int SearchServer::GetDocumentCount() const {
	return documents_.size();
}

std::set<int>::const_iterator SearchServer::begin() const{
	return document_ids_.cbegin();
}

std::set<int>::const_iterator SearchServer::end() const{
	return document_ids_.cend();
}

const std::map<std::string_view, double>& SearchServer::GetWordFrequencies(int document_id) const{
    static std::map<std::string_view,double> result;
	if (id_freqs_word_.count(document_id)) {
		for (const std::pair<std::string_view, double>& word_freq : id_freqs_word_.at(document_id)) {
			result.insert(word_freq);
		}
	}
	return result;
}

void SearchServer::RemoveDocument(int document_id) {
	if (document_ids_.count(document_id)) {
		//бежим по всем словам в документе
		std::for_each(id_freqs_word_.at(document_id).begin(), id_freqs_word_.at(document_id).begin(),
			[&](auto& pair) {
				//удаляем у слов данный документ
				word_to_document_freqs_.at(pair.first).erase(document_id);
				//если у слова нет документов где оно встречается - удаляем слово
				if (word_to_document_freqs_.at(pair.first).empty()) {
               		word_to_document_freqs_.erase(pair.first);
            	}
			});
		//удаляем документ в остальных контейнерах
		documents_.erase(document_id);
		document_ids_.erase(document_id);
		id_freqs_word_.erase(document_id);
	}
}

void SearchServer::RemoveDocument(std::execution::parallel_policy, int document_id) {
	if (document_ids_.count(document_id)) {
		std::for_each(std::execution::par, id_freqs_word_.at(document_id).begin(), id_freqs_word_.at(document_id).begin(),
			[&](auto& pair) {
				word_to_document_freqs_.at(pair.first).erase(document_id);
				if (word_to_document_freqs_.at(pair.first).empty()) {
					std::mutex tmp;
					std::lock_guard guard(tmp);
               		word_to_document_freqs_.erase(pair.first);
            	}
			});
		documents_.erase(document_id);
		document_ids_.erase(document_id);
		id_freqs_word_.erase(document_id);
	}
}

void SearchServer::RemoveDocument(std::execution::sequenced_policy, int document_id) {
	RemoveDocument(document_id);
}

using WordsInDocument = std::tuple<std::vector<std::string_view>, DocumentStatus>;
WordsInDocument SearchServer::MatchDocument(const std::string_view raw_query, int document_id) const {
	const auto query = ParseQuery(raw_query);
    std::vector<std::string_view> matched_words;
    for (auto& word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(std::string(word)).count(document_id)) {
            matched_words.push_back(word_to_document_freqs_.find(word)->first);
        }
    }
    for (std::string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(std::string(word)) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(std::string(word)).count(document_id)) {
            matched_words.clear();
            break;
        }
    }
    return {matched_words, documents_.at(document_id).status};
}

WordsInDocument SearchServer::MatchDocument(std::execution::sequenced_policy,
	const std::string_view raw_query, int document_id) const {
	return MatchDocument(raw_query, document_id);
}
//Понимаю метод скорей всего реализован неверно - тесты с нагрузкой непроходят	- но у меня нет идей как я могу его улучшить
//1. Оптимизировать функцию ParseQuery
//2.
WordsInDocument SearchServer::MatchDocument(std::execution::parallel_policy,
	const std::string_view raw_query, int document_id) const {
	const auto query = ParseQuery(raw_query);	
    static std::vector<std::string_view> matched_words;
 	matched_words.reserve(query.plus_words.size());
    std::copy_if(std::execution::par, query.plus_words.begin(), query.plus_words.end(),
						std::back_inserter(matched_words),
						[&](auto word){
						return word_to_document_freqs_.count(word) && word_to_document_freqs_.at(word).count(document_id);
						});
	for (const std::string& word : query.minus_words) {
		if (word_to_document_freqs_.count(word) && word_to_document_freqs_.at(word).count(document_id)) {
			matched_words.clear();
			break;
		}
	}
	return {matched_words, documents_.at(document_id).status};
}


bool SearchServer::IsStopWord(const std::string_view word) const {
	return stop_words_.count(std::string(word)) > 0;
}

bool SearchServer::IsValidWord(const std::string_view word) {
	return std::none_of(word.begin(), word.end(), [](char c) {
		return c >= '\0' && c < ' ';
	});
}

std::vector<std::string> SearchServer::SplitIntoWordsNoStop(const std::string_view text) const {
	std::vector<std::string> words;
    for (auto word : SplitIntoWords(text)) {
		if (!IsValidWord(word)) {
			std::string word_ = std::string(word);
			throw std::invalid_argument("Word "s + word_ + " is invalid"s);
		}
		if (!IsStopWord(word)) {
			words.push_back(std::string(word));
		}
	}
	return words;
}

int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
	if (ratings.empty()) {
		return 0;
	}
	int rating_sum = 0;
	for (const int rating : ratings) {
		rating_sum += rating;
	}
	return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(const std::string_view text) const {
	if (text.empty()) {
		throw std::invalid_argument("Query word is empty"s);
	}
	std::string word = std::string(text);
	bool is_minus = false;
	if (word[0] == '-') {
		is_minus = true;
		word = word.substr(1);
	}
	if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
		throw std::invalid_argument("Query word "s + word + " is invalid"s);
	}
	return {word, is_minus, IsStopWord(word)};
}

SearchServer::Query SearchServer::ParseQuery(const std::string_view text) const {
	Query result;
    for (auto& word : SplitIntoWords(text)) {
		const auto query_word = ParseQueryWord(word);
		if (!query_word.is_stop) {
			if (query_word.is_minus) {
				result.minus_words.insert(query_word.data);
			} else {
				result.plus_words.insert(query_word.data);
			}
		}
	}
	return result;
}

double SearchServer::ComputeWordInverseDocumentFreq(const std::string& word) const {
	return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}

void AddDocument(SearchServer& search_server, int document_id, const std::string& document, DocumentStatus status,
const std::vector<int>& ratings) {
	try {
		search_server.AddDocument(document_id, document, status, ratings);
	} catch (const std::invalid_argument& e) {
		std::cout << "Ошибка добавления документа "s << document_id << ": "s << e.what() << std::endl;
	}
}

void FindTopDocuments(const SearchServer& search_server, const std::string& raw_query) {
	std::cout << "Результаты поиска по запросу: "s << raw_query << std::endl;
	try {
		for (const Document& document : search_server.FindTopDocuments(raw_query)) {
			PrintDocument(document);
		}
	} catch (const std::invalid_argument& e) {
		std::cout << "Ошибка поиска: "s << e.what() << std::endl;
	}
}

void MatchDocuments(const SearchServer& search_server, const std::string& query) {
	try {
		std::cout << "Матчинг документов по запросу: "s << query << std::endl;
		for (const int document_id : search_server) {
			const auto [words, status] = search_server.MatchDocument(query, document_id);
			PrintMatchDocumentResult(document_id, words, status);
		}
		} catch (const std::invalid_argument& e) {
				std::cout << "Ошибка матчинга документов на запрос "s << query << ": "s << e.what() << std::endl;
			}
}
