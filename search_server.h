#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <map>
#include <set>
#include <algorithm>
#include <string_view>
#include <execution>
#include <cmath>
#include "document.h"
#include "string_processing.h"
#include "concurrent_map.h"
#include "log_duration.h"

const int MAX_RESULT_DOCUMENT_COUNT = 5;
using namespace std::string_literals;

class SearchServer {
public:
	
	template <typename StringContainer>
	explicit SearchServer(const StringContainer& stop_words);
	explicit SearchServer(const std::string& stop_words_text);
	explicit SearchServer(const std::string_view stop_words_text);

	void AddDocument(int document_id, const std::string_view& document, DocumentStatus status, const std::vector<int>& ratings);

	template <typename DocumentPredicate>
	std::vector<Document> FindTopDocuments(const std::string_view raw_query, DocumentPredicate document_predicate) const;
	std::vector<Document> FindTopDocuments(const std::string_view raw_query, DocumentStatus status) const;
	std::vector<Document> FindTopDocuments(const std::string_view raw_query) const; 

	template <typename DocumentPredicate, typename ExecutionPolicy>
	std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, const std::string_view raw_query, DocumentPredicate document_predicate) const;
	template <typename ExecutionPolicy>
	std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, const std::string_view raw_query, DocumentStatus status) const;
	template <typename ExecutionPolicy>
	std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, const std::string_view raw_query) const;

	using WordsInDocument = std::tuple<std::vector<std::string_view>, DocumentStatus>;
	WordsInDocument MatchDocument(const std::string_view raw_query, int document_id) const;
	WordsInDocument MatchDocument(std::execution::parallel_policy, const std::string_view raw_query, int document_id) const;
	WordsInDocument MatchDocument(std::execution::sequenced_policy, const std::string_view raw_query, int document_id) const;

	int GetDocumentCount() const;
	std::set<int>::const_iterator begin() const;
	std::set<int>::const_iterator end() const;
	const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;

	void RemoveDocument(int document_id);
	template <typename ExecutionPolicy>
	void RemoveDocument(ExecutionPolicy&& policy, int document_id);

private:
	struct DocumentData {
		int rating;
		DocumentStatus status;
	};
	const std::set<std::string> stop_words_;
	std::map<std::string, std::map<int, double>> word_to_document_freqs_;
	std::map<int, DocumentData> documents_;
	std::map<int, std::map<std::string,double>>  id_freqs_word_;
	std::set<int> document_ids_;

	bool IsStopWord(const std::string_view word) const;
	static bool IsValidWord(const std::string_view word);
	std::vector<std::string> SplitIntoWordsNoStop(const std::string_view text) const;

	struct QueryWord {
		std::string data;
		bool is_minus;
		bool is_stop;
	};

	struct Query {
		std::set<std::string, std::less<>> plus_words;
		std::set<std::string, std::less<>> minus_words;
	};

	Query ParseQuery(const std::string_view text) const;
	QueryWord ParseQueryWord(const std::string_view text) const;

	double ComputeWordInverseDocumentFreq(const std::string& word) const;
	static int ComputeAverageRating(const std::vector<int>& ratings);
	
	template <typename DocumentPredicate>
	std::vector<Document> FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const;
	template <typename DocumentPredicate>
	std::vector<Document> FindAllDocuments(std::execution::parallel_policy, const Query& query, DocumentPredicate document_predicate) const;
	template <typename DocumentPredicate>
	std::vector<Document> FindAllDocuments(std::execution::sequenced_policy, const Query& query, DocumentPredicate document_predicate) const;
};

void AddDocument(SearchServer& search_server, int document_id, const std::string& document, DocumentStatus status, const std::vector<int>& ratings);
void FindTopDocuments(const SearchServer& search_server, const std::string& raw_query);
void MatchDocuments(const SearchServer& search_server, const std::string& query);

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
: stop_words_(MakeUniqueNonEmptyStrings(stop_words)){
	if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
		throw std::invalid_argument("Some of stop words are invalid"s);
	}
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(const std::string_view raw_query, DocumentPredicate document_predicate) const {
	const auto query = ParseQuery(raw_query);
	auto matched_documents = FindAllDocuments(query, document_predicate);
	std::sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
		if (std::abs(lhs.relevance - rhs.relevance) < 1e-6) {
			return lhs.rating > rhs.rating;
		} else {
			return lhs.relevance > rhs.relevance;
		}
	});
	if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
		matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
	}
	return matched_documents;
}

