#include "avb/m1553/bus.hpp"

#include <cstdio>

#include "avb/m1553/remote_terminal.hpp"

namespace avb::m1553 {

const char* toString(BusId b) { return b == BusId::A ? "A" : "B"; }

// ---------------------------------------------------------------- BusMonitor

void BusMonitor::record(const MonitorEntry& e) {
    ring_[head_] = e;
    head_ = (head_ + 1) % kCapacity;
    if (count_ < kCapacity) {
        ++count_;
    }
    ++total_;
}

const MonitorEntry& BusMonitor::at(std::size_t i) const {
    const std::size_t oldest = (head_ + kCapacity - count_) % kCapacity;
    return ring_[(oldest + i) % kCapacity];
}

void BusMonitor::clear() {
    head_ = 0;
    count_ = 0;
}

std::string BusMonitor::format(std::size_t i) const {
    const MonitorEntry& e = at(i);
    char src[8];
    if (e.source.isBc) {
        std::snprintf(src, sizeof src, "BC");
    } else {
        std::snprintf(src, sizeof src, "RT%02u", static_cast<unsigned>(e.source.rt));
    }
    char line[96];
    if (e.dropped) {
        std::snprintf(line, sizeof line, "%10.1f us  %s  %-4s  (kelime dusuruldu)", static_cast<double>(e.timeNs) / 1000.0,
                      toString(e.bus), src);
    } else {
        const char* kind = e.decoded.word.sync == Sync::CommandStatus ? "KMT/DRM" : "VERI";
        std::snprintf(line, sizeof line, "%10.1f us  %s  %-4s  %-7s 0x%04X  %s", static_cast<double>(e.timeNs) / 1000.0,
                      toString(e.bus), src, e.decoded.ok() ? kind : "-", e.decoded.word.value,
                      e.decoded.ok() ? "" : toString(e.decoded.error));
    }
    return line;
}

// ------------------------------------------------------------- FaultInjector

bool FaultInjector::add(const Rule& r) {
    if (count_ >= kMaxRules) {
        return false;
    }
    rules_[count_++] = r;
    return true;
}

void FaultInjector::clear() {
    count_ = 0;
    halfBitErrorRate_ = 0.0;
}

void FaultInjector::setRandomHalfBitErrorRate(double p, std::uint32_t seed) {
    halfBitErrorRate_ = p;
    rng_ = seed == 0 ? 1 : seed;
}

std::uint32_t FaultInjector::nextRandom() {
    // xorshift32: platformdan bağımsız, tekrarlanabilir
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

FaultInjector::Effect FaultInjector::apply(BusId bus, Source src, int wordIndex, HalfBits hb) {
    Effect e{hb, false};
    for (std::size_t i = 0; i < count_; ++i) {
        Rule& r = rules_[i];
        if (r.remaining == 0 || r.bus != bus || r.fromBc != src.isBc || r.wordIndex != wordIndex) {
            continue;
        }
        if (!src.isBc && r.rt != src.rt) {
            continue;
        }
        switch (r.action) {
            case Action::FlipHalfBit:
                e.halfBits ^= HalfBits{1} << (r.halfBit % kHalfBitsPerWord);
                break;
            case Action::CorruptSync:
                e.halfBits ^= HalfBits{0b111111};  // senkron ters: komut <-> veri
                e.halfBits ^= HalfBits{0b001000};  // ve geçersiz kalıba bozulur
                break;
            case Action::Drop:
                e.dropped = true;
                break;
        }
        if (r.remaining > 0) {
            --r.remaining;
        }
        ++injected_;
    }
    if (halfBitErrorRate_ > 0.0) {
        const auto threshold = static_cast<std::uint32_t>(halfBitErrorRate_ * 4294967295.0);
        for (int b = 0; b < kHalfBitsPerWord; ++b) {
            if (nextRandom() < threshold) {
                e.halfBits ^= HalfBits{1} << b;
                ++injected_;
            }
        }
    }
    return e;
}

// ----------------------------------------------------------------------- Bus

Bus::Bus(BusId id, SimClock& clock, BusMonitor* monitor, FaultInjector* faults)
    : id_(id), clock_(clock), monitor_(monitor), faults_(faults) {}

bool Bus::attach(RemoteTerminal& rt) {
    if (count_ >= kMaxTerminals) {
        return false;
    }
    terminals_[count_++] = &rt;
    return true;
}

void Bus::startOfMessage() {
    for (std::size_t i = 0; i < count_; ++i) {
        terminals_[i]->onMessageBoundary(id_);
    }
}

Bus::Heard Bus::transmit(const Word& w, Source src, int wordIndex) {
    HalfBits hb = encodeManchester(w);
    Heard heard;
    if (faults_ != nullptr) {
        const FaultInjector::Effect e = faults_->apply(id_, src, wordIndex, hb);
        hb = e.halfBits;
        heard.dropped = e.dropped;
    }
    clock_.advance(Timing::kWordNs);
    if (heard.dropped) {
        heard.decoded.error = DecodeError::InvalidSync;
    } else {
        heard.decoded = decodeManchester(hb);
    }
    if (monitor_ != nullptr) {
        monitor_->record({clock_.nowNs(), id_, src, heard.dropped, heard.decoded});
    }
    if (!heard.dropped) {
        for (std::size_t i = 0; i < count_; ++i) {
            RemoteTerminal* t = terminals_[i];
            if (!src.isBc && t->address() == src.rt) {
                continue;  // verici kendi kelimesini işlemez
            }
            t->onWord(id_, heard.decoded);
        }
    }
    return heard;
}

void Bus::endOfTransmission(Source src) {
    for (std::size_t i = 0; i < count_; ++i) {
        terminals_[i]->onEndOfTransmission(id_, src);
    }
}

RemoteTerminal* Bus::responder(bool& collision) const {
    RemoteTerminal* found = nullptr;
    collision = false;
    for (std::size_t i = 0; i < count_; ++i) {
        if (terminals_[i]->hasResponse(id_)) {
            if (found != nullptr) {
                collision = true;
            }
            found = terminals_[i];
        }
    }
    return found;
}

}  // namespace avb::m1553
