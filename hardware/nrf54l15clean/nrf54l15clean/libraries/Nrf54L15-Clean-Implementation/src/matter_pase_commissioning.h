#pragma once

#include <stddef.h>
#include <stdint.h>

#include "matter_manual_pairing.h"
#include "matter_pase_transport.h"
#include "matter_secp256r1.h"
#include "matter_pbkdf2.h"
#include "matter_credentials.h"
#include "matter_message_counter.h"
#include "matter_rng.h"

namespace xiao_nrf54l15 {

// Experimental PASE-like two-core demonstration surface. The framing and
// transcript below are not a Matter wire-compatible SPAKE2+ implementation.
constexpr size_t kMatterSpake2pHashSize = 32U;
constexpr size_t kMatterSpake2pWScalarSize = 32U;    // w0, w1 are scalars
constexpr size_t kMatterSpake2pW0Length = 32U;
constexpr size_t kMatterSpake2pW1Length = 32U;
constexpr size_t kMatterSpake2pPointSize = 65U;      // Uncompressed secp256r1
constexpr size_t kMatterSpake2pConfirmationSize = 32U;
constexpr size_t kMatterSpake2pSaltSize = 32U;
constexpr uint32_t kMatterSpake2pPbkdf2Iterations = 2000U;
constexpr uint32_t kMatterSpake2pMinPbkdf2Iterations = 1000U;
constexpr uint32_t kMatterSpake2pMaxPbkdf2Iterations = 100000U;

// Minimal demonstration message framing constants
enum class MatterMessageExchangeFlags : uint8_t {
  kNone = 0x00U,
  kInitiator = 0x01U,
  kAck = 0x02U,
  kReliable = 0x04U,
  kDuplicate = 0x08U,
};

enum class MatterMessageProtocol : uint16_t {
  kSecureChannel = 0x0000U,
  kInteractionModel = 0x0001U,
  kBdx = 0x0002U,
  kUserDirectedCommissioning = 0x0003U,
  kEcho = 0x0004U,
};

enum class MatterMessageType : uint8_t {
  // Secure Channel
  kMsgCounterSyncReq = 0x00U,
  kMsgCounterSyncResp = 0x01U,
  kStandaloneAck = 0x10U,
  kPBKDFParamRequest = 0x20U,
  kPBKDFParamResponse = 0x21U,
  kPaseSpake2p1 = 0x22U,
  kPaseSpake2p2 = 0x23U,
  kPaseSpake2p3 = 0x24U,
  kPaseSpake2pError = 0x2FU,
  kSigma1 = 0x30U,
  kSigma2 = 0x31U,
  kSigma3 = 0x32U,
  kSigma2Resume = 0x33U,
  kStatusReport = 0x40U,

  // Interaction Model
  kReadRequest = 0x02U,
  kReportData = 0x05U,
  kInvokeCommandRequest = 0x08U,
  kInvokeCommandResponse = 0x09U,
  kTimedRequest = 0x0AU,
};

// Commissioning flow state
enum class MatterCommissioningState : uint8_t {
  kIdle = 0U,
  kDiscovering = 1U,
  kPasePbkdfParamsSent = 2U,
  kPaseSpake2pInProgress = 3U,
  kPaseComplete = 4U,
  kSigmaInProgress = 5U,
  kNocSent = 6U,
  kCommissioned = 7U,
  kFailed = 8U,
};

enum class MatterCommissioningError : uint32_t {
  kNone = 0U,
  kInvalidArgument = 1U,
  kInvalidMessage = 2U,
  kUnexpectedMessage = 3U,
  kCryptoFailed = 4U,
  kAuthenticationFailed = 5U,
  kTransportFailed = 6U,
};

// Demonstration message header
struct MatterMessageHeader {
  uint8_t exchangeFlags = 0U;
  uint8_t sessionType = 0U;
  uint8_t securityFlags = 0U;
  uint16_t messageId = 0U;
  uint32_t sourceNodeId = 0U;
  uint32_t destNodeId = 0U;
  uint16_t exchangeId = 0U;
  uint16_t protocolId = 0U;
  uint8_t  protocolOpcode = 0U;
  uint16_t ackedMessageId = 0U;
};

// PBKDF param structures
struct MatterPbkdfParamRequest {
  uint8_t initiatorRandom[32] = {0};
  uint16_t initiatorSessionId = 0U;
  uint16_t passcodeId = 0U;
  uint8_t hasPbkdfParameters = 0;
  uint32_t idleRetransTimeoutMs = 0U;
  uint32_t activeRetransTimeoutMs = 0U;
};

struct MatterPbkdfParamResponse {
  uint8_t responderRandom[32] = {0};
  uint16_t responderSessionId = 0U;
  uint16_t passcodeId = 0U;
  uint8_t hasPbkdfParameters = 0;
  uint32_t idleRetransTimeoutMs = 0U;
  uint32_t activeRetransTimeoutMs = 0U;
};

// SPAKE2+ verifier state: w0, L = w1 * G
struct MatterSpake2pVerifier {
  uint8_t w0[kMatterSpake2pW0Length] = {0};
  uint8_t L[kMatterSpake2pPointSize] = {0};  // L = w1 * G (uncompressed)
  uint8_t salt[kMatterSpake2pSaltSize] = {0};
  uint32_t iterations = kMatterSpake2pPbkdf2Iterations;
  bool valid = false;
};

// Session state for PASE commissioning
struct MatterPaseSessionState {
  bool active = false;
  bool initiator = false;
  MatterCommissioningState state = MatterCommissioningState::kIdle;
  uint16_t localSessionId = 0U;
  uint16_t peerSessionId = 0U;
  uint16_t passcodeId = 0U;
  uint32_t setupPinCode = 0U;

