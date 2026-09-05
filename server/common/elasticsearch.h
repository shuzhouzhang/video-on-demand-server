#pragma once

#include "config.h"
#include "../data/video.h"

#include <string>
#include <vector>

namespace bitesearch {

class IVideoSearchIndex {
public:
    virtual ~IVideoSearchIndex() = default;
    virtual bool ensureIndex(std::string& error) = 0;
    virtual bool upsert(const bitevideo::Video& video,
                        std::string& error) = 0;
    virtual bool remove(const std::string& videoId,
                        std::string& error) = 0;
    virtual bool searchIds(const std::string& keyword,
                           std::vector<std::string>& videoIds,
                           std::string& error) = 0;
};

#ifdef VOD_ENABLE_REFERENCE_RUNTIME
class ElasticsearchVideoIndex final : public IVideoSearchIndex {
public:
    explicit ElasticsearchVideoIndex(
        biteconfig::ElasticsearchSettings settings);

    bool ensureIndex(std::string& error) override;
    bool upsert(const bitevideo::Video& video,
                std::string& error) override;
    bool remove(const std::string& videoId,
                std::string& error) override;
    bool searchIds(const std::string& keyword,
                   std::vector<std::string>& videoIds,
                   std::string& error) override;

private:
    bool request(const std::string& method,
                 const std::string& path,
                 const std::string& body,
                 long& status,
                 Json::Value& response,
                 std::string& error) const;
    std::string escapedPathSegment(const std::string& value) const;

    biteconfig::ElasticsearchSettings settings_;
};
#endif

}  // namespace bitesearch