template <typename DocumentPredicate, typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, const std::string_view raw_query, DocumentPredicate document_predicate) const {
	const auto query = ParseQuery(raw_query);
	auto matched_documents = FindAllDocuments(policy, query, document_predicate);
	std::sort(policy, matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
		if (std::abs(lhs.relevance - rhs.relevance) < 1e-6) {
			return lhs.rating > rhs.rating;
			} else {
			return lhs.relevance > rhs.relevance;
		}
	});
	if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
		matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
	}
	return matched_documents;
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, const std::string_view raw_query, DocumentStatus status) const {
	return FindTopDocuments(policy, raw_query, [status](int, DocumentStatus document_status, int) {
		return document_status == status;
	});
}
template <typename ExecutionPolicy>	
	std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, const std::string_view raw_query) const {
	return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const {
	std::map<int, double> document_to_relevance;
	for (const std::string& word : query.plus_words) {
		if (word_to_document_freqs_.count(word) == 0) {
			continue;
		}
		const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
		for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
			const auto& document_data = documents_.at(document_id);
			if (document_predicate(document_id, document_data.status, document_data.rating)) {
				document_to_relevance[document_id] += term_freq * inverse_document_freq;
			}	
		}
	}
	for (const std::string& word : query.minus_words) {
		if (word_to_document_freqs_.count(word) == 0) {
			continue;
		}
		for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
			document_to_relevance.erase(document_id);
		}
	}
	std::vector<Document> matched_documents;
	for (const auto [document_id, relevance] : document_to_relevance) {
		matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
	}
	return matched_documents;
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(std::execution::sequenced_policy, const Query& query, DocumentPredicate document_predicate) const {
	std::map<int, double> document_to_relevance;
	for (const std::string& word : query.plus_words) {
		if (word_to_document_freqs_.count(word) == 0) {
			continue;
		}
		const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
		for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
			const auto& document_data = documents_.at(document_id);
			if (document_predicate(document_id, document_data.status, document_data.rating)) {
				document_to_relevance[document_id] += term_freq * inverse_document_freq;
			}	
		}
	}
	for (const std::string& word : query.minus_words) {
		if (word_to_document_freqs_.count(word) == 0) {
			continue;
		}
		for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
			document_to_relevance.erase(document_id);
		}
	}
	std::vector<Document> matched_documents;
	for (const auto [document_id, relevance] : document_to_relevance) {
		matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
	}
	return matched_documents;
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(std::execution::parallel_policy, const Query& query, DocumentPredicate document_predicate) const {
	ConcurrentMap<int, double> document_to_relevance(4);
	for (auto word : query.plus_words) {
		auto it = std::find_if(std::execution::par, word_to_document_freqs_.begin(), word_to_document_freqs_.end(),
			[&word](auto& pair) {return pair.first == word;});
		if (it != word_to_document_freqs_.end()) {
			const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
			for_each(std::execution::par, (it->second).begin(), (it->second).end(), 
				[&] (const std::pair<int, double>  id_freq) {
				const auto& document_data = documents_.at(id_freq.first);
				if (document_predicate(id_freq.first, document_data.status, document_data.rating)) {
					document_to_relevance[id_freq.first] += id_freq.second * inverse_document_freq;
				}
			});
		}
	}
	for (auto word : query.minus_words) {
		auto it = std::find_if(std::execution::par, word_to_document_freqs_.begin(), word_to_document_freqs_.end(),
			[&word](auto& pair) {return pair.first == word;});
		if (it != word_to_document_freqs_.end()) {
			for (const auto [document_id, _] : it->second) {
				document_to_relevance.erase(document_id);
			}
		}
	}	
	std::map<int, double> documents (std::move(document_to_relevance.BuildOrdinaryMap()));
	std::vector<Document> matched_documents;
	for (const auto [document_id, relevance] : documents) {
		matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
	}
	return matched_documents;
}

template <typename ExecutionPolicy>
void SearchServer::RemoveDocument(ExecutionPolicy&& policy, int document_id) {
	if (document_ids_.count(document_id)) {
		std::for_each(policy, id_freqs_word_.at(document_id).begin(), id_freqs_word_.at(document_id).begin(),
			[&](auto& pair) {
				word_to_document_freqs_.at(pair.first).erase(document_id);
				if (word_to_document_freqs_.at(pair.first).empty()) {
					std::mutex tmp; // замедляет непараллельную версию 
					std::lock_guard guard(tmp);
					word_to_document_freqs_.erase(pair.first);
				}
			});
		documents_.erase(document_id);
		document_ids_.erase(document_id);
		id_freqs_word_.erase(document_id);
	}
}