#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "InMemoryServiceClient.h"
#include "LinkMap.h"
#include "MockTransportSdk.h"

namespace {

std::shared_ptr<Link> makeLink(const LinkID &id, ::testing::NiceMock<MockTransportSdk> &sdk) {
    return std::make_shared<Link>(id, LinkAddress{"c-" + id, ""}, LinkProperties{}, true, true,
                                  std::make_shared<InMemoryServiceClient>(), &sdk);
}

}  // namespace

TEST(TestLinkMap, get_returns_nullptr_for_unknown_link) {
    LinkMap map;
    EXPECT_EQ(nullptr, map.get("missing"));
    EXPECT_FALSE(map.contains("missing"));
}

TEST(TestLinkMap, add_get_remove) {
    ::testing::NiceMock<MockTransportSdk> sdk;
    LinkMap map;
    auto link = makeLink("l1", sdk);
    map.add(link);
    map.add(makeLink("l2", sdk));

    EXPECT_EQ(2u, map.size());
    EXPECT_EQ(link, map.get("l1"));
    EXPECT_EQ(2u, map.getAll().size());
    EXPECT_EQ(link, map.remove("l1"));
    EXPECT_EQ(nullptr, map.remove("l1"));
    EXPECT_EQ(1u, map.size());
}
