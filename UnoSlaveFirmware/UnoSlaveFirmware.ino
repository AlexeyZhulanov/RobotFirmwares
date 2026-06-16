//#include <SoftwareSerial.h> // Библиотека для "виртуального" Serial порта
//#include <AltSoftSerial.h>
#include <Servo.h>

// --- Настройки RS-485 ---
//#define RS485_RX_PIN 8 // было 6
//#define RS485_TX_PIN 9 // было 7
#define SLAVE_ADDRESS 1 // Уникальный адрес этого устройства в сети

// Создаем "виртуальный" Serial порт для общения по RS-485
//SoftwareSerial rs485Serial(RS485_RX_PIN, RS485_TX_PIN);
//AltSoftSerial rs485Serial;

// Массив объектов Servo под каждый возможный цифровой пин Uno
Servo unoServos[14];
bool isServoAttached[14] = {false};

void setup() {
  // Запускаем основной Serial порт для отладки в Мониторе порта
  Serial.begin(115200);
  // Ждем открытия Монитора порта, чтобы не пропустить первые сообщения
  //while (!Serial) { ; }
  //Serial.println("[Slave] Arduino Uno ready. Address: " + String(SLAVE_ADDRESS));
  // Запускаем наш RS-485 порт
  //rs485Serial.begin(9600);
  //Serial.println("[Slave] Waiting for commands...");
}

void loop() {
  // Постоянно проверяем, не пришла ли команда от Master
  if (Serial.available()) {
    // Читаем входящую строку до символа новой строки '\n'
    String packet = Serial.readStringUntil('\n');
    packet.trim(); // Убираем лишние пробелы
    //Serial.print("[Slave RAW DATA] Received: <");
    //Serial.print(packet);
    //Serial.println(">");

    if (packet.length() > 0) {
      //Serial.print("Received packet: ");
      //Serial.println(packet);
      parseAndExecute(packet);
    }
  }
}

void sendResponse(const String& response) {
  Serial.print(response);
  Serial.flush();
  //Serial.print("[Slave] Sent Response: " + response);
}

// Главная функция - парсит пакет и выполняет команду
void parseAndExecute(const String& packet) {
  // Формат пакета: "ID:КОМАНДА:ЗНАЧЕНИЕ_ИЛИ_СТРОКА"
  // Пример: "1:uno_motor_9_8:-255"

  int id_separator = packet.indexOf(':');
  if (id_separator == -1) return;
  byte id = packet.substring(0, id_separator).toInt();
  if (id != SLAVE_ADDRESS) return;

  int cmd_separator = packet.indexOf(':', id_separator + 1);
  if (cmd_separator == -1) return;

  String command = packet.substring(id_separator + 1, cmd_separator);
  
  // Сначала сохраняем данные как строку, так как uno_tank передает текст "5,4,255,3,2,-255"
  String valueString = packet.substring(cmd_separator + 1); 
  
  // Для старых команд (uno_motor, uno_led) конвертируем строку в int
  int value = valueString.toInt();

  // Serial.println("-> Command for me! Cmd: '" + command + "', Val: '" + valueString + "'");

  if (command == "GET_SENSORS") {
    // Просто читаем все 6 пинов и отправляем
    String data = String(analogRead(A0));
    data += "," + String(analogRead(A1));
    data += "," + String(analogRead(A2));
    data += "," + String(analogRead(A3));
    data += "," + String(analogRead(A4));
    data += "," + String(analogRead(A5));
    sendResponse(String(SLAVE_ADDRESS) + ":SENSORS:" + data + "\n");
  } else if (command == "uno_tank") {
    // Ожидаемый формат valueString: "lPwm,lDir,lSpeed,rPwm,rDir,rSpeed"
    // Пример: "5,4,255,3,2,-255"
    int parts[6];
    int lastCommaIndex = -1;

    for (int i = 0; i < 6; i++) {
      int nextCommaIndex = valueString.indexOf(',', lastCommaIndex + 1);
      if (nextCommaIndex == -1) {
        nextCommaIndex = valueString.length(); // Для последнего элемента
      }
      // Вырезаем кусок строки от запятой до запятой и переводим в число
      parts[i] = valueString.substring(lastCommaIndex + 1, nextCommaIndex).toInt();
      lastCommaIndex = nextCommaIndex;
    }

    // Запускаем оба мотора синхронно
    // parts[0] = lPwm, parts[1] = lDir, parts[2] = lSpeed
    controlMotorNoShield(parts[0], parts[1], parts[2]); 
    
    // parts[3] = rPwm, parts[4] = rDir, parts[5] = rSpeed
    controlMotorNoShield(parts[3], parts[4], parts[5]);
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
  } else if (command.startsWith("uno_servo_")) {
    String params = command.substring(10); 
    int targetPin = params.toInt(); 
    
    // Ограничиваем угол (360 градусов для поддержки continuous сервоприводов)
    int angle = constrain(value, 0, 360);

    if (targetPin >= 0 && targetPin <= 13) {        
        // Инициализация на лету для конкретного пина
        if (!isServoAttached[targetPin]) {
            unoServos[targetPin].write(angle); // Задаем стартовый угол, чтобы избежать рывка
            unoServos[targetPin].attach(targetPin);
            isServoAttached[targetPin] = true;
            // Serial.println("-> Servo dynamically attached to pin " + String(targetPin));
        }
        // Поворачиваем сервопривод
        unoServos[targetPin].write(angle);
        // Serial.println("-> Servo pin " + String(targetPin) + " set to " + String(angle));
    }
  } else if (command.startsWith("uno_led")) {
    // Ожидаемый формат: "uno_led<pin>"
    int pin = command.substring(7).toInt(); // "uno_led" = 7 символов
    controlLed(pin, value);
  } else {
    //Serial.println(" -> WARNING: Unknown command '" + command + "'");
  }
}

// Функция для управления одним мотором
void controlMotorNoShield(int pwmPin, int dirPin, int speed) {
  speed = constrain(speed, -255, 255);

  if (speed == 0) {
    // На обоих пинах 0
    digitalWrite(dirPin, LOW);
    analogWrite(pwmPin, 0);
  }
  else if (speed > 0) {
    // Движение вперед
    // dirPin = 0V, pwmPin = от 1 до 255
    digitalWrite(dirPin, LOW);
    analogWrite(pwmPin, speed);
  } else {
    // Движение назад
    // dirPin = 5V. Чтобы появилась разница, pwmPin должен проседать к нулю
    digitalWrite(dirPin, HIGH);
    // Инвертируем ШИМ
    analogWrite(pwmPin, 255 - abs(speed));
  }
  
  //Serial.println("-> Motor on pins " + String(pwmPin) + "/" + String(dirPin) + " set speed " + String(speed));
}

// Функция для управления одним светодиодом
void controlLed(int pin, int state) {
  // Простая проверка, чтобы не использовать системные пины 0 и 1
  if (pin < 2 || pin > 13) return; 

  pinMode(pin, OUTPUT);
  digitalWrite(pin, (state == 1) ? HIGH : LOW);

  //Serial.println("-> LED on pin " + String(pin) + " set to " + String(state == 1 ? "ON" : "OFF"));
}