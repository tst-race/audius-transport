#include <gtest/gtest.h>

#include "LinkAddress.h"

TEST(TestLinkAddress, round_trips_through_json) {
    LinkAddress address{"container-1", "00000007"};
    LinkAddress parsed;
    ASSERT_TRUE(parseLinkAddress(linkAddressToString(address), parsed));
    EXPECT_EQ("container-1", parsed.container);
    EXPECT_EQ("00000007", parsed.initialCursor);
}

TEST(TestLinkAddress, cursor_is_optional) {
    LinkAddress parsed;
    ASSERT_TRUE(parseLinkAddress(R"({"container": "c"})", parsed));
    EXPECT_EQ("", parsed.initialCursor);
}

TEST(TestLinkAddress, ignores_unknown_fields) {
    LinkAddress parsed;
    ASSERT_TRUE(parseLinkAddress(R"({"container": "c", "note": "informational"})", parsed));
    EXPECT_EQ("c", parsed.container);
}

TEST(TestLinkAddress, rejects_bad_input_without_modifying_output) {
    LinkAddress parsed{"keep", ""};
    EXPECT_FALSE(parseLinkAddress("not json", parsed));
    EXPECT_FALSE(parseLinkAddress("{}", parsed));
    EXPECT_FALSE(parseLinkAddress(R"({"container": ""})", parsed));
    EXPECT_EQ("keep", parsed.container);
}
