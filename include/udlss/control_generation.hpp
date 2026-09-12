#pragma once
#include <cstdint>
namespace udlss {
class GenerationTracker {
public:
    bool consume(std::uint32_t generation) {
        if (generation == seen_) return false;
        seen_ = generation;
        return true;
    }
    std::uint32_t seen() const { return seen_; }
private:
    std::uint32_t seen_{};
};
}
