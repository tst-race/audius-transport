#include "LinkMap.h"

size_t LinkMap::size() const {
    std::lock_guard<std::mutex> lock(mutex);
    return links.size();
}

bool LinkMap::contains(const LinkID &linkId) const {
    std::lock_guard<std::mutex> lock(mutex);
    return links.count(linkId) != 0;
}

void LinkMap::add(const std::shared_ptr<Link> &link) {
    std::lock_guard<std::mutex> lock(mutex);
    links[link->getId()] = link;
}

std::shared_ptr<Link> LinkMap::get(const LinkID &linkId) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto iter = links.find(linkId);
    return iter == links.end() ? nullptr : iter->second;
}

std::vector<std::shared_ptr<Link>> LinkMap::getAll() const {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::shared_ptr<Link>> all;
    all.reserve(links.size());
    for (const auto &entry : links) {
        all.push_back(entry.second);
    }
    return all;
}

std::shared_ptr<Link> LinkMap::remove(const LinkID &linkId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto iter = links.find(linkId);
    if (iter == links.end()) {
        return nullptr;
    }
    auto link = iter->second;
    links.erase(iter);
    return link;
}
