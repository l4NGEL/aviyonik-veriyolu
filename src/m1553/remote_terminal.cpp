#include "avb/m1553/remote_terminal.hpp"

#include <algorithm>

namespace avb::m1553 {

RemoteTerminal::RemoteTerminal(std::uint8_t address) : address_(static_cast<std::uint8_t>(address & 0x1FU)) {}

bool RemoteTerminal::setTransmitData(std::uint8_t subaddress, const std::uint16_t* data, std::size_t count) {
    // 0 ve 31 mod kodu alt adresi, veri tutmaz
    if (subaddress == 0 || subaddress >= 31 || count > kMaxDataWords || (count > 0 && data == nullptr)) {
        return false;
    }
    TxBuffer& t = tx_[subaddress];
    std::copy_n(data, count, t.data.begin());
    t.count = static_cast<std::uint8_t>(count);
    return true;
}

std::size_t RemoteTerminal::readReceivedData(std::uint8_t subaddress, std::uint16_t* out, std::size_t capacity,
                                             std::uint32_t* seq) const {
    if (subaddress >= kSubaddressCount) {
        return 0;
    }
    const RxBuffer& r = rx_[subaddress];
    const std::size_t n = std::min<std::size_t>(r.count, capacity);
    if (out != nullptr) {
        std::copy_n(r.data.begin(), n, out);
    }
    if (seq != nullptr) {
        *seq = r.seq;
    }
    return n;
}

StatusWord RemoteTerminal::currentStatus() const {
    StatusWord s;
    s.rt = address_;
    s.messageError = messageError_;
    s.serviceRequest = serviceRequest_;
    s.broadcastReceived = broadcastReceived_;
    s.busy = busy_;
    s.subsystemFlag = subsystemFlag_;
    s.terminalFlag = terminalFlag_;
    return s;
}

// ---------------------------------------------------------------- hat tarafı

void RemoteTerminal::onWord(BusId bus, const DecodeResult& r) {
    if (!r.ok()) {
        // Komut kelimesindeki hata adresi belirsiz kılar: yok sayılır. Mesaj
        // ortasındaki hata mesajı geçersiz yapar.  LLR-1553-022
        if (state_ != State::Idle && bus == activeBus_) {
            messageInvalid_ = true;
        }
        return;
    }
    const Word& w = r.word;
    switch (state_) {
        case State::Idle:
            if (w.sync == Sync::CommandStatus) {
                handleCommand(bus, CommandWord::decode(w.value));
            }
            return;

        case State::Receiving:
        case State::RtRtReceiving:
            if (w.sync == Sync::Data) {
                if (received_ < kMaxDataWords) {
                    scratch_[static_cast<std::size_t>(received_)] = w.value;
                }
                ++received_;
                return;
            }
            {
                const CommandWord c = CommandWord::decode(w.value);
                // RT->RT: alma komutunu hemen başka RT'ye gönder komutu izler.  LLR-1553-026
                if (state_ == State::Receiving && received_ == 0 && c.transmit && c.rt != address_ &&
                    !c.isBroadcast() && !c.isModeCode()) {
                    if (c.countOrCode != cmd_.countOrCode) {
                        messageInvalid_ = true;
                    }
                    rtRtTransmitCmd_ = c;
                    state_ = State::RtRtWaitStatus;
                    return;
                }
                // Beklenmeyen yeni komut öncekini geçersiz kılar
                abortMessage();
                handleCommand(bus, c);
            }
            return;

        case State::RtRtWaitStatus:
            if (w.sync == Sync::CommandStatus) {
                const StatusWord s = StatusWord::decode(w.value);
                if (s.rt != rtRtTransmitCmd_.rt) {
                    messageInvalid_ = true;
                }
                state_ = State::RtRtReceiving;
            } else {
                messageInvalid_ = true;
            }
            return;
    }
}

void RemoteTerminal::onEndOfTransmission(BusId bus, Source src) {
    if (bus != activeBus_) {
        return;
    }
    if (state_ == State::Receiving && src.isBc) {
        completeReception(bus);
    } else if (state_ == State::RtRtReceiving && !src.isBc && src.rt == rtRtTransmitCmd_.rt) {
        completeReception(bus);
    } else if (state_ == State::RtRtWaitStatus && !src.isBc && src.rt == rtRtTransmitCmd_.rt) {
        abortMessage();  // gönderen RT'nin durum kelimesi hiç çözülemedi
    }
}

void RemoteTerminal::onMessageBoundary(BusId /*bus*/) {
    // Yeni mesaj başlarken hâlâ veri bekleniyorsa RT zaman aşımına düşmüştür
    if (state_ != State::Idle) {
        abortMessage();
    }
    response_.pending = false;
}

void RemoteTerminal::abortMessage() {
    state_ = State::Idle;
    messageError_ = true;
    ++invalidMessages_;
}

void RemoteTerminal::completeReception(BusId bus) {
    state_ = State::Idle;
    if (messageInvalid_ || received_ != expected_) {
        // Geçersiz mesaj: durum kelimesi bastırılır, ME biti sonraki durum isteğinde görünür
        messageError_ = true;
        ++invalidMessages_;
        return;
    }
    if (cmd_.isModeCode()) {
        if (cmd_.countOrCode == mode::kSynchronizeWithData) {
            ++syncCount_;
            syncData_ = scratch_[0];
        }
    } else if (!busy_) {
        RxBuffer& rx = rx_[cmd_.subaddress];
        std::copy_n(scratch_.begin(), expected_, rx.data.begin());
        rx.count = static_cast<std::uint8_t>(expected_);
        ++rx.seq;
    }
    if (cmd_.isBroadcast()) {
        broadcastReceived_ = true;  // yayına yanıt verilmez  LLR-1553-024
        return;
    }
    respond(bus, nullptr, 0);
}

void RemoteTerminal::beginFreshStatus(const CommandWord& c) {
    cmd_ = c;
    lastCommand_ = c.encode();
    messageError_ = false;
    broadcastReceived_ = false;
}

void RemoteTerminal::handleCommand(BusId bus, const CommandWord& c) {
    if (c.rt != address_ && !c.isBroadcast()) {
        return;  // başka terminale  LLR-1553-020
    }
    activeBus_ = bus;
    messageInvalid_ = false;
    received_ = 0;
    response_.pending = false;

    if (c.isModeCode()) {
        handleModeCode(bus, c);
        return;
    }
    beginFreshStatus(c);
    if (c.transmit) {
        if (c.isBroadcast()) {
            // Tüm RT'ler aynı anda gönderemez: geçersiz komut, yanıt yok
            messageError_ = true;
            ++invalidMessages_;
            return;
        }
        if (busy_) {
            respond(bus, nullptr, 0);  // meşgul: yalnız durum  LLR-1553-028
            return;
        }
        respond(bus, tx_[c.subaddress].data.data(), static_cast<std::size_t>(c.dataWordCount()));
        return;
    }
    expected_ = c.dataWordCount();
    state_ = State::Receiving;
}

void RemoteTerminal::handleModeCode(BusId bus, const CommandWord& c) {
    const std::uint8_t code = c.countOrCode;
    const bool bc = c.isBroadcast();
    const bool implemented = code == mode::kSynchronize || code == mode::kTransmitStatusWord ||
                             code == mode::kTransmitterShutdown || code == mode::kOverrideTransmitterShutdown ||
                             code == mode::kResetRemoteTerminal || code == mode::kTransmitVectorWord ||
                             code == mode::kSynchronizeWithData || code == mode::kTransmitLastCommand ||
                             code == mode::kTransmitBitWord;
    // 17 dışındaki tüm desteklenen kodlarda T/R=1 olmalı
    const bool expectTransmit = code != mode::kSynchronizeWithData;
    const bool broadcastAllowed = code == mode::kSynchronize || code == mode::kTransmitterShutdown ||
                                  code == mode::kOverrideTransmitterShutdown ||
                                  code == mode::kResetRemoteTerminal || code == mode::kSynchronizeWithData;

    if (!implemented || c.transmit != expectTransmit || (bc && !broadcastAllowed)) {
        // Geçersiz komut: ME set, yalnız durum kelimesi (yayında yanıt yok)  LLR-1553-030
        cmd_ = c;
        lastCommand_ = c.encode();
        messageError_ = true;
        broadcastReceived_ = false;
        ++invalidMessages_;
        if (!bc) {
            respond(bus, nullptr, 0);
        }
        return;
    }

    if (code == mode::kTransmitStatusWord) {
        // Durum yazmacı güncellenmeden döner: önceki mesajın ME/BCR bitleri okunur  LLR-1553-032
        cmd_ = c;
        lastCommand_ = c.encode();
        respond(bus, nullptr, 0);
        return;
    }
    if (code == mode::kTransmitLastCommand) {
        const std::uint16_t last = lastCommand_;  // ne durum ne son komut güncellenir
        respond(bus, &last, 1);
        return;
    }

    beginFreshStatus(c);
    switch (code) {
        case mode::kSynchronize:
            ++syncCount_;
            break;
        case mode::kTransmitterShutdown:
            txEnabled_[static_cast<int>(other(bus))] = false;  // LLR-1553-034
            break;
        case mode::kOverrideTransmitterShutdown:
            txEnabled_[static_cast<int>(other(bus))] = true;
            break;
        case mode::kTransmitVectorWord:
            respond(bus, &vectorWord_, 1);
            return;
        case mode::kTransmitBitWord:
            respond(bus, &bitWord_, 1);
            return;
        case mode::kSynchronizeWithData:
            expected_ = 1;
            state_ = State::Receiving;
            return;
        default:
            break;  // sıfırlama: yanıttan sonra
    }

    if (bc) {
        broadcastReceived_ = true;
    } else {
        respond(bus, nullptr, 0);
    }
    if (code == mode::kResetRemoteTerminal) {
        resetTerminal();  // durum kelimesi hazırlandıktan sonra sıfırlanır
    }
}

void RemoteTerminal::respond(BusId bus, const std::uint16_t* data, std::size_t count) {
    if (silent_ || !txEnabled_[static_cast<int>(bus)]) {
        return;  // verici kapalı  LLR-1553-034
    }
    response_.words[0] = {Sync::CommandStatus, currentStatus().encode()};
    for (std::size_t i = 0; i < count; ++i) {
        response_.words[i + 1] = {Sync::Data, data[i]};
    }
    response_.count = static_cast<std::uint8_t>(count + 1);
    response_.delayNs = responseDelayNs_;
    response_.bus = bus;
    response_.pending = true;
}

void RemoteTerminal::resetTerminal() {
    txEnabled_ = {{true, true}};
    messageError_ = false;
    broadcastReceived_ = false;
    state_ = State::Idle;
    ++resetCount_;
}

}  // namespace avb::m1553
