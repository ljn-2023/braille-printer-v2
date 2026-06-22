#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

/* ========= WiFi ========= */
const char* ssid = "XH";
const char* password = "13650311";//这里建议用手机热点，同时可以使用串口打开调试，查看ESP32的IP地址，方便后续访问。

WebServer server(80);

/* ========= 舵机 ========= */
const int servoPin1 = 22;
const int servoPin2 = 23;

const int ch1 = 0;
const int ch2 = 1;

#define SERVO_FREQ 50
#define SERVO_RES 16

const int CH1_START_ANGLE = 80; //Y轴舵机初始化位置
const float DEG_PER_DOT = 20.0; //X轴的初始化位置
int dotAngle[6];

/* ========= X轴：霍尔闭环 ========= */
#define X_AIN1 25
#define X_AIN2 26
#define X_PWM  27

#define X_HALL_A 34
#define X_HALL_B 35

#define PWM_CH_X 2

#define X_COUNT_PER_MM 375.0

#define X_MIN_MM 0.0
#define X_MAX_MM 25.0 //x最大值
#define CELL_STEP_MM 5.0 //X每次移动的最大距离，超过这个距离会自动换行

#define PWM_MIN 90
#define PWM_MAX 230

#define STOP_ERR_MM 0.2
#define SLOW_ERR_MM 1.5

volatile long xCount = 0;
long xTargetCount = 0;
bool xMoving = false;
float currentXmm = 0;

/* ========= Y轴：开环棍纸 ========= */
#define Y_AIN1 18
#define Y_AIN2 19
#define Y_PWM  21

#define PWM_CH_Y 3

#define Y_PWM_SPEED 255

#define MS_PER_MM 120.0
#define LINE_STEP_MM 150.0
#define EJECT_MM 200.0

float currentYmm = 0;
// 启动时 Y 轴持续走纸，直到网页上点击完成走纸
volatile bool yKeepRunning = true;

/* ========= 电机PWM ========= */
#define MOTOR_PWM_FREQ 20000
#define MOTOR_PWM_RES  8

/* ========= 舵机控制 ========= */
void servoWrite(int channel, int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;

  int us = map(angle, 0, 180, 500, 2400);
  int duty = (int)((us / 20000.0) * 65535);

  ledcWrite(channel, duty);
}

// ch2 初始化为 50，打点动作：50 -> 0 -> 50
void punchOnce() {
  servoWrite(ch2, 50);
  delay(200);

  servoWrite(ch2, 7);
  delay(500);

  servoWrite(ch2, 50);
  delay(200);
}

void punchDot(int dotNumber) {
  if (dotNumber < 1 || dotNumber > 6) return;

  servoWrite(ch1, dotAngle[dotNumber - 1]);
  delay(500);

  punchOnce();
}

void printOneCell(String cell) {
  cell.trim();

  for (int i = 0; i < cell.length(); i++) {
    char c = cell.charAt(i);

    if (c >= '1' && c <= '6') {
      punchDot(c - '0');
    }
  }
}

/* ========= X轴霍尔 ========= */
void IRAM_ATTR xHallISR() {
  int b = digitalRead(X_HALL_B);

  // 如果X方向反了，把++和--对调
  if (b == HIGH) {
    xCount--;
  } else {
    xCount++;
  }
}

long xMmToCount(float mm) {
  return (long)(mm * X_COUNT_PER_MM);
}

float xCountToMm(long count) {
  return count / X_COUNT_PER_MM;
}

/* ========= X轴电机 ========= */
void xMotorStop() {
  ledcWrite(PWM_CH_X, 0);
  digitalWrite(X_AIN1, LOW);
  digitalWrite(X_AIN2, LOW);
}

void xMotorRun(int pwm) {
  if (pwm > 0) {
    digitalWrite(X_AIN1, HIGH);
    digitalWrite(X_AIN2, LOW);
    ledcWrite(PWM_CH_X, pwm);
  } else if (pwm < 0) {
    digitalWrite(X_AIN1, LOW);
    digitalWrite(X_AIN2, HIGH);
    ledcWrite(PWM_CH_X, -pwm);
  } else {
    xMotorStop();
  }
}

