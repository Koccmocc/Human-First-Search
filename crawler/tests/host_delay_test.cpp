#include "hfs/host_delay.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;

class HostDelayTest : public testing::Test {
protected:
    hfs::HostDelayOptions::Clock::time_point now{};
    std::vector<std::chrono::milliseconds> waits;
    hfs::HostDelayOptions timing{
        1s, [this] { return now; }, [this](auto duration) {
            waits.push_back(duration);
            now += duration;
        }};
};

TEST_F(HostDelayTest, FirstRequestIsImmediateAndNextWaits)
{
    hfs::HostDelay delay{timing};
    delay.wait("https://example.org/a");
    EXPECT_TRUE(waits.empty());
    delay.wait("https://example.org/b");
    EXPECT_EQ(waits, (std::vector<std::chrono::milliseconds>{1s}));
}

TEST_F(HostDelayTest, WaitsOnlyForRemainingTime)
{
    hfs::HostDelay delay{timing};
    delay.wait("https://example.org/");
    now += 250ms;
    delay.wait("https://example.org/next");
    EXPECT_EQ(waits, (std::vector<std::chrono::milliseconds>{750ms}));
    now += 1500ms;
    delay.wait("https://example.org/last");
    EXPECT_EQ(waits.size(), 1U);
}

TEST_F(HostDelayTest, SharesTimingAcrossSchemesAndPortsButSeparatesHosts)
{
    hfs::HostDelay delay{timing};
    delay.wait("http://EXAMPLE.org:8080/");
    delay.wait("https://example.org/");
    delay.wait("https://sub.example.org/");
    delay.wait("https://other.org/");
    EXPECT_EQ(waits, (std::vector<std::chrono::milliseconds>{1s}));
}

TEST_F(HostDelayTest, ZeroIntervalAllowsImmediateRequests)
{
    timing.interval = 0ms;
    hfs::HostDelay delay{timing};
    delay.wait("https://example.org/a");
    delay.wait("https://example.org/b");
    EXPECT_TRUE(waits.empty());
}

TEST_F(HostDelayTest, RejectsInvalidConfigurationAndUrls)
{
    timing.interval = -1ms;
    EXPECT_THROW(hfs::HostDelay{timing}, std::invalid_argument);
    timing.interval = 1s;
    timing.now = {};
    EXPECT_THROW(hfs::HostDelay{timing}, std::invalid_argument);
    timing.now = [this] { return now; };
    timing.sleep = {};
    EXPECT_THROW(hfs::HostDelay{timing}, std::invalid_argument);
    hfs::HostDelay delay;
    EXPECT_THROW(delay.wait("file:///unused"), std::invalid_argument);
}
