#include "LinkAddress.h"

void to_json(nlohmann::json &j, const LinkAddress &address) {
    j = nlohmann::json{{"container", address.container}, {"initialCursor", address.initialCursor}};
}

void from_json(const nlohmann::json &j, LinkAddress &address) {
    j.at("container").get_to(address.container);
    address.initialCursor = j.value("initialCursor", "");
}

bool parseLinkAddress(const std::string &text, LinkAddress &out) {
    try {
        LinkAddress parsed = nlohmann::json::parse(text).get<LinkAddress>();
        if (parsed.container.empty()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (const nlohmann::json::exception &) {
        return false;
    }
}

std::string linkAddressToString(const LinkAddress &address) {
    return nlohmann::json(address).dump();
}
