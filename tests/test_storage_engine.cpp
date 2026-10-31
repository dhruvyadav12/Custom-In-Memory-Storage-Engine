#include "storage/persistence.hpp"
#include "storage/storage_engine.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

void test_strings() {
    std::cout << "[Test] StorageEngine: Strings... ";
    storage::StorageEngine engine;

    assert(engine.set("foo", "bar"));
    assert(engine.get("foo") == "bar");
    assert(engine.exists("foo"));
    assert(engine.type("foo") == "string");

    assert(engine.append("foo", "_baz") == 8);
    assert(engine.get("foo") == "bar_baz");
    assert(engine.strlen_cmd("foo") == 8);

    assert(engine.set("counter", "100"));
    assert(engine.incrby("counter", 50) == 150);
    assert(engine.incrby("counter", -30) == 120);

    assert(engine.del({"foo", "counter"}) == 2);
    assert(!engine.exists("foo"));
    std::cout << "PASSED\n";
}

void test_lists() {
    std::cout << "[Test] StorageEngine: Lists... ";
    storage::StorageEngine engine;

    assert(engine.rpush("list", {"a", "b", "c"}) == 3);
    assert(engine.lpush("list", {"z"}) == 4); // [z, a, b, c]
    assert(engine.llen("list") == 4);

    assert(engine.lpop("list") == "z");
    assert(engine.rpop("list") == "c");
    assert(engine.llen("list") == 2);

    auto range = engine.lrange("list", 0, -1);
    assert(range.size() == 2);
    assert(range[0] == "a");
    assert(range[1] == "b");

    std::cout << "PASSED\n";
}

void test_sets() {
    std::cout << "[Test] StorageEngine: Sets... ";
    storage::StorageEngine engine;

    assert(engine.sadd("set1", {"apple", "banana", "cherry"}) == 3);
    assert(engine.sadd("set1", {"apple"}) == 0); // duplicate
    assert(engine.sismember("set1", "banana"));
    assert(!engine.sismember("set1", "grape"));
    assert(engine.scard("set1") == 3);

    assert(engine.sadd("set2", {"banana", "cherry", "date"}) == 3);

    auto intersection = engine.sinter({"set1", "set2"});
    assert(intersection.size() == 2); // banana, cherry

    auto diff = engine.sdiff({"set1", "set2"});
    assert(diff.size() == 1);
    assert(diff[0] == "apple");

    std::cout << "PASSED\n";
}

void test_hashes() {
    std::cout << "[Test] StorageEngine: Hashes... ";
    storage::StorageEngine engine;

    assert(engine.hset("user:1", "name", "Dhruv") == 1);
    assert(engine.hset("user:1", "role", "Engineer") == 1);
    assert(engine.hset("user:1", "name", "Dhruv Yadav") == 0); // update

    assert(engine.hget("user:1", "name") == "Dhruv Yadav");
    assert(engine.hexists("user:1", "role"));
    assert(engine.hlen("user:1") == 2);

    auto all = engine.hgetall("user:1");
    assert(all.size() == 2);

    assert(engine.hdel("user:1", {"role"}) == 1);
    assert(!engine.hexists("user:1", "role"));
    std::cout << "PASSED\n";
}

void test_avl_sorted_sets() {
    std::cout << "[Test] StorageEngine: AVL Tree Sorted Sets... ";
    storage::StorageEngine engine;

    assert(engine.zadd("leaderboard", 100.0, "alice") == 1);
    assert(engine.zadd("leaderboard", 250.5, "bob") == 1);
    assert(engine.zadd("leaderboard", 50.0, "charlie") == 1);
    assert(engine.zadd("leaderboard", 180.0, "david") == 1);
    assert(engine.zcard("leaderboard") == 4);

    assert(*engine.zscore("leaderboard", "bob") == 250.5);

    // Range in sorted order: charlie (50), alice (100), david (180), bob (250.5)
    auto sorted = engine.zrange("leaderboard", 0, -1);
    assert(sorted.size() == 4);
    assert(sorted[0].first == "charlie");
    assert(sorted[1].first == "alice");
    assert(sorted[2].first == "david");
    assert(sorted[3].first == "bob");

    // AVL Tree Range Query by Score [80, 200] -> alice (100), david (180)
    auto by_score = engine.zrangebyscore("leaderboard", 80.0, 200.0);
    assert(by_score.size() == 2);
    assert(by_score[0].first == "alice");
    assert(by_score[1].first == "david");

    assert(engine.zrem("leaderboard", "alice") == 1);
    assert(engine.zcard("leaderboard") == 3);
    std::cout << "PASSED\n";
}

void test_ttl_and_persistence() {
    std::cout << "[Test] StorageEngine: TTL Expiration & TLV Persistence... ";
    storage::StorageEngine engine;

    engine.set("temp_key", "ephemeral", 0, 80); // 80ms TTL
    assert(engine.exists("temp_key"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!engine.exists("temp_key")); // Lazy expiration
    assert(engine.get("temp_key") == std::nullopt);

    // Test TLV snapshot save and restore
    engine.set("persisted_str", "hello_tlv");
    engine.lpush("persisted_list", {"one", "two", "three"});
    engine.zadd("persisted_zset", 99.9, "winner");

    std::string test_dump = "test_dump.tlv";
    storage::PersistenceManager pm(test_dump);
    assert(pm.save(engine));

    storage::StorageEngine restored_engine;
    assert(pm.load(restored_engine));

    assert(restored_engine.get("persisted_str") == "hello_tlv");
    assert(restored_engine.llen("persisted_list") == 3);
    assert(*restored_engine.zscore("persisted_zset", "winner") == 99.9);

    std::remove(test_dump.c_str());
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Running StorageEngine Full System Tests ===\n";
    test_strings();
    test_lists();
    test_sets();
    test_hashes();
    test_avl_sorted_sets();
    test_ttl_and_persistence();
    std::cout << "ALL StorageEngine Tests Passed Successfully!\n";
    return 0;
}
