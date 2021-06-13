#include <future>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <mutex>

using namespace std::string_literals;

template <typename Key, typename Value>
class ConcurrentMap {
public:
    static_assert(std::is_integral_v<Key>, "ConcurrentMap supports only integer keys"s);

    struct Bucket {
        std::map<Key, Value> map;
        std::mutex map_mutex;
    };

    struct Access {
        Access(Bucket& bucket, const Key& key) :  guard(bucket.map_mutex), ref_to_value(bucket.map[key]) {
        }
        std::lock_guard<std::mutex> guard;
        Value& ref_to_value;

    };

    explicit ConcurrentMap(size_t bucket_count) : maps_(bucket_count), count_maps_(bucket_count) {
    }

    Access operator[](const Key& key) {
        auto index = key % count_maps_;
        return {maps_[index], key};
    }

    std::map<Key, Value> BuildOrdinaryMap() {
        std::map<Key, Value> result;
        for(size_t i = 0; i < count_maps_; ++i) {
            std::lock_guard guard(maps_[i].map_mutex);
            result.insert(maps_[i].map.begin(), maps_[i].map.end());
        }
        return result;
    }

private:
    std::vector<Bucket> maps_;
    size_t count_maps_;
};

// template <typename Key>
// class ConcurrentSet {
// public:

//     struct Bucket
//     {
//         std::set<Key> set;
//         std::mutex set_mutex;
//     };

//     struct Access {
//         Access(Bucket& bucket, const Key& key) :  guard(bucket.map_mutex), ref_to_value(bucket.set[key]) {
//         }
//         std::lock_guard<std::mutex> guard;
//         Key& ref_to_value;

//     };

//     explicit ConcurrentSet(size_t bucket_count) : sets_(bucket_count), count_sets_(bucket_count) {
//     }

//     Access operator[](const Key& key) {
//         auto index = key % count_sets_;
//         return {sets_[index], key};
//     }

//     std::set<Key> BuildOrdinarySet() {
//         std::set<Key> result;
//         for(size_t i = 0; i < count_sets_; ++i) {
//             std::lock_guard guard(sets_[i].set_mutex);
//             result.insert(sets_[i].set.begin(), sets_[i].set.end());
//         }
//         return result;
//     }

// private:
//     std::vector<Bucket> sets_;
//     size_t count_sets_;
// };