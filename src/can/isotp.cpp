#include "avb/can/isotp.hpp"

#include <algorithm>

namespace avb::can::isotp {

namespace {

constexpr std::uint8_t kSingle = 0x0;
constexpr std::uint8_t kFirst = 0x1;
constexpr std::uint8_t kConsecutive = 0x2;
constexpr std::uint8_t kFlow = 0x3;

constexpr std::uint8_t kFsContinue = 0;
constexpr std::uint8_t kFsWait = 1;
constexpr std::uint8_t kFsOverflow = 2;

constexpr std::uint8_t kPad = 0xCC;

bool matches(const Addressing& a, const Frame& f) { return f.id == a.rxId && f.extended == a.extended; }

Frame makeFrame(const Addressing& a, const std::uint8_t* pci, std::size_t pciLen, const std::uint8_t* d,
                std::size_t n) {
    Frame f;
    f.id = a.txId;
    f.extended = a.extended;
    std::copy_n(pci, pciLen, f.data.begin());
    if (n > 0) {
        std::copy_n(d, n, f.data.begin() + static_cast<std::ptrdiff_t>(pciLen));
    }
    const std::size_t used = pciLen + n;
    if (a.padding) {
        std::fill(f.data.begin() + static_cast<std::ptrdiff_t>(used), f.data.end(), kPad);
        f.dlc = 8;
    } else {
        f.dlc = static_cast<std::uint8_t>(used);
    }
    return f;
}

}  // namespace

const char* toString(Error e) {
    switch (e) {
        case Error::None:
            return "yok";
        case Error::TimeoutBs:
            return "N_Bs-zaman-asimi";
        case Error::TimeoutCr:
            return "N_Cr-zaman-asimi";
        case Error::WrongSequence:
            return "yanlis-sira";
        case Error::Overflow:
            return "tasma";
        case Error::InvalidFlowStatus:
            return "gecersiz-akis-durumu";
        case Error::WaitLimit:
            return "bekleme-siniri";
        case Error::Busy:
            return "mesgul";
    }
    return "?";
}

std::int64_t stMinToUs(std::uint8_t code) {
    if (code <= 0x7F) {
        return static_cast<std::int64_t>(code) * 1000;
    }
    if (code >= 0xF1 && code <= 0xF9) {
        return static_cast<std::int64_t>(code - 0xF0) * 100;
    }
    return 127'000;  // ayrılmış değerler en uzun süre olarak yorumlanır
}

// -------------------------------------------------------------------- Sender

void Sender::emit(const std::uint8_t* pci, std::size_t pciLen, const std::uint8_t* d, std::size_t n) {
    if (send_ != nullptr) {
        send_(ctx_, makeFrame(addr_, pci, pciLen, d, n));
    }
}

void Sender::fail(Error e) {
    error_ = e;
    state_ = State::Failed;
}

bool Sender::start(const std::uint8_t* payload, std::size_t len, std::int64_t nowUs) {
    if (state_ == State::WaitFlowControl || state_ == State::Sending) {
        error_ = Error::Busy;
        return false;
    }
    if (len == 0 || len > kMaxPayload || payload == nullptr) {
        return false;
    }
    std::copy_n(payload, len, buf_.begin());
    len_ = len;
    error_ = Error::None;
    waits_ = 0;

    if (len <= 7) {
        const std::uint8_t pci[1] = {static_cast<std::uint8_t>((kSingle << 4) | len)};
        emit(pci, 1, buf_.data(), len);
        state_ = State::Done;
        return true;
    }
    const std::uint8_t pci[2] = {static_cast<std::uint8_t>((kFirst << 4) | ((len >> 8) & 0x0F)),
                                 static_cast<std::uint8_t>(len & 0xFF)};
    emit(pci, 2, buf_.data(), 6);
    offset_ = 6;
    seq_ = 1;
    state_ = State::WaitFlowControl;
    deadlineUs_ = nowUs + timeoutBsUs;
    return true;
}

void Sender::onFrame(const Frame& f, std::int64_t nowUs) {
    if (state_ != State::WaitFlowControl || !matches(addr_, f) || f.dlc < 3 || (f.data[0] >> 4) != kFlow) {
        return;
    }
    const std::uint8_t fs = f.data[0] & 0x0F;
    switch (fs) {
        case kFsContinue:
            blockSize_ = f.data[1];
            blockRemaining_ = blockSize_;
            stMinUs_ = stMinToUs(f.data[2]);
            state_ = State::Sending;
            nextSendUs_ = nowUs;
            tick(nowUs);
            return;
        case kFsWait:
            if (++waits_ > maxWait) {
                fail(Error::WaitLimit);
            } else {
                deadlineUs_ = nowUs + timeoutBsUs;  // LLR-CAN-022
            }
            return;
        case kFsOverflow:
            fail(Error::Overflow);
            return;
        default:
            fail(Error::InvalidFlowStatus);
            return;
    }
}

void Sender::tick(std::int64_t nowUs) {
    if (state_ == State::WaitFlowControl && nowUs > deadlineUs_) {
        fail(Error::TimeoutBs);  // LLR-CAN-023
        return;
    }
    while (state_ == State::Sending && nowUs >= nextSendUs_) {
        const std::size_t n = std::min<std::size_t>(7, len_ - offset_);
        const std::uint8_t pci[1] = {static_cast<std::uint8_t>((kConsecutive << 4) | seq_)};
        emit(pci, 1, buf_.data() + offset_, n);
        offset_ += n;
        seq_ = static_cast<std::uint8_t>((seq_ + 1) & 0x0F);  // 15'ten sonra 0
        if (offset_ >= len_) {
            state_ = State::Done;
            return;
        }
        if (blockSize_ != 0 && --blockRemaining_ == 0) {
            state_ = State::WaitFlowControl;
            deadlineUs_ = nowUs + timeoutBsUs;
            return;
        }
        nextSendUs_ = nowUs + stMinUs_;  // LLR-CAN-024
        if (stMinUs_ > 0) {
            return;
        }
    }
}

// ------------------------------------------------------------------ Receiver

void Receiver::sendFlowControl(std::uint8_t status) {
    if (send_ == nullptr) {
        return;
    }
    const std::uint8_t pci[3] = {static_cast<std::uint8_t>((kFlow << 4) | status), blockSize, stMin};
    send_(ctx_, makeFrame(addr_, pci, 3, nullptr, 0));
}

void Receiver::fail(Error e) {
    error_ = e;
    ++errors_;
    receiving_ = false;
}

void Receiver::onFrame(const Frame& f, std::int64_t nowUs) {
    if (!matches(addr_, f) || f.dlc < 1) {
        return;
    }
    const std::uint8_t type = static_cast<std::uint8_t>(f.data[0] >> 4);
    switch (type) {
        case kSingle: {
            const std::size_t n = f.data[0] & 0x0F;
            if (n == 0 || n > 7 || n + 1 > f.dlc) {
                return;  // geçersiz tek çerçeve yok sayılır
            }
            receiving_ = false;  // süren alım varsa yenisi onu iptal eder
            std::copy_n(f.data.begin() + 1, n, buf_.begin());
            len_ = n;
            complete_ = true;
            return;
        }
        case kFirst: {
            if (f.dlc < 8) {
                return;
            }
            const std::size_t n = (static_cast<std::size_t>(f.data[0] & 0x0F) << 8) | f.data[1];
            if (n < 8) {
                return;  // 8'den kısa yük tek çerçevede gelmeliydi
            }
            if (n > capacity) {
                sendFlowControl(kFsOverflow);
                fail(Error::Overflow);
                return;
            }
            std::copy_n(f.data.begin() + 2, 6, buf_.begin());
            len_ = n;
            got_ = 6;
            expectedSeq_ = 1;
            blockCount_ = 0;
            receiving_ = true;
            complete_ = false;
            deadlineUs_ = nowUs + timeoutCrUs;
            sendFlowControl(kFsContinue);
            return;
        }
        case kConsecutive: {
            if (!receiving_) {
                return;
            }
            const std::uint8_t seq = f.data[0] & 0x0F;
            if (seq != expectedSeq_) {
                fail(Error::WrongSequence);  // LLR-CAN-021
                return;
            }
            const std::size_t n = std::min<std::size_t>(7, len_ - got_);
            if (n + 1 > f.dlc) {
                fail(Error::WrongSequence);
                return;
            }
            std::copy_n(f.data.begin() + 1, n, buf_.begin() + static_cast<std::ptrdiff_t>(got_));
            got_ += n;
            expectedSeq_ = static_cast<std::uint8_t>((expectedSeq_ + 1) & 0x0F);
            if (got_ >= len_) {
                receiving_ = false;
                complete_ = true;
                return;
            }
            deadlineUs_ = nowUs + timeoutCrUs;
            if (blockSize != 0 && ++blockCount_ == blockSize) {
                blockCount_ = 0;
                sendFlowControl(kFsContinue);
            }
            return;
        }
        default:
            return;  // akış kontrolü alıcıyı ilgilendirmez
    }
}

void Receiver::tick(std::int64_t nowUs) {
    if (receiving_ && nowUs > deadlineUs_) {
        fail(Error::TimeoutCr);  // LLR-CAN-023
    }
}

}  // namespace avb::can::isotp
