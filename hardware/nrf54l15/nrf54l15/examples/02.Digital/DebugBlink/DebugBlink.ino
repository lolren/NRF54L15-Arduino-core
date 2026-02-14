/*
 * Debug Blink Test for XIAO nRF54L15
 *
 * Tests LED and Serial output
 */

void setup() {
    // Initialize Serial first for debug output
    Serial.begin(115200);
    delay(1000);  // Wait for Serial to be ready
    Serial.println("XIAO nRF54L15 - Debug Blink Test");
    Serial.println("LED_BUILTIN = ");
    Serial.println(LED_BUILTIN);

    // Initialize the LED pin
    pinMode(LED_BUILTIN, OUTPUT);

    // Test both states
    Serial.println("Setting LED HIGH...");
    digitalWrite(LED_BUILTIN, HIGH);
    delay(2000);

    Serial.println("Setting LED LOW...");
    digitalWrite(LED_BUILTIN, LOW);
    delay(2000);

    Serial.println("Starting blink loop...");
}

void loop() {
    static int count = 0;

    Serial.print("Blink count: ");
    Serial.println(count++);

    digitalWrite(LED_BUILTIN, LOW);   // LED on (active low)
    Serial.println("LED ON");
    delay(500);

    digitalWrite(LED_BUILTIN, HIGH);  // LED off
    Serial.println("LED OFF");
    delay(500);
}
