#include <doctest/doctest.h>

#include "RobloxModLoader/core/init_latch.hpp"

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

TEST_CASE("latch starts waiting and times out when nobody releases it")
{
	rml::InitLatch latch;
	CHECK(latch.state() == rml::InitGateState::Waiting);
	latch.mark_reached();
	CHECK(latch.state() == rml::InitGateState::Reached);
	CHECK_FALSE(latch.wait(20ms));
	CHECK(latch.state() == rml::InitGateState::Reached);
}

TEST_CASE("release from another thread wakes the waiter")
{
	rml::InitLatch latch;
	latch.mark_reached();
	std::thread releaser([&] {
		std::this_thread::sleep_for(20ms);
		latch.release();
	});
	CHECK(latch.wait(5s));
	releaser.join();
	CHECK(latch.state() == rml::InitGateState::Released);
}

TEST_CASE("a release before the gate is reached is remembered")
{
	rml::InitLatch latch;
	latch.release();
	CHECK(latch.wait(0ms));
	latch.mark_reached();
	CHECK(latch.state() == rml::InitGateState::Released);
}

TEST_CASE("missed is terminal")
{
	rml::InitLatch latch;
	latch.mark_missed();
	CHECK(latch.state() == rml::InitGateState::Missed);
	latch.release();
	CHECK(latch.state() == rml::InitGateState::Missed);
}