  // SPAKE2+ derived keys
  uint8_t w0[kMatterSpake2pW0Length] = {0};    // Verifier's w0
  uint8_t w1[kMatterSpake2pW1Length] = {0};    // Prover's w1
  uint8_t ws[kMatterSpake2pHashSize] = {0};    // From PBKDF2
  uint8_t L[kMatterSpake2pPointSize] = {0};    // L = w1 * G (public)
  uint8_t salt[kMatterSpake2pSaltSize] = {0};
  uint32_t pbkdf2Iterations = kMatterSpake2pPbkdf2Iterations;

  // ECC points (uncompressed format)
  uint8_t X[kMatterSpake2pPointSize] = {0};    // Prover X = x*G + w0*G
  uint8_t Y[kMatterSpake2pPointSize] = {0};    // Verifier Y = y*G + w0*G
  uint8_t Z[kMatterSpake2pPointSize] = {0};    // Shared: Z = x*(Y-w0*G) = y*(X-w0*G)
  uint8_t V[kMatterSpake2pPointSize] = {0};    // Shared: V = w1*(Y-w0*G) = y*L

  // Ephemeral scalar (x for prover, y for verifier) must be reused for Z.
  uint8_t ephemeralScalar[kMatterSpake2pWScalarSize] = {0};

  // Shared secret and keys
  uint8_t sharedSecret[kMatterSpake2pHashSize] = {0};
  uint8_t ke[kMatterSpake2pHashSize] = {0};     // Encryption key
  uint8_t kcA[kMatterSpake2pHashSize] = {0};    // Prover confirmation key
  uint8_t kcB[kMatterSpake2pHashSize] = {0};    // Verifier confirmation key

  // Exchange randoms
  uint8_t initiateRandom[32] = {0};
  uint8_t respondRandom[32] = {0};

  // Confirmations
  uint8_t cA[kMatterSpake2pConfirmationSize] = {0};
  uint8_t cB[kMatterSpake2pConfirmationSize] = {0};
};

class MatterPaseCommissioning {
 public:
  using CommissioningCallback =
      void (*)(void* context, MatterCommissioningState state,
               uint32_t errorCode);

  MatterPaseCommissioning() = default;

  // Commissionee (verifier): the device being commissioned
  bool beginAsCommissionee(MatterPaseTransport* platform,
                           CommissioningCallback callback = nullptr,
                           void* context = nullptr);

  // Commissioner (prover): the device doing the commissioning
  bool beginAsCommissioner(MatterPaseTransport* platform,
                           CommissioningCallback callback = nullptr,
                           void* context = nullptr);

  void end();
  void process();
  bool active() const;
  MatterCommissioningState state() const;
  const char* stateName() const;

  // Set the setup PIN code for PASE
  bool setPasscode(uint32_t passcode);
  bool setDiscriminator(uint16_t discriminator);

  // Derive a verifier from a passcode for this experimental surface.
  static bool deriveVerifier(uint32_t passcode,
                             const uint8_t salt[kMatterSpake2pSaltSize],
                             uint32_t iterations,
                             MatterSpake2pVerifier* outVerifier);