void moveXToMM(float targetMM) {
  if (targetMM < X_MIN_MM) targetMM = X_MIN_MM;
  if (targetMM > X_MAX_MM) targetMM = X_MAX_MM;

  xTargetCount = xMmToCount(targetMM);
  xMoving = true;

  Serial.print("X目标: ");
  Serial.print(targetMM);
  Serial.println(" mm");
}

int calcPWM(float absErrMM) {
  int pwm;

  if (absErrMM > SLOW_ERR_MM) {
    pwm = PWM_MAX;
  } else {
    pwm = map((long)(absErrMM * 100),
              (long)(STOP_ERR_MM * 100),
              (long)(SLOW_ERR_MM * 100),
              PWM_MIN,
              PWM_MAX);
  }

  if (pwm < PWM_MIN) pwm = PWM_MIN;
  if (pwm > PWM_MAX) pwm = PWM_MAX;

  return pwm;
}

void updateXMotor() {
  if (!xMoving) return;

  long errCount = xTargetCount - xCount;
  float errMM = errCount / X_COUNT_PER_MM;
  float absErrMM = fabs(errMM);

  if (absErrMM <= STOP_ERR_MM) {
    xMotorStop();
    xMoving = false;
    currentXmm = xCountToMm(xCount);

    Serial.print("X到位: ");
    Serial.print(currentXmm);
    Serial.println(" mm");
    return;
  }

  int pwm = calcPWM(absErrMM);

  if (errCount > 0) {
    xMotorRun(pwm);
  } else {
    xMotorRun(-pwm);
  }
}

void waitXArrive() {
  while (xMoving) {
    updateXMotor();
    delay(5);
  }
}

void goHomeX() {
  moveXToMM(0);
  waitXArrive();
  currentXmm = 0;
}

/* ========= Y轴开环 ========= */
void yMotorStop() {
  ledcWrite(PWM_CH_Y, 0);
  digitalWrite(Y_AIN1, LOW);
  digitalWrite(Y_AIN2, LOW);
}

void yMotorRun(int pwm) {
  if (pwm > 0) {
    digitalWrite(Y_AIN1, HIGH);
    digitalWrite(Y_AIN2, LOW);
    ledcWrite(PWM_CH_Y, pwm);
  } else if (pwm < 0) {
    digitalWrite(Y_AIN1, LOW);
    digitalWrite(Y_AIN2, HIGH);
    ledcWrite(PWM_CH_Y, -pwm);
  } else {
    yMotorStop();
  }
}

void moveYOpenLoopMM(float mm) {
  if (mm == 0) return;

  int dir = (mm > 0) ? 1 : -1;
  float absMM = fabs(mm);

  unsigned long runTime = absMM * MS_PER_MM;

  Serial.print("Y走纸: ");
  Serial.print(mm);
  Serial.print(" mm, time=");
  Serial.print(runTime);
  Serial.println(" ms");

  yMotorRun(dir * Y_PWM_SPEED);
  delay(runTime);
  yMotorStop();

  currentYmm += mm;
}

/* ========= 打印逻辑 ========= */
void nextLine() {
  Serial.println("换行：X回0，Y走纸");

  goHomeX();

  moveYOpenLoopMM(LINE_STEP_MM);

  currentXmm = 0;
}

void ejectPaper() {

  Serial.println("开始退纸");

  moveYOpenLoopMM(-EJECT_MM);

  Serial.println("退纸完成");
}

