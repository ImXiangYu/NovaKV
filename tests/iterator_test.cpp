#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "DBImpl.h"
#include "ValueRecord.h"

namespace fs = std::filesystem;

namespace {

void PutValue(DBImpl& db, const std::string& key, const std::string& value) {
    ValueRecord record{ValueType::kValue, value};
    db.Put(key, record);
}

void PutDeletion(DBImpl& db, const std::string& key) {
    ValueRecord record{ValueType::kDeletion, ""};
    db.Put(key, record);
}

void ForceMinorCompaction(DBImpl& db, const std::string& prefix) {
    for (int i = 0; i < 1000; ++i) {
        PutValue(db, prefix + "_fill_" + std::to_string(i), "v");
    }
    PutValue(db, prefix + "_trigger", "x");
}

template <typename T, typename = void>
struct HasNewIterator : std::false_type {};

template <typename T>
struct HasNewIterator<T, std::void_t<decltype(std::declval<T&>().NewIterator())>> : std::true_type {};

template <typename T, typename = void>
struct HasSeek : std::false_type {};

template <typename T>
struct HasSeek<T, std::void_t<decltype(std::declval<T&>().Seek(std::declval<const std::string&>()))>> : std::true_type {};

template <typename T, typename = void>
struct HasNext : std::false_type {};

template <typename T>
struct HasNext<T, std::void_t<decltype(std::declval<T&>().Next())>> : std::true_type {};

template <typename T, typename = void>
struct HasValid : std::false_type {};

template <typename T>
struct HasValid<T, std::void_t<decltype(std::declval<T&>().Valid())>> : std::true_type {};

template <typename T, typename = void>
struct HasKey : std::false_type {};

template <typename T>
struct HasKey<T, std::void_t<decltype(std::declval<T&>().key())>> : std::true_type {};

template <typename T, typename = void>
struct HasValue : std::false_type {};

template <typename T>
struct HasValue<T, std::void_t<decltype(std::declval<T&>().value())>> : std::true_type {};

template <typename DB, typename = void>
struct HasExpectedIteratorApi : std::false_type {};

template <typename DB>
struct HasExpectedIteratorApi<DB, std::enable_if_t<HasNewIterator<DB>::value>> {
private:
    using IteratorPtr = decltype(std::declval<DB&>().NewIterator());
    using Iterator = std::remove_reference_t<decltype(*std::declval<IteratorPtr&>())>;

public:
    static constexpr bool value =
        HasSeek<Iterator>::value &&
        HasNext<Iterator>::value &&
        HasValid<Iterator>::value &&
        HasKey<Iterator>::value &&
        HasValue<Iterator>::value;
};

template <typename DB>
std::vector<std::pair<std::string, std::string>> CollectFrom(DB& db, const std::string& start_key) {
    if constexpr (!HasExpectedIteratorApi<DB>::value) {
        GTEST_SKIP() << "Iterator API not ready. Expect DBImpl::NewIterator + Seek/Next/Valid/key/value.";
        return {};
    } else {
        std::vector<std::pair<std::string, std::string>> out;
        auto it = db.NewIterator();
        ASSERT_TRUE(static_cast<bool>(it));

        it->Seek(start_key);
        while (it->Valid()) {
            out.emplace_back(it->key(), it->value());
            it->Next();
        }
        return out;
    }
}

}  // namespace

class IteratorTest : public ::testing::Test {
protected:
    std::string test_db_path = "./test_iterator_db";

    void SetUp() override {
        if (fs::exists(test_db_path)) {
            fs::remove_all(test_db_path);
        }
        fs::create_directories(test_db_path);
    }

    void TearDown() override {
        if (fs::exists(test_db_path)) {
            fs::remove_all(test_db_path);
        }
    }
};

TEST_F(IteratorTest, SeekAndNext_BasicOrder) {
    DBImpl db(test_db_path);
    PutValue(db, "a", "1");
    PutValue(db, "b", "2");
    PutValue(db, "c", "3");

    const auto rows = CollectFrom(db, "b");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0], std::make_pair(std::string("b"), std::string("2")));
    EXPECT_EQ(rows[1], std::make_pair(std::string("c"), std::string("3")));
}

TEST_F(IteratorTest, Seek_UsesLowerBoundSemantics) {
    DBImpl db(test_db_path);
    PutValue(db, "a", "1");
    PutValue(db, "c", "3");

    const auto rows = CollectFrom(db, "b");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0], std::make_pair(std::string("c"), std::string("3")));
}

TEST_F(IteratorTest, Iterator_HidesTombstoneInMemTable) {
    DBImpl db(test_db_path);
    PutValue(db, "a", "alive");
    PutDeletion(db, "a");
    PutValue(db, "b", "visible");

    const auto rows = CollectFrom(db, "a");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0], std::make_pair(std::string("b"), std::string("visible")));
}

TEST_F(IteratorTest, Iterator_NewestVersionWinsAcrossSSTAndMemTable) {
    DBImpl db(test_db_path);

    PutValue(db, "dup", "old");
    ForceMinorCompaction(db, "round1");

    PutValue(db, "dup", "new");
    PutValue(db, "z", "tail");

    const auto rows = CollectFrom(db, "dup");
    ASSERT_GE(rows.size(), 2u);
    EXPECT_EQ(rows[0], std::make_pair(std::string("dup"), std::string("new")));

    size_t dup_count = 0;
    for (const auto& row : rows) {
        if (row.first == "dup") {
            ++dup_count;
        }
    }
    EXPECT_EQ(dup_count, 1u);
}

TEST_F(IteratorTest, Iterator_HidesTombstoneAcrossLevels) {
    DBImpl db(test_db_path);

    PutValue(db, "k", "v1");
    ForceMinorCompaction(db, "round1");

    PutDeletion(db, "k");
    ForceMinorCompaction(db, "round2");

    PutValue(db, "m", "v2");

    const auto rows = CollectFrom(db, "k");
    for (const auto& row : rows) {
        EXPECT_NE(row.first, "k");
    }
}
