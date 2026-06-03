#pragma once

namespace spotykach {

class Follower {
public:
    Follower();
    ~Follower() = default;

    void set_speed(const float norm);
    void set_amp(const float norm);
    void process(const float in);
    void reset();
    float value() const;

private:
    static constexpr auto ln367 = -0.99967234081320612357829304641019f;
    static constexpr auto kof = ln367 / 48.f; //ln(36,7) / (samplerate * 0.001)
    static constexpr auto kMinFadeMs = 8.f;
    static constexpr auto kAttackRangeMs = 22.f; 
    static constexpr auto kReleaseRangeMs = 3992.f;
    static constexpr auto kAmpMult = 1000.f;

    float _attack_ms;
    float _release_ms;
    float _value;
    float _amp;
};
};
