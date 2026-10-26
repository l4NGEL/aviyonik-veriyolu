#include "avb/m1553/bus_controller.hpp"

#include <algorithm>

#include "avb/m1553/remote_terminal.hpp"

namespace avb::m1553 {

namespace {

std::uint8_t countField(std::uint8_t n) {
    // 32 kelime alanda 0 olarak kodlanır
    return n >= kMaxDataWords ? 0 : n;
}

std::uint8_t clampCount(std::uint8_t n) {
    if (n == 0) {
        return 1;
    }
    return n > kMaxDataWords ? kMaxDataWords : n;
}

bool modeCodeHasDataIn(std::uint8_t code) { return code >= 16 && code != mode::kSynchronizeWithData; }

int gcd(int a, int b) {
    while (b != 0) {
        const int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

}  // namespace

// ------------------------------------------------------------------- Message

Message Message::bcToRt(std::uint8_t rt, std::uint8_t sa, const std::uint16_t* d, std::uint8_t n) {
    Message m;
    m.kind = Kind::BcToRt;
    m.rt = rt;
    m.subaddress = sa;
    m.wordCount = clampCount(n);
    if (d != nullptr) {
        std::copy_n(d, m.wordCount, m.data.begin());
    }
    return m;
}

Message Message::rtToBc(std::uint8_t rt, std::uint8_t sa, std::uint8_t n) {
    Message m;
    m.kind = Kind::RtToBc;
    m.rt = rt;
    m.subaddress = sa;
    m.wordCount = clampCount(n);
    return m;
}

Message Message::rtToRt(std::uint8_t rxRt, std::uint8_t rxSa, std::uint8_t txRt, std::uint8_t txSa, std::uint8_t n) {
    Message m;
    m.kind = Kind::RtToRt;
    m.rt = rxRt;
    m.subaddress = rxSa;
    m.txRt = txRt;
    m.txSubaddress = txSa;
    m.wordCount = clampCount(n);
    return m;
}

Message Message::modeCodeMsg(std::uint8_t rt, std::uint8_t code, std::uint16_t dataWord) {
    Message m;
    m.kind = Kind::ModeCode;
    m.rt = rt;
    m.modeCode = code;
    m.data[0] = dataWord;
    return m;
}

const char* toString(Outcome o) {
    switch (o) {
        case Outcome::Ok:
            return "tamam";
        case Outcome::BroadcastSent:
            return "yayin";
        case Outcome::NoResponse:
            return "yanit-yok";
        case Outcome::ResponseError:
            return "yanit-hatali";
        case Outcome::AddressMismatch:
            return "adres-uyusmazligi";
        case Outcome::MessageError:
            return "mesaj-hatasi";
        case Outcome::Busy:
            return "mesgul";
    }
    return "?";
}

std::int64_t worstCaseDurationNs(const Message& m, std::int64_t gapNs) {
    constexpr std::int64_t W = Timing::kWordNs;
    constexpr std::int64_t R = Timing::kMaxResponseNs;
    const std::int64_t n = m.wordCount;
    const bool bc = m.rt == kBroadcastAddress;
    std::int64_t t = gapNs;
    switch (m.kind) {
        case Message::Kind::BcToRt:
            t += (1 + n) * W + (bc ? 0 : R + W);
            break;
        case Message::Kind::RtToBc:
            t += W + R + (1 + n) * W;
            break;
        case Message::Kind::RtToRt:
            t += 2 * W + R + (1 + n) * W + (bc ? 0 : R + W);
            break;
        case Message::Kind::ModeCode: {
            const bool out = m.modeCode == mode::kSynchronizeWithData;
            const bool in = modeCodeHasDataIn(m.modeCode);
            t += W + (out ? W : 0) + (bc ? 0 : R + W + (in ? W : 0));
            break;
        }
    }
    return t;
}

// ------------------------------------------------------------------ Schedule

bool Schedule::add(const Entry& e) {
    if (count_ >= kMaxEntries || e.period == 0 || e.phase >= e.period) {
        return false;
    }
    const int current = majorFrameLength();
    const int lcm = current / gcd(current, e.period) * e.period;
    if (lcm > kMaxMajorFrame) {
        return false;
    }
    entries_[count_++] = e;
    return true;
}

int Schedule::majorFrameLength() const {
    int lcm = 1;
    for (std::size_t i = 0; i < count_; ++i) {
        lcm = lcm / gcd(lcm, entries_[i].period) * entries_[i].period;
    }
    return lcm;
}

bool Schedule::runsIn(std::size_t i, int minorIndex) const {
    const Entry& e = entries_[i];
    return minorIndex % e.period == e.phase;
}

Schedule::Check Schedule::validate() const {
    Check c;
    const int major = majorFrameLength();
    for (int k = 0; k < major; ++k) {
        std::int64_t sum = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            if (runsIn(i, k)) {
                sum += worstCaseDurationNs(entries_[i].message);
            }
        }
        if (sum > c.worstNs) {
            c.worstNs = sum;
            c.worstMinor = k;
        }
    }
    c.worstUtilization = static_cast<double>(c.worstNs) / static_cast<double>(minorFrameNs_);
    c.ok = c.worstNs <= minorFrameNs_;
    return c;
}

// ------------------------------------------------------------- BusController

BusController::BusController(SimClock& clock, Bus& busA, Bus& busB) : clock_(clock), busA_(busA), busB_(busB) {}

Result BusController::execute(const Message& m) {
    Result r;
    BusId id = m.bus;
    for (int attemptNo = 0;; ++attemptNo) {
        r = attempt(m, id);
        r.retries = attemptNo;
        if (!retryable(r.outcome) || attemptNo >= maxRetries_) {
            break;
        }
        if (alternateOnRetry_) {
            id = other(id);  // LLR-1553-014
        }
    }

    ++stats_.messages;
    stats_.retries += static_cast<std::uint64_t>(r.retries);
    switch (r.outcome) {
        case Outcome::Ok:
        case Outcome::BroadcastSent:
            ++stats_.ok;
            if (r.retries > 0 && r.busUsed != m.bus) {
                ++stats_.recoveredOnAlternateBus;
            }
            break;
        case Outcome::NoResponse:
            ++stats_.noResponse;
            break;
        case Outcome::ResponseError:
        case Outcome::AddressMismatch:
            ++stats_.responseError;
            break;
        case Outcome::MessageError:
            ++stats_.messageError;
            break;
        case Outcome::Busy:
            ++stats_.busy;
            break;
    }
    return r;
}

Result BusController::attempt(const Message& m, BusId id) {
    Bus& b = bus(id);
    clock_.advance(gapNs_);
    b.startOfMessage();

    Result r;
    r.busUsed = id;
    r.startNs = clock_.nowNs();
    const Source bc = Source::bc();
    const std::uint8_t n = clampCount(m.wordCount);

    switch (m.kind) {
        case Message::Kind::BcToRt: {
            const CommandWord c{m.rt, false, m.subaddress, countField(n)};
            b.transmit({Sync::CommandStatus, c.encode()}, bc, 0);
            for (int i = 0; i < n; ++i) {
                b.transmit({Sync::Data, m.data[static_cast<std::size_t>(i)]}, bc, i + 1);
            }
            b.endOfTransmission(bc);
            r.outcome = c.isBroadcast() ? Outcome::BroadcastSent : collect(b, m.rt, 0, r.status, nullptr, nullptr);
            break;
        }
        case Message::Kind::RtToBc: {
            const CommandWord c{m.rt, true, m.subaddress, countField(n)};
            b.transmit({Sync::CommandStatus, c.encode()}, bc, 0);
            b.endOfTransmission(bc);
            r.outcome = collect(b, m.rt, n, r.status, &r.data, &r.dataCount);
            break;
        }
        case Message::Kind::RtToRt: {
            const CommandWord rx{m.rt, false, m.subaddress, countField(n)};
            const CommandWord tx{m.txRt, true, m.txSubaddress, countField(n)};
            b.transmit({Sync::CommandStatus, rx.encode()}, bc, 0);
            b.transmit({Sync::CommandStatus, tx.encode()}, bc, 1);
            b.endOfTransmission(bc);
            const Outcome txOut = collect(b, m.txRt, n, r.txStatus, &r.data, &r.dataCount);
            if (txOut != Outcome::Ok) {
                r.outcome = txOut;
            } else if (rx.isBroadcast()) {
                r.outcome = Outcome::BroadcastSent;
            } else {
                r.outcome = collect(b, m.rt, 0, r.status, nullptr, nullptr);
            }
            break;
        }
        case Message::Kind::ModeCode: {
            const bool withDataOut = m.modeCode == mode::kSynchronizeWithData;
            const CommandWord c{m.rt, !withDataOut, 31, m.modeCode};
            b.transmit({Sync::CommandStatus, c.encode()}, bc, 0);
            if (withDataOut) {
                b.transmit({Sync::Data, m.data[0]}, bc, 1);
            }
            b.endOfTransmission(bc);
            r.outcome = c.isBroadcast()
                            ? Outcome::BroadcastSent
                            : collect(b, m.rt, modeCodeHasDataIn(m.modeCode) ? 1 : 0, r.status, &r.data, &r.dataCount);
            break;
        }
    }
    r.endNs = clock_.nowNs();
    return r;
}

Outcome BusController::collect(Bus& b, std::uint8_t expectedRt, int expectedData, StatusWord& status,
                               std::array<std::uint16_t, kMaxDataWords>* data, std::uint8_t* dataCount) {
    bool collision = false;
    RemoteTerminal* rt = b.responder(collision);
    if (rt == nullptr) {
        clock_.advance(Timing::kNoResponseTimeoutNs);  // LLR-1553-011
        return Outcome::NoResponse;
    }
    if (collision) {
        clock_.advance(Timing::kNoResponseTimeoutNs);
        return Outcome::ResponseError;
    }
    const RemoteTerminal::Response resp = rt->response();
    rt->clearResponse();
    if (resp.delayNs > Timing::kNoResponseTimeoutNs) {
        // Geç gelen yanıt BC zaman aşımından sonra: yok sayılır
        clock_.advance(Timing::kNoResponseTimeoutNs);
        return Outcome::NoResponse;
    }
    clock_.advance(resp.delayNs);

    const Source src = Source::terminal(rt->address());
    Outcome out = Outcome::Ok;
    std::uint8_t got = 0;
    for (int i = 0; i < resp.count; ++i) {
        const Bus::Heard h = b.transmit(resp.words[static_cast<std::size_t>(i)], src, i);
        const bool good = !h.dropped && h.decoded.ok();
        if (i == 0) {
            if (!good || h.decoded.word.sync != Sync::CommandStatus) {
                out = Outcome::ResponseError;
            } else {
                status = StatusWord::decode(h.decoded.word.value);
            }
        } else if (!good || h.decoded.word.sync != Sync::Data) {
            out = Outcome::ResponseError;
        } else if (data != nullptr && got < kMaxDataWords) {
            (*data)[got++] = h.decoded.word.value;
        }
    }
    b.endOfTransmission(src);
    if (dataCount != nullptr) {
        *dataCount = got;
    }

    if (out != Outcome::Ok) {
        return out;
    }
    if (status.rt != expectedRt) {
        return Outcome::AddressMismatch;  // LLR-1553-013
    }
    if (status.messageError) {
        return Outcome::MessageError;
    }
    if (status.busy) {
        return Outcome::Busy;
    }
    if (got != expectedData) {
        return Outcome::ResponseError;
    }
    return Outcome::Ok;
}

std::size_t BusController::runMinorFrame(const Schedule& s, int minorIndex, Result* results, std::size_t capacity) {
    const std::int64_t frameEnd = clock_.nowNs() + s.minorFrameNs();
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!s.runsIn(i, minorIndex)) {
            continue;
        }
        const Result r = execute(s.entry(i).message);
        if (results != nullptr && n < capacity) {
            results[n] = r;
        }
        ++n;
    }
    if (clock_.nowNs() > frameEnd) {
        ++stats_.frameOverruns;  // LLR-1553-062
    } else {
        clock_.advanceTo(frameEnd);
    }
    return n;
}

}  // namespace avb::m1553
