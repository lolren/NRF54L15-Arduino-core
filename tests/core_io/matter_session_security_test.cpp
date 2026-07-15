#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <deque>
#include <functional>
#include <utility>
#include <vector>

#define private public
#include "matter_pase_commissioning.h"
#undef private
#include "matter_case_session.h"

using xiao_nrf54l15::CaseCertificate;
using xiao_nrf54l15::CaseSessionKeys;
using xiao_nrf54l15::CaseSigma1;
using xiao_nrf54l15::CaseSigma2;
using xiao_nrf54l15::CaseSigma3;
using xiao_nrf54l15::CaseState;
using xiao_nrf54l15::MatterCaseSession;
using xiao_nrf54l15::MatterCommissioningState;
using xiao_nrf54l15::MatterPaseCommissioning;
using xiao_nrf54l15::MatterPaseTransport;
using xiao_nrf54l15::Secp256r1;
using xiao_nrf54l15::Secp256r1Point;
using xiao_nrf54l15::Secp256r1Scalar;
using xiao_nrf54l15::kMatterSpake2pConfirmationSize;
using xiao_nrf54l15::kMatterSpake2pPointSize;

namespace {

constexpr uint16_t kCommissionerPort = 5540U;
constexpr uint16_t kCommissioneePort = 5541U;
constexpr size_t kPaseHeaderSize = 20U;
constexpr uint8_t kSpake2p2Opcode = 0x23U;

otIp6Address makeAddress(uint8_t suffix) {
  otIp6Address address = {};
  address.mFields.m8[0] = 0xFDU;
  address.mFields.m8[15] = suffix;
  return address;
}

bool addressesEqual(const otIp6Address& lhs, const otIp6Address& rhs) {
  return memcmp(lhs.mFields.m8, rhs.mFields.m8,
                sizeof(lhs.mFields.m8)) == 0;
}

class QueuedTransport;

struct Datagram {
  QueuedTransport* sender = nullptr;
  otIp6Address destination = {};
  uint16_t destinationPort = 0U;
  std::vector<uint8_t> payload;
};

class QueuedNetwork {
 public:
  void attach(QueuedTransport* endpoint) { endpoints_.push_back(endpoint); }

  void enqueue(Datagram datagram) {
    if (mutator_) {
      mutator_(datagram);
    }
    queue_.push_back(datagram);
  }

  void setMutator(std::function<void(Datagram&)> mutator) {
    mutator_ = std::move(mutator);
  }

  void pump();

 private:
  std::vector<QueuedTransport*> endpoints_;
  std::deque<Datagram> queue_;
  std::function<void(Datagram&)> mutator_;
};

class QueuedTransport final : public MatterPaseTransport {
 public:
  QueuedTransport(QueuedNetwork* network, const otIp6Address& address,
                  uint16_t port)
      : network_(network), address_(address), port_(port) {
    assert(network_ != nullptr);
    network_->attach(this);
  }

  bool sendUdp(const uint8_t* payload, uint16_t length,
               const otIp6Address& destAddr,
               uint16_t destPort) override {
    if ((payload == nullptr && length != 0U) || destPort == 0U) {
      return false;
    }
    Datagram datagram = {};
    datagram.sender = this;
    datagram.destination = destAddr;
    datagram.destinationPort = destPort;
    datagram.payload.assign(payload, payload + length);
    network_->enqueue(std::move(datagram));
    return true;
  }

  bool setReceiveCallback(ReceiveCallback callback,
                          void* context = nullptr) override {
    callback_ = callback;
    callbackContext_ = context;
    return true;
  }

  bool matches(const otIp6Address& address, uint16_t port) const {
    return port_ == port && addressesEqual(address_, address);
  }

  void deliver(const Datagram& datagram) {
    assert(datagram.sender != nullptr);
    assert(callback_ != nullptr);
    callback_(callbackContext_, datagram.payload.data(),
              static_cast<uint16_t>(datagram.payload.size()),
              datagram.sender->address_, datagram.sender->port_);
  }

  const otIp6Address& address() const { return address_; }

 private:
  friend class QueuedNetwork;

