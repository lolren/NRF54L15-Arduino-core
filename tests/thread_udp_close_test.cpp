#include <stdint.h>
#include <stdio.h>

#define private public
#include "nrf54_thread_experimental.cpp"
#undef private

namespace {

otError gCloseResult = OT_ERROR_NONE;
unsigned gCloseCalls = 0U;
otUdpSocket* gLastClosedSocket = nullptr;
unsigned gNewMessageCalls = 0U;
unsigned gSendCalls = 0U;
unsigned gFreeCalls = 0U;
otUdpSocket* gLastSendSocket = nullptr;
otMessageInfo gLastMessageInfo = {};

bool expect(bool condition, const char* message) {
  if (condition) {
    return true;
  }
  fprintf(stderr, "FAIL: %s\n", message);
  return false;
}

void receiveCallback(void*, const uint8_t*, uint16_t, const otMessageInfo&) {}

}  // namespace

uint32_t micros(void) { return 0U; }

extern "C" otError otUdpClose(otInstance*, otUdpSocket* socket) {
  ++gCloseCalls;
  gLastClosedSocket = socket;
  return gCloseResult;
}

extern "C" otMessage* otUdpNewMessage(otInstance*, const otMessageSettings*) {
  ++gNewMessageCalls;
  return reinterpret_cast<otMessage*>(static_cast<uintptr_t>(3U));
}

extern "C" otError otMessageAppend(otMessage*, const void*, uint16_t) {
  return OT_ERROR_NONE;
}

extern "C" void otMessageFree(otMessage*) { ++gFreeCalls; }

extern "C" otError otUdpSend(otInstance*, otUdpSocket* socket, otMessage*,
                              const otMessageInfo* messageInfo) {
  ++gSendCalls;
  gLastSendSocket = socket;
  gLastMessageInfo = *messageInfo;
  return OT_ERROR_NONE;
}

int main() {
  using xiao_nrf54l15::Nrf54ThreadExperimental;

  bool ok = true;
  Nrf54ThreadExperimental thread;

  ok &= expect(!thread.closeUdp(0U), "port zero must be rejected");
  ok &= expect(thread.lastUdpError_ == OT_ERROR_INVALID_ARGS,
               "port-zero rejection must report invalid args");

  ok &= expect(thread.closeUdp(61616U),
               "closing an unknown port must be idempotent");
  ok &= expect(thread.lastUdpError_ == OT_ERROR_NONE,
               "an idempotent close must clear the prior error");

  auto& requested = thread.udpSockets_[0];
  requested.port = 61616U;
  requested.callback = receiveCallback;
  requested.callbackContext =
      reinterpret_cast<void*>(static_cast<uintptr_t>(2U));
  requested.requested = true;
  requested.opened = false;
  ok &= expect(thread.closeUdp(61616U),
               "a queued, unopened socket must be cancellable");
  ok &= expect(requested.port == 0U && !requested.requested &&
                   !requested.opened && requested.callback == nullptr &&
                   requested.callbackContext == nullptr,
               "cancelling a queued socket must release the complete slot");
  ok &= expect(gCloseCalls == 0U,
               "a queued socket must not call OpenThread close");

  auto& opened = thread.udpSockets_[1];
  opened.port = 61617U;
  opened.requested = true;
  opened.opened = true;
  ok &= expect(!thread.closeUdp(61617U),
               "an open socket without an instance must fail closed");
  ok &= expect(thread.lastUdpError_ == OT_ERROR_INVALID_STATE,
               "missing instance must report invalid state");
  ok &= expect(opened.port == 61617U && opened.opened,
               "a failed close must retain the live slot");

  thread.instance_ = reinterpret_cast<otInstance*>(static_cast<uintptr_t>(1U));
  gCloseResult = OT_ERROR_BUSY;
  ok &= expect(!thread.closeUdp(61617U),
               "an OpenThread close error must propagate");
  ok &= expect(thread.lastUdpError_ == OT_ERROR_BUSY,
               "the OpenThread close error must be retained");
  ok &= expect(opened.port == 61617U && opened.opened,
               "an OpenThread close error must retain the slot");
  ok &= expect(gCloseCalls == 1U && gLastClosedSocket == &opened.socket,
               "the matching OpenThread socket must be closed");

  gCloseResult = OT_ERROR_NONE;
  ok &= expect(thread.closeUdp(61617U),
               "an open socket must close successfully");
  ok &= expect(thread.lastUdpError_ == OT_ERROR_NONE,
               "successful close must clear the prior error");
  ok &= expect(opened.port == 0U && !opened.requested && !opened.opened,
               "successful close must release the slot");
  ok &= expect(gCloseCalls == 2U,
               "successful close must call OpenThread exactly once");

  ok &= expect(thread.closeUdp(61617U),
               "repeated close must remain successful");
  ok &= expect(gCloseCalls == 2U,
               "repeated close must not close an already released socket");

  auto& fallback = thread.udpSockets_[2];
  fallback.port = 61618U;
  fallback.requested = true;
  fallback.opened = true;
  const uint8_t payload[] = {0x54U};
  const otIp6Address peer = {};
  ok &= expect(!thread.sendUdpFrom(61619U, peer, 61620U, payload,
                                   sizeof(payload)),
               "an unavailable explicit source port must fail closed");
  ok &= expect(thread.lastUdpError_ == OT_ERROR_INVALID_STATE,
               "an unavailable source port must report invalid state");
  ok &= expect(gNewMessageCalls == 0U && gSendCalls == 0U,
               "an explicit source port must never use an unrelated socket");

  ok &= expect(thread.sendUdpFrom(61618U, peer, 61620U, payload,
                                  sizeof(payload)),
               "an available explicit source port must send successfully");
  ok &= expect(gNewMessageCalls == 1U && gSendCalls == 1U &&
                   gFreeCalls == 0U,
               "a successful UDP send must transfer exactly one message");
  ok &= expect(gLastSendSocket == &fallback.socket &&
                   gLastMessageInfo.mSockPort == 61618U &&
                   gLastMessageInfo.mPeerPort == 61620U,
               "UDP send must preserve explicit endpoint identity");

  if (ok) {
    puts("PASS Thread UDP close lifecycle");
  }
  return ok ? 0 : 1;
}