  // Send PBKDF param request (commissioner initiates)
  bool sendPbkdfParamRequest(const otIp6Address& peerAddr,
                             uint16_t peerPort,
                             uint32_t setupPinCode);

  // Send PBKDF param response (commissionee responds)
  bool sendPbkdfParamResponse(const otIp6Address& peerAddr,
                              uint16_t peerPort);

  // Initiate SPAKE2+ from commissioner side (after PBKDF params exchanged)
  bool initiateSpake2p(const otIp6Address& peerAddr, uint16_t peerPort);

  // Get session shared secret (available after PASE complete)
  bool getSharedSecret(uint8_t outSharedSecret[kMatterSpake2pHashSize]) const;

  static const char* stateName(MatterCommissioningState state);

 private:
  enum class ExpectedMessage : uint8_t {
    kNone = 0U,
    kPbkdfParamRequest,
    kPbkdfParamResponse,
    kSpake2p1,
    kSpake2p2,
    kSpake2p3,
  };

  static void handleUdpReceive(void* context,
                               const uint8_t* payload, uint16_t length,
                               const otIp6Address& source,
                               uint16_t sourcePort);

  void handleMessage(const uint8_t* payload, uint16_t length,
                     const otIp6Address& source, uint16_t sourcePort);
  void handlePbkdfParamRequest(const uint8_t* payload, uint16_t length,
                               const otIp6Address& source, uint16_t sourcePort);
  void handlePbkdfParamResponse(const uint8_t* payload, uint16_t length,
                                const otIp6Address& source, uint16_t sourcePort);
  void handleSpake2p1(const uint8_t* payload, uint16_t length,
                      const otIp6Address& source, uint16_t sourcePort);
  void handleSpake2p2(const uint8_t* payload, uint16_t length,
                      const otIp6Address& source, uint16_t sourcePort);
  void handleSpake2p3(const uint8_t* payload, uint16_t length);

  // SPAKE2+ cryptographic operations
  bool computeSpake2pX();
  bool computeSpake2pY();
  bool computeSpake2pZ(bool responderVerifier);
  bool deriveSharedSecret();
  bool verifyConfirmationB();
  bool verifyConfirmationA();
  bool generateConfirmationA();
  bool generateConfirmationB();

  // Message framing
  bool parseMessageHeader(const uint8_t* payload, uint16_t length,
                          MatterMessageHeader* outHeader,
                          size_t* outPayloadOffset = nullptr) const;
  bool buildMessageHeader(const MatterMessageHeader& header,
                          uint8_t* outBuffer, size_t outCapacity,
                          size_t* outLength = nullptr) const;
  bool sendMessage(const otIp6Address& peerAddr, uint16_t peerPort,
                   const MatterMessageHeader& header,
                   const uint8_t* appPayload, uint16_t appPayloadLength);

  bool messageExpected(const MatterMessageHeader& header,
                       const otIp6Address& source,
                       uint16_t sourcePort) const;
  bool messagePayloadValid(const MatterMessageHeader& header,
                           const uint8_t* payload,
                           uint16_t length) const;
  bool peerMatches(const otIp6Address& source, uint16_t sourcePort) const;
  void bindPeer(const otIp6Address& source, uint16_t sourcePort,
                uint16_t exchangeId);
  void fail(MatterCommissioningError error);

  uint16_t nextExchangeId();
  uint16_t nextMessageId();
  bool generateRandom(uint8_t* output, size_t length);
  void advanceState(MatterCommissioningState newState,
                    uint32_t errorCode = 0U);

  MatterPaseTransport* platform_ = nullptr;
  CommissioningCallback callback_ = nullptr;
  void* callbackContext_ = nullptr;
  bool initiator_ = false;

  uint16_t localExchangeId_ = 0U;
  uint16_t peerExchangeId_ = 0U;
  uint16_t localMessageId_ = 0U;
  uint16_t peerMessageId_ = 0U;
  uint16_t peerPort_ = 0U;
  otIp6Address peerAddr_ = {};
  bool peerBound_ = false;
  MatterMessageCounter16 peerMessageCounter_;
  ExpectedMessage expectedMessage_ = ExpectedMessage::kNone;

  MatterPaseSessionState session_ = {};
  uint32_t setupPinCode_ = kDefaultSetupPinCode;
  uint16_t discriminator_ = kDefaultDiscriminator;

  // Pre-computed verifier (commissioner side)
  MatterSpake2pVerifier verifier_ = {};
};

}  // namespace xiao_nrf54l15