  QueuedNetwork* network_ = nullptr;
  otIp6Address address_ = {};
  uint16_t port_ = 0U;
  ReceiveCallback callback_ = nullptr;
  void* callbackContext_ = nullptr;
};

void QueuedNetwork::pump() {
  size_t deliveries = 0U;
  while (!queue_.empty()) {
    assert(++deliveries <= 16U);
    Datagram datagram = std::move(queue_.front());
    queue_.pop_front();
    QueuedTransport* recipient = nullptr;
    for (QueuedTransport* endpoint : endpoints_) {
      if (endpoint->matches(datagram.destination,
                            datagram.destinationPort)) {
        recipient = endpoint;
        break;
      }
    }
    assert(recipient != nullptr);
    recipient->deliver(datagram);
  }
}

struct PaseCallbackState {
  MatterCommissioningState state = MatterCommissioningState::kIdle;
  uint32_t error = 0U;
};

void paseStateCallback(void* context, MatterCommissioningState state,
                       uint32_t error) {
  auto* callbackState = static_cast<PaseCallbackState*>(context);
  assert(callbackState != nullptr);
  callbackState->state = state;
  callbackState->error = error;
}

enum class PaseMutation {
  kNone,
  kTamperConfirmationB,
  kReflectConfirmationA,
};

void runPaseExchange(PaseMutation mutation, bool expectSuccess) {
  QueuedNetwork network;
  QueuedTransport commissionerTransport(&network, makeAddress(1U),
                                         kCommissionerPort);
  QueuedTransport commissioneeTransport(&network, makeAddress(2U),
                                         kCommissioneePort);
  MatterPaseCommissioning commissioner;
  MatterPaseCommissioning commissionee;
  PaseCallbackState commissionerCallback;
  PaseCallbackState commissioneeCallback;

  assert(commissioner.setPasscode(20202021UL));
  assert(commissionee.setPasscode(20202021UL));
  assert(commissioner.beginAsCommissioner(
      &commissionerTransport, paseStateCallback, &commissionerCallback));
  assert(commissionee.beginAsCommissionee(
      &commissioneeTransport, paseStateCallback, &commissioneeCallback));

  network.setMutator([&](Datagram& datagram) {
    if (datagram.payload.size() !=
            (kPaseHeaderSize + kMatterSpake2pPointSize +
             kMatterSpake2pConfirmationSize) ||
        datagram.payload[kPaseHeaderSize - 1U] != kSpake2p2Opcode) {
      return;
    }
    uint8_t* confirmation =
        datagram.payload.data() + kPaseHeaderSize + kMatterSpake2pPointSize;
    if (mutation == PaseMutation::kTamperConfirmationB) {
      confirmation[kMatterSpake2pConfirmationSize - 1U] ^= 0x80U;
    } else if (mutation == PaseMutation::kReflectConfirmationA) {
      assert(commissionee.generateConfirmationA());
      memcpy(confirmation, commissionee.session_.cA,
             kMatterSpake2pConfirmationSize);
    }
  });

  assert(commissioner.sendPbkdfParamRequest(
      commissioneeTransport.address(), kCommissioneePort, 20202021UL));
  network.pump();

  if (!expectSuccess) {
    assert(commissioner.state() == MatterCommissioningState::kFailed);
    assert(commissionerCallback.state == MatterCommissioningState::kFailed);
    assert(commissionerCallback.error != 0U);
    assert(commissionee.state() != MatterCommissioningState::kPaseComplete);
    return;
  }

  if (commissioner.state() != MatterCommissioningState::kPaseComplete ||
      commissionee.state() != MatterCommissioningState::kPaseComplete) {
    fprintf(stderr, "PASE states commissioner=%u/%lu commissionee=%u/%lu\n",
            static_cast<unsigned>(commissioner.state()),
            static_cast<unsigned long>(commissionerCallback.error),
            static_cast<unsigned>(commissionee.state()),
            static_cast<unsigned long>(commissioneeCallback.error));
  }
  assert(commissioner.state() == MatterCommissioningState::kPaseComplete);
  assert(commissionee.state() == MatterCommissioningState::kPaseComplete);
  assert(commissionerCallback.error == 0U);
  assert(commissioneeCallback.error == 0U);
  assert(memcmp(commissioner.session_.Z, commissionee.session_.Z,
                sizeof(commissioner.session_.Z)) == 0);
  assert(memcmp(commissioner.session_.V, commissionee.session_.V,
                sizeof(commissioner.session_.V)) == 0);
  assert(memcmp(commissioner.session_.sharedSecret,
                commissionee.session_.sharedSecret,
                sizeof(commissioner.session_.sharedSecret)) == 0);
  uint8_t commissionerSecret[32] = {0};
  uint8_t commissioneeSecret[32] = {0};
  assert(commissioner.getSharedSecret(commissionerSecret));
  assert(commissionee.getSharedSecret(commissioneeSecret));
  assert(memcmp(commissionerSecret, commissioneeSecret,
                sizeof(commissionerSecret)) == 0);
  assert(memcmp(commissioner.session_.kcA, commissioner.session_.kcB,
                sizeof(commissioner.session_.kcA)) != 0);
  assert(memcmp(commissioner.session_.cA, commissioner.session_.cB,
                sizeof(commissioner.session_.cA)) != 0);
  assert(memcmp(commissioner.session_.cA, commissionee.session_.cA,
                sizeof(commissioner.session_.cA)) == 0);
  assert(memcmp(commissioner.session_.cB, commissionee.session_.cB,
                sizeof(commissioner.session_.cB)) == 0);
}

void testSecpArithmeticBoundaries() {
  Secp256r1::BigNum256 one = {};
  Secp256r1::BigNum256 two = {};
  Secp256r1::bnSetOne(&one);
  Secp256r1::bnSetOne(&two);
  two.w[0] = 2U;

  const Secp256r1::BigNum256 prime = Secp256r1::primeP();
  Secp256r1::BigNum256 expectedPrimeMinusOne = {};
  Secp256r1::BigNum256 actualPrimeMinusOne = {};
  Secp256r1::bnSub(prime, one, &expectedPrimeMinusOne);
  Secp256r1::bnModSub(one, two, &actualPrimeMinusOne);
  assert(Secp256r1::bnEquals(expectedPrimeMinusOne,
                             actualPrimeMinusOne));

  const Secp256r1::BigNum256 order = Secp256r1::orderN();
  Secp256r1::BigNum256 orderMinusOne = {};
  Secp256r1::BigNum256 expectedOrderMinusTwo = {};
  Secp256r1::BigNum256 doubledOrderMinusOne = {};
  Secp256r1::bnSub(order, one, &orderMinusOne);
  Secp256r1::bnSub(order, two, &expectedOrderMinusTwo);
  Secp256r1::bnModAddN(orderMinusOne, orderMinusOne,
                       &doubledOrderMinusOne);
  assert(Secp256r1::bnEquals(expectedOrderMinusTwo,
                             doubledOrderMinusOne));
}

Secp256r1Scalar scalarFromByte(uint8_t value) {
  Secp256r1Scalar scalar = {};
  scalar.bytes[31] = value;
  return scalar;
}

struct CaseIdentities {
  Secp256r1Scalar initiatorPrivate = {};
  Secp256r1Scalar responderPrivate = {};
  CaseCertificate initiatorCertificate = {};
  CaseCertificate responderCertificate = {};
};

CaseIdentities makeCaseIdentities() {
  CaseIdentities identities;
  identities.initiatorPrivate = scalarFromByte(7U);
  identities.responderPrivate = scalarFromByte(11U);
  Secp256r1Point initiatorPublic = {};
  Secp256r1Point responderPublic = {};
  assert(Secp256r1::scalarMultiplyBase(identities.initiatorPrivate,
                                       &initiatorPublic));
  assert(Secp256r1::scalarMultiplyBase(identities.responderPrivate,
                                       &responderPublic));
  assert(MatterCaseSession::generateSelfSignedCert(
      identities.initiatorPrivate, initiatorPublic, 0xFFF1U, 1U,
      &identities.initiatorCertificate));
  assert(MatterCaseSession::generateSelfSignedCert(
      identities.responderPrivate, responderPublic, 0xFFF1U, 2U,
      &identities.responderCertificate));
  return identities;
}

void configureCaseSessions(const CaseIdentities& identities,
                           MatterCaseSession* initiator,
                           MatterCaseSession* responder) {
  assert(initiator != nullptr && responder != nullptr);
  assert(initiator->setCertificate(identities.initiatorCertificate,
                                   identities.initiatorPrivate));
  assert(initiator->setPeerCertificate(identities.responderCertificate));
  assert(responder->setCertificate(identities.responderCertificate,
                                   identities.responderPrivate));
  assert(responder->setPeerCertificate(identities.initiatorCertificate));
  assert(initiator->beginAsInitiator());
  assert(responder->beginAsResponder());
}

void advanceCaseToSigma2(MatterCaseSession* initiator,
                         MatterCaseSession* responder,
                         CaseSigma2* sigma2) {
  CaseSigma1 sigma1 = {};
  assert(initiator->buildSigma1(&sigma1));
  assert(responder->processSigma1(sigma1));
  assert(responder->buildSigma2(sigma2));
}

void testCaseRoundTripAndCapacity(const CaseIdentities& identities) {
  MatterCaseSession initiator;
  MatterCaseSession responder;
  configureCaseSessions(identities, &initiator, &responder);

  CaseSigma2 sigma2 = {};
  advanceCaseToSigma2(&initiator, &responder, &sigma2);
  assert(initiator.processSigma2(sigma2));
  CaseSigma3 sigma3 = {};
  assert(initiator.buildSigma3(&sigma3));
  assert(responder.processSigma3(sigma3));
  assert(initiator.state() == CaseState::kEstablished);
  assert(responder.state() == CaseState::kEstablished);

  CaseSessionKeys initiatorKeys = {};
  CaseSessionKeys responderKeys = {};
  assert(initiator.getSessionKeys(&initiatorKeys));
  assert(responder.getSessionKeys(&responderKeys));
  assert(memcmp(&initiatorKeys, &responderKeys,
                sizeof(initiatorKeys)) == 0);

  const uint8_t plaintext[] = {
      'c', 'a', 'p', 'a', 'c', 'i', 't', 'y', '-', 'c', 'h', 'e', 'c', 'k'};
  const uint8_t aad[] = {'a', 'a', 'd'};
  uint8_t ciphertext[sizeof(plaintext) + 8U];
  memset(ciphertext, 0xA5, sizeof(ciphertext));
  uint16_t ciphertextLength = 99U;
  assert(!initiator.encryptMessage(
      plaintext, sizeof(plaintext), aad, sizeof(aad), ciphertext,
      sizeof(ciphertext) - 1U, &ciphertextLength, true));
  assert(ciphertextLength == 0U);
  for (uint8_t value : ciphertext) assert(value == 0xA5U);

  assert(initiator.encryptMessage(
      plaintext, sizeof(plaintext), aad, sizeof(aad), ciphertext,
      sizeof(ciphertext), &ciphertextLength, true));
  assert(ciphertextLength == sizeof(ciphertext));

  uint8_t decrypted[sizeof(plaintext)];
  memset(decrypted, 0x5AU, sizeof(decrypted));
  uint16_t decryptedLength = 99U;
  assert(!responder.decryptMessage(
      ciphertext, ciphertextLength, aad, sizeof(aad), decrypted,
      sizeof(decrypted) - 1U, &decryptedLength, true));
  assert(decryptedLength == 0U);
  for (uint8_t value : decrypted) assert(value == 0x5AU);

  assert(responder.decryptMessage(
      ciphertext, ciphertextLength, aad, sizeof(aad), decrypted,
      sizeof(decrypted), &decryptedLength, true));
  assert(decryptedLength == sizeof(plaintext));
  assert(memcmp(decrypted, plaintext, sizeof(plaintext)) == 0);
}

void testCaseSigmaProofTamper(const CaseIdentities& identities) {
  {
    MatterCaseSession initiator;
    MatterCaseSession responder;
    configureCaseSessions(identities, &initiator, &responder);
    CaseSigma2 sigma2 = {};
    advanceCaseToSigma2(&initiator, &responder, &sigma2);
    sigma2.transcriptSignature[5] ^= 0x01U;
    assert(!initiator.processSigma2(sigma2));
    assert(initiator.state() == CaseState::kFailed);
  }

  {
    MatterCaseSession initiator;
    MatterCaseSession responder;
    configureCaseSessions(identities, &initiator, &responder);
    CaseSigma2 sigma2 = {};
    advanceCaseToSigma2(&initiator, &responder, &sigma2);
    assert(initiator.processSigma2(sigma2));
    CaseSigma3 sigma3 = {};
    assert(initiator.buildSigma3(&sigma3));
    memcpy(sigma3.transcriptSignature, sigma2.transcriptSignature,
           sizeof(sigma3.transcriptSignature));
    assert(!responder.processSigma3(sigma3));
    assert(responder.state() == CaseState::kFailed);
  }
}

}  // namespace

int main() {
  testSecpArithmeticBoundaries();
  runPaseExchange(PaseMutation::kNone, true);
  runPaseExchange(PaseMutation::kTamperConfirmationB, false);
  runPaseExchange(PaseMutation::kReflectConfirmationA, false);

  const CaseIdentities identities = makeCaseIdentities();
  testCaseRoundTripAndCapacity(identities);
  testCaseSigmaProofTamper(identities);
  return 0;
}
