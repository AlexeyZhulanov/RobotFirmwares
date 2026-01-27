#include <SoftwareSerial.h> // Библиотека для "виртуального" Serial порта

// --- Настройки RS-485 ---
#define RS485_RX_PIN 6
#define RS485_TX_PIN 7
#define SLAVE_ADDRESS 1 // Уникальный адрес этого устройства в сети

// Создаем "виртуальный" Serial порт для общения по RS-485
SoftwareSerial rs485Serial(RS485_RX_PIN, RS485_TX_PIN);

void setup() {
  // Запускаем основной Serial порт для отладки в Мониторе порта
  Serial.begin(9600);
  // Ждем открытия Монитора порта, чтобы не пропустить первые сообщения
  while (!Serial) { ; }
  Serial.println("[Slave] Arduino Uno ready. Address: " + String(SLAVE_ADDRESS));
  // Запускаем наш RS-485 порт
  rs485Serial.begin(9600);
  Serial.println("[Slave] Waiting for commands...");
}

void loop() {
  // Постоянно проверяем, не пришла ли команда от Master
  if (rs485Serial.available()) {
    // Читаем входящую строку до символа новой строки '\n'
    String packet = rs485Serial.readStringUntil('\n');
    packet.trim(); // Убираем лишние пробелы
    Serial.print("[Slave RAW DATA] Received: <");
    Serial.print(packet);
    Serial.println(">");

    if (packet.length() > 0) {
      //Serial.print("Received packet: ");
      //Serial.println(packet);
      parseAndExecute(packet);
    }
  }
}

void sendResponse(const String& response) {
  rs485Serial.print(response);
  rs485Serial.flush();
  Serial.print("[Slave] Sent Response: " + response);
}

// Главная функция - парсит пакет и выполняет команду
void parseAndExecute(const String& packet) {
  // Формат пакета: "ID:КОМАНДА_С_ПИНАМИ:ЗНАЧЕНИЕ"
  // Пример: "1:uno_motor_9_8:-255"

  int id_separator = packet.indexOf(':');
  if (id_separator == -1) return;
  byte id = packet.substring(0, id_separator).toInt();
  if (id != SLAVE_ADDRESS) return;

  int cmd_separator = packet.indexOf(':', id_separator + 1);
  if (cmd_separator == -1) return;
  
  String command = packet.substring(id_separator + 1, cmd_separator);
  int value = packet.substring(cmd_separator + 1).toInt();

  Serial.println("-> Command for me! Cmd: '" + command + "', Val: " + String(value));

  if (command == "GET_SENSORS") {
    // Просто читаем все 6 пинов и отправляем
    String data = String(analogRead(A0));
    data += "," + String(analogRead(A1));
    data += "," + String(analogRead(A2));
    data += "," + String(analogRead(A3));
    data += "," + String(analogRead(A4));
    data += "," + String(analogRead(A5));
    sendResponse(String(SLAVE_ADDRESS) + ":SENSORS:" + data + "\n");
  } else if (command.startsWith("uno_motor")) {
    // Ожидаемый формат: "uno_motor_<pwmPin>_<dirPin>"
    // Ищем первое подчёркивание ПОСЛЕ "uno_" (индекс 4)
    int first_ = command.indexOf('_', 4);
    int second_ = -1;
    if (first_ != -1) {
      second_ = command.indexOf('_', first_ + 1);
    }
    if (first_ != -1 && second_ != -1) {
      int pwmPin = command.substring(first_ + 1, second_).toInt();
      int dirPin = command.substring(second_ + 1).toInt();
      controlMotorNoShield(pwmPin, dirPin, value);
    }
  } else if (command.startsWith("uno_led")) {
    // Ожидаемый формат: "uno_led<pin>"
    int pin = command.substring(7).toInt(); // "uno_led" = 7 символов
    controlLed(pin, value);
  } else {
    Serial.println(" -> WARNING: Unknown command '" + command + "'");
  }
}

// Функция для управления одним мотором
void controlMotorNoShield(int pwmPin, int dirPin, int speed) {
  speed = constrain(speed, -255, 255);

  if (speed >= 0) {
    digitalWrite(dirPin, HIGH); // Направление "вперед"
  } else {
    digitalWrite(dirPin, LOW); // Направление "назад"
  }

  // analogWrite работает со значениями 0-255, поэтому используем abs()
  analogWrite(pwmPin, abs(speed)); 
  
  Serial.println("-> Motor on pins " + String(pwmPin) + "/" + String(dirPin) + " set speed " + String(speed));
}

// Функция для управления одним светодиодом
void controlLed(int pin, int state) {
  // Простая проверка, чтобы не использовать системные пины 0 и 1
  if (pin < 2 || pin > 13) return; 

  pinMode(pin, OUTPUT);
  digitalWrite(pin, (state == 1) ? HIGH : LOW);

  Serial.println("-> LED on pin " + String(pin) + " set to " + String(state == 1 ? "ON" : "OFF"));
}
