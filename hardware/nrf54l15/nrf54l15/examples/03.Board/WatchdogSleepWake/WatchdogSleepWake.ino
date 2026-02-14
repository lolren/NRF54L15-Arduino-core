/*
 * WatchdogSleepWake
 *
 * Demonstrates watchdog setup/feed with low-power sleeps.
 *
 * Behavior:
 * - Starts watchdog with a 4-second timeout.
 * - Feeds watchdog once per second while sleeping between feeds.
 * - Press USER button to stop feeding; watchdog will reset the board.
 * - On reboot, reset cause is printed so watchdog reset can be verified.
 */

#include <XiaoNrf54L15.h>

static uint32_t g_ticks = 0;
static bool g_triggerReset = false;

static void printResetCause()
{
  const uint32_t cause = XiaoNrf54L15.resetCause();

  Serial.print("reset-cause-mask: 0x");
  Serial.println(cause, HEX);
  Serial.print("reset-was-watchdog: ");
  Serial.println(XiaoNrf54L15.resetWasWatchdog() ? "yes" : "no");

  XiaoNrf54L15.clearResetCause();
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.begin(115200);
  delay(250);

  Serial.println("watchdog-sleep: ready");
  printResetCause();

  if (!XiaoNrf54L15.watchdogStart(4000, false, true)) {
    Serial.print("watchdog-start-failed err=");
    Serial.println(XiaoNrf54L15.watchdogLastError());
  } else {
    Serial.println("watchdog started: timeout=4000ms pauseInSleep=no pauseInDebug=yes");
  }

  Serial.println("press USER button to stop feeding and force watchdog reset");
}

void loop()
{
  digitalWrite(LED_BUILTIN, ((g_ticks & 1U) != 0U) ? LOW : HIGH); // active-low LED

  if (digitalRead(PIN_BUTTON) == LOW) {
    g_triggerReset = true;
  }

  Serial.print("tick=");
  Serial.print(g_ticks++);
  Serial.print(" watchdog-active=");
  Serial.print(XiaoNrf54L15.watchdogActive() ? "yes" : "no");

  if (!g_triggerReset) {
    if (XiaoNrf54L15.watchdogFeed()) {
      Serial.println(" feed=ok");
    } else {
      Serial.print(" feed=fail err=");
      Serial.println(XiaoNrf54L15.watchdogLastError());
    }

    XiaoNrf54L15.sleepMs(1000);
    return;
  }

  Serial.println(" feed=stopped; waiting for watchdog reset...");
  XiaoNrf54L15.sleepMs(5000);
}
