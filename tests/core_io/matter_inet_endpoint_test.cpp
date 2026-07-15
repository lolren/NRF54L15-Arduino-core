#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

#include <InetArduino.h>
#include <SystemLayerImplArduino.h>

unsigned long millis() { return 0U; }
unsigned long micros() { return 0U; }
uint64_t nrf54l15_core_monotonic_time_us() { return 0U; }

namespace {

void receiveCallback(chip::Inet::UDPEndPoint*,
                     chip::System::PacketBufferHandle&&,
                     const chip::Inet::IPPacketInfo&) {}

using FailStopCase = void (*)();

void expectFailStop(FailStopCase testCase) {
  const pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    testCase();
    _exit(0);
  }

  int status = 0;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status));
  assert(WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGABRT);
}

void endpointDestructorFailsStopWithLiveSocket() {
  xiao_nrf54l15::Nrf54ThreadExperimental thread;
  thread.closeResult = false;
  chip::Inet::UDPEndPointArduino endpoint;
  endpoint.Reset(&thread, 49152U, nullptr, 0U);
  assert(endpoint.Bind(chip::Inet::IPAddress::Any, 5540U) == CHIP_NO_ERROR);
}

void layerDestructorFailsStopAfterShutdownFailure() {
  chip::System::LayerImplArduino systemLayer;
  assert(systemLayer.Init() == CHIP_NO_ERROR);
  xiao_nrf54l15::Nrf54ThreadExperimental thread;
  thread.closeResult = false;
  chip::Inet::InetLayer inetLayer;
  inetLayer.SetThreadTransport(thread);
  assert(inetLayer.Init(systemLayer) == CHIP_NO_ERROR);
  chip::Inet::UDPEndPointArduino* endpoint = nullptr;
  assert(inetLayer.NewUDPEndPoint(&endpoint) == CHIP_NO_ERROR);
  assert(endpoint->Bind(chip::Inet::IPAddress::Any, 5540U) == CHIP_NO_ERROR);
  assert(inetLayer.ShutdownWithStatus() == CHIP_ERROR_INTERNAL);
}

}  // namespace

int main() {
  chip::System::LayerImplArduino systemLayer;
  assert(systemLayer.Init() == CHIP_NO_ERROR);

  xiao_nrf54l15::Nrf54ThreadExperimental thread;
  chip::Inet::InetLayer inetLayer;
  inetLayer.SetThreadTransport(thread);
  assert(inetLayer.Init(systemLayer) == CHIP_NO_ERROR);

  chip::Inet::UDPEndPointArduino* endpoint = nullptr;
  assert(inetLayer.NewUDPEndPoint(&endpoint) == CHIP_NO_ERROR);
  assert(endpoint != nullptr);
  assert(endpoint->Bind(chip::Inet::IPAddress::Any, 5540U) == CHIP_NO_ERROR);
  assert(endpoint->Listen() == CHIP_NO_ERROR);
  int appState = 1;
  endpoint->SetReceiveCallback(receiveCallback, &appState);

  thread.closeResult = false;
  endpoint->Close();
  assert(endpoint->GetLastCloseError() == CHIP_ERROR_INTERNAL);
  assert(endpoint->IsBound());
  assert(endpoint->IsListening());
  assert(endpoint->GetBoundPort() == 5540U);
  assert(endpoint->GetUDPEndPointAppState() == &appState);

  // A failed close also blocks rebinding and deletion so a live Thread socket
  // can never be silently replaced or its callback context recycled.
  assert(endpoint->Bind(chip::Inet::IPAddress::Any, 5541U) ==
         CHIP_ERROR_INTERNAL);
  assert(endpoint->GetBoundPort() == 5540U);
  assert(inetLayer.DeleteUDPEndPointWithStatus(endpoint) ==
         CHIP_ERROR_INTERNAL);

  chip::Inet::UDPEndPointArduino* secondEndpoint = nullptr;
  assert(inetLayer.NewUDPEndPoint(&secondEndpoint) == CHIP_NO_ERROR);
  assert(secondEndpoint != nullptr);
  assert(secondEndpoint != endpoint);

  thread.closeResult = true;
  assert(endpoint->CloseWithStatus() == CHIP_NO_ERROR);
  assert(endpoint->GetLastCloseError() == CHIP_NO_ERROR);
  assert(!endpoint->IsBound());
  assert(!endpoint->IsListening());
  assert(endpoint->GetBoundPort() == 0U);
  assert(endpoint->GetUDPEndPointAppState() == nullptr);
  assert(inetLayer.DeleteUDPEndPointWithStatus(endpoint) == CHIP_NO_ERROR);
  assert(inetLayer.DeleteUDPEndPointWithStatus(secondEndpoint) ==
         CHIP_NO_ERROR);

  chip::Inet::UDPEndPointArduino* reusedEndpoint = nullptr;
  assert(inetLayer.NewUDPEndPoint(&reusedEndpoint) == CHIP_NO_ERROR);
  assert(reusedEndpoint == endpoint);
  assert(reusedEndpoint->Bind(chip::Inet::IPAddress::Any, 5542U) ==
         CHIP_NO_ERROR);
  assert(reusedEndpoint->Listen() == CHIP_NO_ERROR);
  reusedEndpoint->SetReceiveCallback(receiveCallback, &appState);

  thread.closeResult = false;
  assert(inetLayer.ShutdownWithStatus() == CHIP_ERROR_INTERNAL);
  assert(inetLayer.GetLastShutdownError() == CHIP_ERROR_INTERNAL);
  assert(inetLayer.IsInitialized());
  assert(reusedEndpoint->IsBound());
  assert(reusedEndpoint->IsListening());
  assert(reusedEndpoint->GetBoundPort() == 5542U);
  assert(reusedEndpoint->GetUDPEndPointAppState() == &appState);
  chip::Inet::UDPEndPointArduino* endpointAfterFailedShutdown = nullptr;
  assert(inetLayer.NewUDPEndPoint(&endpointAfterFailedShutdown) ==
         CHIP_NO_ERROR);
  assert(endpointAfterFailedShutdown != reusedEndpoint);

  thread.closeResult = true;
  assert(inetLayer.ShutdownWithStatus() == CHIP_NO_ERROR);
  assert(inetLayer.GetLastShutdownError() == CHIP_NO_ERROR);
  assert(!inetLayer.IsInitialized());

  systemLayer.Shutdown();

  expectFailStop(endpointDestructorFailsStopWithLiveSocket);
  expectFailStop(layerDestructorFailsStopAfterShutdownFailure);
  return 0;
}