void printBrailleLine(String code) {
  code.trim();

  // 每次启动打印，X必须先回初始位置
  goHomeX();
  currentXmm = 0;

  int start = 0;

  while (start < code.length()) {
    int spaceIndex = code.indexOf(' ', start);
    String cell;

    if (spaceIndex == -1) {
      cell = code.substring(start);
      start = code.length();
    } else {
      cell = code.substring(start, spaceIndex);
      start = spaceIndex + 1;
    }

    cell.trim();
    if (cell.length() == 0) continue;

    Serial.print("打印盲文方: ");
    Serial.print(cell);
    Serial.print(" X=");
    Serial.print(currentXmm);
    Serial.print(" Y=");
    Serial.println(currentYmm);

    printOneCell(cell);

    currentXmm += CELL_STEP_MM;

    if (currentXmm >= X_MAX_MM) {
      nextLine();
    } else {
      moveXToMM(currentXmm);
      waitXArrive();
    }
  }

  goHomeX();
  Serial.println("打印完成，X已回0");

  ejectPaper();

  Serial.println("打印任务结束");

}

/* ========= Web ========= */
void handleRoot() {
  server.send(200, "text/html; charset=utf-8",
              "<html><head><meta charset=\"utf-8\"></head><body>"
              "<h3>Braille Printer Ready</h3>"
              "<p>启动时 Y 轴持续走纸。完成走纸后请点击：<a href=\"/finish\">完成走纸</a></p>"
              "<p>打印示例：GET /print?code=13%20145%203</p>"
              "</body></html>");
}

void handlePrint() {
  if (!server.hasArg("code")) {
    server.send(400, "text/plain; charset=utf-8", "缺少 code 参数");
    return;
  }

  String code = server.arg("code");

  server.send(200, "text/plain; charset=utf-8", "收到，开始打印: " + code);

  printBrailleLine(code);
}

/* ========= setup ========= */
void handleFinishY();
void setup() {
  Serial.begin(115200);
  delay(500);

  /* 舵机初始化 */
  ledcSetup(ch1, SERVO_FREQ, SERVO_RES);
  ledcAttachPin(servoPin1, ch1);

  ledcSetup(ch2, SERVO_FREQ, SERVO_RES);
  ledcAttachPin(servoPin2, ch2);

  for (int i = 0; i < 6; i++) {
    dotAngle[i] = CH1_START_ANGLE + i * DEG_PER_DOT;
    if (dotAngle[i] > 180) dotAngle[i] = 180;
  }

  servoWrite(ch1, CH1_START_ANGLE);
  delay(1000);

  // 你要求初始化必须是50
  servoWrite(ch2, 50);
  delay(1000);

  /* X轴初始化 */
  pinMode(X_AIN1, OUTPUT);
  pinMode(X_AIN2, OUTPUT);
  pinMode(X_HALL_A, INPUT);
  pinMode(X_HALL_B, INPUT);

  ledcSetup(PWM_CH_X, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(X_PWM, PWM_CH_X);

  attachInterrupt(digitalPinToInterrupt(X_HALL_A), xHallISR, RISING);

  xMotorStop();

  /* Y轴初始化 */
  pinMode(Y_AIN1, OUTPUT);
  pinMode(Y_AIN2, OUTPUT);

  ledcSetup(PWM_CH_Y, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(Y_PWM, PWM_CH_Y);

  yMotorStop();
  // 启动时让 Y 轴持续走动，直到网页 /finish 被点击
  yKeepRunning = true;
  yMotorRun(Y_PWM_SPEED);

  /* WiFi */
  WiFi.begin(ssid, password);
  Serial.println("正在连接WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("ESP32 IP: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/print", handlePrint);
  server.on("/finish", handleFinishY);
  server.begin();

  Serial.println("盲文打印机启动完成");
}

void loop() {
  server.handleClient();
  updateXMotor();
}

void handleFinishY() {
  if (!yKeepRunning) {
    server.send(200, "text/plain; charset=utf-8", "Y轴已停止，已完成走纸");
    return;
  }

  yKeepRunning = false;
  yMotorStop();
  Serial.println("收到完成走纸，Y轴停止");
  server.send(200, "text/plain; charset=utf-8", "收到，已停止Y轴走纸，可以打印");
}