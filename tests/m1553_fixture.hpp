#pragma once

#include <gtest/gtest.h>

#include "avb/m1553/bus.hpp"
#include "avb/m1553/bus_controller.hpp"
#include "avb/m1553/remote_terminal.hpp"

// Iki hatli (A/B) veri yolu, monitor, hata enjektoru, BC ve iki RT (5 ve 6).
struct VeriYolu : ::testing::Test {
    avb::m1553::SimClock clock;
    avb::m1553::BusMonitor monitor;
    avb::m1553::FaultInjector faults;
    avb::m1553::Bus busA{avb::m1553::BusId::A, clock, &monitor, &faults};
    avb::m1553::Bus busB{avb::m1553::BusId::B, clock, &monitor, &faults};
    avb::m1553::RemoteTerminal rt5{5};
    avb::m1553::RemoteTerminal rt6{6};
    avb::m1553::BusController bc{clock, busA, busB};

    void SetUp() override {
        for (auto* rt : {&rt5, &rt6}) {
            busA.attach(*rt);
            busB.attach(*rt);
        }
    }

    // BC'nin RT'ye yolladigi veriyi okur
    std::vector<std::uint16_t> received(const avb::m1553::RemoteTerminal& rt, std::uint8_t sa,
                                        std::uint32_t* seq = nullptr) {
        std::vector<std::uint16_t> out(32);
        out.resize(rt.readReceivedData(sa, out.data(), out.size(), seq));
        return out;
    }

    avb::m1553::StatusWord statusOf(std::uint8_t rt) {
        bc.setMaxRetries(0);
        return bc.execute(avb::m1553::Message::modeCodeMsg(rt, avb::m1553::mode::kTransmitStatusWord)).status;
    }
};
