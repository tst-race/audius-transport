#ifndef __AUDIUS_PERSISTENT_STORAGE_HELPERS_H__
#define __AUDIUS_PERSISTENT_STORAGE_HELPERS_H__

#include <IComponentSdkBase.h>

#include <string>
#include <vector>

// Per-node persistent key/value storage built on the SDK file APIs. Values survive plugin restarts
// (e.g. a fetch cursor), so a restarted node does not re-deliver old messages.
namespace psh {

inline bool saveValue(IComponentSdkBase *sdk, const std::string &key, const std::string &value) {
    return sdk->writeFile(key, {value.begin(), value.end()}).status == CM_OK;
}

// Returns defaultValue if the key does not exist (or was cleared with saveValue(key, "")).
inline std::string readValue(IComponentSdkBase *sdk, const std::string &key,
                             const std::string &defaultValue = "") {
    std::vector<uint8_t> data = sdk->readFile(key);
    if (data.empty()) {
        return defaultValue;
    }
    return std::string(data.begin(), data.end());
}

// The SDK has no delete-file call; an empty value is treated as "absent" by readValue.
inline bool clearValue(IComponentSdkBase *sdk, const std::string &key) {
    return saveValue(sdk, key, "");
}

// Keys must not contain path separators other than the one used as a directory.
inline std::string cursorKey(const std::string &linkId) {
    return "audius/cursor/" + linkId;
}

}  // namespace psh

#endif
