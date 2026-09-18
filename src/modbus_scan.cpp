// โหมดวินิจฉัยเซนเซอร์ RS485 — คอมไพล์เฉพาะตอน build ด้วย env "scan"
//   pio run -e scan -t upload -t monitor
// ไล่ยิงทุกคู่ของ baud rate x ขา DE/RE เพื่อหาว่าคู่ไหนเซนเซอร์ตอบกลับ
// ไม่ถูกคอมไพล์เข้าเฟิร์มแวร์ปกติ จึงไม่กระทบขนาดหรือการทำงานจริง
#ifdef MODBUS_SCAN_MODE

#include <Arduino.h>
#include <ModbusMaster.h>
#include <esp_task_wdt.h>
#include "app_config.h"

// -1 = ไม่คุมทิศทาง (ชิลด์แบบ auto-direction)
// ที่เหลือคือขาที่ชิลด์ ESP32 มักใช้เป็น DE/RE เลี่ยง GPIO6-11 (ต่อกับ flash) และ 34-39 (input อย่างเดียว)
static const int kDePins[] = {-1, 4, 5, 13, 14, 15, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
static const uint32_t kBauds[] = {4800, 9600, 19200, 2400};

static int g_activeDePin = -1;

static void scanPreTransmission() {
    if (g_activeDePin >= 0) digitalWrite(g_activeDePin, HIGH);
}
static void scanPostTransmission() {
    if (g_activeDePin >= 0) digitalWrite(g_activeDePin, LOW);
}

static bool tryCombination(uint32_t baud, int dePin, uint8_t slaveId, int rxPin, int txPin) {
    g_activeDePin = dePin;

    Serial2.begin(baud, SERIAL_8N1, rxPin, txPin);

    ModbusMaster node;
    if (dePin >= 0) {
        pinMode(dePin, OUTPUT);
        digitalWrite(dePin, LOW);
        node.preTransmission(scanPreTransmission);
        node.postTransmission(scanPostTransmission);
    }
    node.begin(slaveId, Serial2);

    delay(50);
    node.clearResponseBuffer();
    uint8_t result = node.readHoldingRegisters(SENSOR_REG_ADDR, SENSOR_REG_COUNT);

    if (result == node.ku8MBSuccess) {
        uint16_t rawTurbidity = node.getResponseBuffer(0);
        int16_t rawTemp = (int16_t)node.getResponseBuffer(1);
        Serial.println();
        Serial.println("=======================================================");
        Serial.printf("  พบเซนเซอร์แล้ว!  baud=%u  DE/RE pin=%d  slave=%u  RX=%d TX=%d\n",
                      baud, dePin, slaveId, rxPin, txPin);
        Serial.printf("  ความขุ่น   : %.1f NTU (raw 0x%04X)\n", rawTurbidity * TURBIDITY_SCALE, rawTurbidity);
        Serial.printf("  อุณหภูมิ   : %.1f C   (raw 0x%04X)\n", rawTemp * TEMPERATURE_SCALE, (uint16_t)rawTemp);
        Serial.println("-------------------------------------------------------");
        Serial.println("  นำค่าไปใส่ใน include/app_config.h:");
        Serial.printf("    #define MODBUS_BAUD_RATE %u\n", baud);
        Serial.printf("    #define MODBUS_DE_RE_PIN %d\n", dePin);
        Serial.printf("    #define SENSOR_SLAVE_ID %u\n", slaveId);
        Serial.printf("    #define MODBUS_RX_PIN %d\n", rxPin);
        Serial.printf("    #define MODBUS_TX_PIN %d\n", txPin);
        Serial.println("=======================================================");
        return true;
    }

    Serial2.end();
    if (dePin >= 0) {
        pinMode(dePin, INPUT); // ปล่อยขาคืน เผื่อรอบถัดไปใช้ขาอื่น
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    esp_task_wdt_init(WDT_TIMEOUT_SECONDS, false); // ไม่ panic ระหว่างสแกน
    esp_task_wdt_add(NULL);

    Serial.println("\n=== MODBUS SCAN MODE ===");
    Serial.printf("RX=GPIO%d  TX=GPIO%d  register=0x%04X x%d\n",
                  MODBUS_RX_PIN, MODBUS_TX_PIN, SENSOR_REG_ADDR, SENSOR_REG_COUNT);
    Serial.println("ตรวจสอบก่อนเริ่ม: เซนเซอร์ได้ไฟ 12V แล้ว และรออุ่นเครื่องอย่างน้อย 5 วินาที\n");
    delay(5000); // warm-up ตามที่โค้ดเดิมทำ

    const int pinCount = sizeof(kDePins) / sizeof(kDePins[0]);
    const int baudCount = sizeof(kBauds) / sizeof(kBauds[0]);

    // ลองทั้งสองทิศทางของ RX/TX เผื่อชิลด์ต่อสลับกับที่เข้าใจ
    const int kRx[] = {MODBUS_RX_PIN, MODBUS_TX_PIN};
    const int kTx[] = {MODBUS_TX_PIN, MODBUS_RX_PIN};
    const int orientationCount = 2;

    const int total = orientationCount * baudCount * pinCount;
    int attempt = 0;

    for (int o = 0; o < orientationCount; o++) {
        Serial.printf("\n--- ทดสอบ RX=GPIO%d TX=GPIO%d ---\n", kRx[o], kTx[o]);
        for (int b = 0; b < baudCount; b++) {
            for (int p = 0; p < pinCount; p++) {
                esp_task_wdt_reset();
                attempt++;
                Serial.printf("[%3d/%3d] RX=%-2d baud=%-5u DE=%-3d ... ",
                              attempt, total, kRx[o], kBauds[b], kDePins[p]);

                if (tryCombination(kBauds[b], kDePins[p], SENSOR_SLAVE_ID, kRx[o], kTx[o])) {
                    return; // เจอแล้ว หยุดเลย
                }
                Serial.println("ไม่ตอบ");
            }
        }
    }

    Serial.println("\n!! ไม่เจอเซนเซอร์ในทุกคู่ที่ลอง");
    Serial.println("แปลว่าปัญหาน่าจะไม่ใช่ baud หรือขา DE/RE ให้ไล่เช็กฮาร์ดแวร์:");
    Serial.println("  1. เซนเซอร์มีไฟ 12V จริงหรือไม่ (วัดที่สายน้ำตาล-ดำ)");
    Serial.println("  2. สาย A/B สลับกันหรือไม่ (เหลือง=A, น้ำเงิน=B) ลองสลับดู");
    Serial.println("  3. GND ของชิลด์กับของเซนเซอร์ต่อถึงกันหรือไม่");
    Serial.println("  4. ขา RX/TX ระหว่าง ESP32 กับชิลด์สลับกันหรือไม่ (ลองสลับ 16 กับ 17)");
    Serial.println("  5. เซนเซอร์เคยถูกตั้ง slave address ใหม่หรือไม่ (ค่าปกติคือ 1)");
}

void loop() {
    esp_task_wdt_reset();
    delay(1000);
}

#endif // MODBUS_SCAN_MODE
