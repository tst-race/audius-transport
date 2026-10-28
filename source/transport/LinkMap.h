#ifndef __AUDIUS_LINK_MAP_H__
#define __AUDIUS_LINK_MAP_H__

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "Link.h"

// Thread-safe LinkID -> Link map. The SDK calls the component from several threads.
class LinkMap {
public:
    size_t size() const;
    bool contains(const LinkID &linkId) const;
    void add(const std::shared_ptr<Link> &link);
    // Returns nullptr (never throws) for an unknown link id.
    std::shared_ptr<Link> get(const LinkID &linkId) const;
    std::vector<std::shared_ptr<Link>> getAll() const;
    std::shared_ptr<Link> remove(const LinkID &linkId);

private:
    mutable std::mutex mutex;
    std::unordered_map<LinkID, std::shared_ptr<Link>> links;
};

#endif
