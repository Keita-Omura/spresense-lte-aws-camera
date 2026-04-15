/*
 * Spresense LTE Camera to AWS Pipeline
 *
 * Auto-detects serial connection:
 * - When connected to PC: shows debug messages via Serial Monitor
 * - When running on battery: skips serial output for stable operation
 *
 * Hardware Requirements:
 * - Spresense Main Board
 * - Spresense Camera Board
 * - Spresense LTE Extension Board
 * - SD Card (must contain /certs/AmazonRootCA1.pem)
 * - Tact Switch (connected between D01 pin and GND)
 * - SIM Card with data plan
 *
 * Arduino IDE Settings:
 * - Memory: 1536 KB (required for camera + LTE + TLS to work simultaneously)
 *
 * License: MIT
 */

#include <LTE.h>
#include <LTETLSClient.h>
#include <SDHCI.h>
#include <File.h>
#include <Camera.h>
#include <stdio.h>

// ============================================
// Configuration Parameters
// ============================================

// LTE Connection Settings
// Replace with your carrier's APN settings
#define APP_LTE_APN       "your.apn.here"   // e.g., "iijmio.jp"
#define APP_LTE_USER_NAME "your_username"   // e.g., "mio@iij"
#define APP_LTE_PASSWORD  "your_password"   // e.g., "iij"

// File Paths on SD Card
#define ROOTCA_FILE     "/certs/AmazonRootCA1.pem"
#define TEMP_IMAGE_FILE "/images/temp_upload.jpg"

// AWS API Gateway Settings
// Replace with your API Gateway endpoint
#define API_HOST "your-api-id.execute-api.ap-northeast-1.amazonaws.com"
#define API_PORT 443
#define API_PATH "/image-upload"

// Button Configuration
#define BUTTON_PIN    1   // D01 pin
#define DEBOUNCE_DELAY 50 // Debounce delay in milliseconds

// Connection Retry Settings
#define LTE_CONNECT_RETRY  3
#define HTTP_CONNECT_RETRY 2

// Buffer Sizes
#define READ_BUFFER_SIZE     1024
#define RESPONSE_BUFFER_SIZE 512

// Serial Communication
#define BAUDRATE       115200
#define SERIAL_TIMEOUT 3000   // Serial connection timeout in milliseconds

// ============================================
// Global Objects
// ============================================

LTE lteAccess;
LTETLSClient client;
SDClass theSD;

// Button state management
int lastButtonState = HIGH;
int buttonState     = HIGH;
unsigned long lastDebounceTime = 0;

// Statistics
int pictureCount      = 0;
int uploadSuccessCount = 0;
int uploadFailCount    = 0;

// Connection state
bool lteConnected = false;

// Serial connection state (auto-detected at startup)
bool serialEnabled = false;

// ============================================
// Debug Output Macros
// ============================================
// Automatically skips Serial.print() when not connected to PC.
// This prevents blocking when running on mobile battery.

#define DEBUG_PRINT(x)    if (serialEnabled) Serial.print(x)
#define DEBUG_PRINTLN(x)  if (serialEnabled) Serial.println(x)
#define DEBUG_PRINT2(x,y) if (serialEnabled) Serial.print(x, y)

// ============================================
// Function Prototypes
// ============================================

void printError(enum CamErr err);
bool initializeSD();
bool initializeCamera();
bool loadCertificate();
bool connectLTE();
bool takePictureAndSave();
bool uploadImage(const char* filename);
int  readResponse();
void printStats();

// ============================================
// Setup
// ============================================

void setup() {
  CamErr err;

  // Initialize LEDs (all off)
  pinMode(LED0, OUTPUT);
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  digitalWrite(LED0, LOW);
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);

  // Initialize serial communication
  Serial.begin(BAUDRATE);

  // Wait for serial connection (max 3 seconds)
  // Allows operation without PC connection
  unsigned long serialTimeout = millis();
  while (!Serial && (millis() - serialTimeout < SERIAL_TIMEOUT)) {
    ;
  }

  // Auto-detect serial connection
  serialEnabled = (bool)Serial;

  DEBUG_PRINTLN("========================================");
  DEBUG_PRINTLN("Spresense LTE Camera to AWS Pipeline");
  DEBUG_PRINTLN("========================================");
  DEBUG_PRINTLN();

  if (serialEnabled) {
    DEBUG_PRINTLN("[Mode] Serial Debug ON");
  }
  DEBUG_PRINTLN();

  // Initialize button pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  DEBUG_PRINTLN("[Init] Button initialized on D01");
  DEBUG_PRINTLN();

  // [1/5] Initialize SD card
  DEBUG_PRINTLN("[1/5] Initializing SD card...");
  if (!initializeSD()) {
    DEBUG_PRINTLN("ERROR: SD card initialization failed!");
    DEBUG_PRINTLN("Program stopped.");
    while (1) {
      digitalWrite(LED0, HIGH);
      delay(500);
      digitalWrite(LED0, LOW);
      delay(500);
    }
  }
  DEBUG_PRINTLN("OK: SD card initialized");
  digitalWrite(LED0, HIGH); // LED0 ON: SD card ready
  DEBUG_PRINTLN();

  // [2/5] Initialize camera
  DEBUG_PRINTLN("[2/5] Initializing camera...");
  if (!initializeCamera()) {
    DEBUG_PRINTLN("ERROR: Camera initialization failed!");
    DEBUG_PRINTLN("Program stopped.");
    while (1) {
      digitalWrite(LED1, HIGH);
      delay(500);
      digitalWrite(LED1, LOW);
      delay(500);
    }
  }
  DEBUG_PRINTLN("OK: Camera initialized");
  DEBUG_PRINTLN();

  // [3/5] Load root CA certificate
  DEBUG_PRINTLN("[3/5] Loading certificate...");
  if (!loadCertificate()) {
    DEBUG_PRINTLN("ERROR: Certificate loading failed!");
    DEBUG_PRINTLN("Program stopped.");
    while (1) {
      digitalWrite(LED1, HIGH);
      delay(500);
      digitalWrite(LED1, LOW);
      delay(500);
    }
  }
  DEBUG_PRINTLN("OK: Certificate loaded");
  digitalWrite(LED1, HIGH); // LED1 ON: certificate ready
  DEBUG_PRINTLN();

  // [4/5] Connect to LTE network
  DEBUG_PRINTLN("[4/5] Connecting to LTE network...");
  if (!connectLTE()) {
    DEBUG_PRINTLN("WARNING: LTE connection failed!");
    DEBUG_PRINTLN("Camera will work, but upload will fail.");
    lteConnected = false;
    // Blink LED2 three times to indicate warning
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED2, HIGH);
      delay(200);
      digitalWrite(LED2, LOW);
      delay(200);
    }
  } else {
    DEBUG_PRINTLN("OK: LTE connected");
    digitalWrite(LED2, HIGH); // LED2 ON: LTE connected
    lteConnected = true;
  }
  DEBUG_PRINTLN();

  // [5/5] System ready
  DEBUG_PRINTLN("[5/5] System ready!");
  digitalWrite(LED3, HIGH); // LED3 ON: system ready
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("========================================");
  DEBUG_PRINTLN("Press button (D01) to take a picture");
  DEBUG_PRINTLN("========================================");
  DEBUG_PRINTLN();
}

// ============================================
// Main Loop
// ============================================

void loop() {
  int reading = digitalRead(BUTTON_PIN);

  // Reset debounce timer if button state changed
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  // Process button state after debounce period
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;

      // Button pressed (LOW)
      if (buttonState == LOW) {
        DEBUG_PRINTLN("========================================");
        DEBUG_PRINTLN("Button pressed!");
        DEBUG_PRINTLN("========================================");
        DEBUG_PRINTLN();

        // Turn off LED3 to indicate processing
        digitalWrite(LED3, LOW);

        // Step 1: Take picture
        DEBUG_PRINTLN("[Step 1/2] Taking picture...");
        if (takePictureAndSave()) {
          DEBUG_PRINTLN("OK: Picture saved to SD card");
          DEBUG_PRINTLN();

          // Blink LED3 once to confirm capture
          digitalWrite(LED3, HIGH);
          delay(100);
          digitalWrite(LED3, LOW);
          delay(100);
          digitalWrite(LED3, HIGH);

          // Retry LTE if disconnected
          if (!lteConnected) {
            DEBUG_PRINTLN("WARNING: LTE not connected. Retrying...");
            if (connectLTE()) {
              lteConnected = true;
              digitalWrite(LED2, HIGH);
              DEBUG_PRINTLN("OK: LTE reconnected");
            } else {
              DEBUG_PRINTLN("ERROR: LTE connection failed. Upload skipped.");
              uploadFailCount++;
              printStats();
              DEBUG_PRINTLN("Ready for next picture.");
              DEBUG_PRINTLN();
              digitalWrite(LED3, HIGH);
              lastButtonState = reading;
              return;
            }
            DEBUG_PRINTLN();
          }

          // Step 2: Upload image
          DEBUG_PRINTLN("[Step 2/2] Uploading to AWS...");

          // Turn off all LEDs during upload to indicate data transmission
          digitalWrite(LED0, LOW);
          digitalWrite(LED1, LOW);
          digitalWrite(LED2, LOW);
          digitalWrite(LED3, LOW);

          if (uploadImage(TEMP_IMAGE_FILE)) {
            // Restore LEDs after successful upload
            digitalWrite(LED0, HIGH);
            digitalWrite(LED1, HIGH);
            digitalWrite(LED2, HIGH);
            digitalWrite(LED3, HIGH);

            DEBUG_PRINTLN("OK: Upload successful!");
            uploadSuccessCount++;

            // Blink LED3 slowly three times to indicate success
            for (int i = 0; i < 3; i++) {
              digitalWrite(LED3, LOW);
              delay(200);
              digitalWrite(LED3, HIGH);
              delay(200);
            }
          } else {
            // Restore LEDs after failed upload
            digitalWrite(LED0, HIGH);
            digitalWrite(LED1, HIGH);
            digitalWrite(LED2, HIGH);
            digitalWrite(LED3, HIGH);

            DEBUG_PRINTLN("ERROR: Upload failed");
            uploadFailCount++;

            // Blink LED3 quickly five times to indicate failure
            for (int i = 0; i < 5; i++) {
              digitalWrite(LED3, LOW);
              delay(100);
              digitalWrite(LED3, HIGH);
              delay(100);
            }
          }
        } else {
          DEBUG_PRINTLN("ERROR: Failed to take picture");
          uploadFailCount++;

          // Blink LED3 quickly five times to indicate failure
          for (int i = 0; i < 5; i++) {
            digitalWrite(LED3, LOW);
            delay(100);
            digitalWrite(LED3, HIGH);
            delay(100);
          }
        }

        DEBUG_PRINTLN();
        printStats();
        DEBUG_PRINTLN("========================================");
        DEBUG_PRINTLN("Ready for next picture.");
        DEBUG_PRINTLN("========================================");
        DEBUG_PRINTLN();

        // Restore LED3 for standby
        digitalWrite(LED3, HIGH);
      }
    }
  }

  lastButtonState = reading;
}

// ============================================
// Function Implementations
// ============================================

/**
 * Print camera error message
 */
void printError(enum CamErr err) {
  DEBUG_PRINT("  Camera Error: ");
  switch (err) {
    case CAM_ERR_NO_DEVICE:          DEBUG_PRINTLN("No Device");                  break;
    case CAM_ERR_ILLEGAL_DEVERR:     DEBUG_PRINTLN("Illegal device error");       break;
    case CAM_ERR_ALREADY_INITIALIZED:DEBUG_PRINTLN("Already initialized");        break;
    case CAM_ERR_NOT_INITIALIZED:    DEBUG_PRINTLN("Not initialized");            break;
    case CAM_ERR_NOT_STILL_INITIALIZED: DEBUG_PRINTLN("Still picture not initialized"); break;
    case CAM_ERR_CANT_CREATE_THREAD: DEBUG_PRINTLN("Failed to create thread");    break;
    case CAM_ERR_INVALID_PARAM:      DEBUG_PRINTLN("Invalid parameter");          break;
    case CAM_ERR_NO_MEMORY:          DEBUG_PRINTLN("No memory");                  break;
    case CAM_ERR_USR_INUSED:         DEBUG_PRINTLN("Buffer already in use");      break;
    case CAM_ERR_NOT_PERMITTED:      DEBUG_PRINTLN("Operation not permitted");    break;
    default:                         DEBUG_PRINTLN("Unknown error");              break;
  }
}

/**
 * Initialize SD card with retry logic
 */
bool initializeSD() {
  for (int i = 0; i < 5; i++) {
    if (theSD.begin()) return true;
    DEBUG_PRINT("  Retry ");
    DEBUG_PRINT(i + 1);
    DEBUG_PRINTLN("/5...");
    delay(1000);
  }
  return false;
}

/**
 * Initialize camera with QUADVGA JPEG format
 */
bool initializeCamera() {
  CamErr err;

  err = theCamera.begin();
  if (err != CAM_ERR_SUCCESS) { printError(err); return false; }

  DEBUG_PRINTLN("  Setting auto white balance...");
  err = theCamera.setAutoWhiteBalanceMode(CAM_WHITE_BALANCE_DAYLIGHT);
  if (err != CAM_ERR_SUCCESS) { printError(err); return false; }

  DEBUG_PRINTLN("  Setting image format (QUADVGA, JPEG)...");
  err = theCamera.setStillPictureImageFormat(
    CAM_IMGSIZE_QUADVGA_H,
    CAM_IMGSIZE_QUADVGA_V,
    CAM_IMAGE_PIX_FMT_JPG
  );
  if (err != CAM_ERR_SUCCESS) { printError(err); return false; }

  return true;
}

/**
 * Load Amazon Root CA certificate from SD card for TLS verification
 */
bool loadCertificate() {
  File certFile = theSD.open(ROOTCA_FILE, FILE_READ);
  if (!certFile) {
    DEBUG_PRINT("  ERROR: Cannot open certificate file: ");
    DEBUG_PRINTLN(ROOTCA_FILE);
    return false;
  }

  size_t certSize = certFile.size();
  DEBUG_PRINT("  Certificate file size: ");
  DEBUG_PRINT(certSize);
  DEBUG_PRINTLN(" bytes");

  if (certSize == 0 || certSize > 4096) {
    DEBUG_PRINTLN("  ERROR: Invalid certificate file size");
    certFile.close();
    return false;
  }

  client.setCACert(certFile, certSize);
  certFile.close();

  DEBUG_PRINTLN("  Certificate set successfully");
  return true;
}

/**
 * Connect to LTE network with retry logic
 */
bool connectLTE() {
  for (int i = 0; i < LTE_CONNECT_RETRY; i++) {
    DEBUG_PRINT("  Attempt ");
    DEBUG_PRINT(i + 1);
    DEBUG_PRINT("/");
    DEBUG_PRINTLN(LTE_CONNECT_RETRY);

    if (lteAccess.begin() != LTE_SEARCHING) {
      DEBUG_PRINTLN("  ERROR: Failed to start LTE modem");
      delay(2000);
      continue;
    }

    LTEModemStatus status = lteAccess.attach(
      APP_LTE_APN,
      APP_LTE_USER_NAME,
      APP_LTE_PASSWORD
    );

    if (status == LTE_READY) {
      DEBUG_PRINTLN("  Attached to LTE network");

      unsigned long startTime = millis();
      while (lteAccess.getStatus() != LTE_READY) {
        if (millis() - startTime > 30000) {
          DEBUG_PRINTLN("  ERROR: Timeout waiting for LTE_READY");
          break;
        }
        delay(1000);
        DEBUG_PRINT(".");
      }

      if (lteAccess.getStatus() == LTE_READY) {
        DEBUG_PRINTLN();
        if (serialEnabled) {
          DEBUG_PRINT("  IP Address: ");
          IPAddress ip = lteAccess.getIPAddress();
          Serial.println(ip);
        }
        return true;
      }
    }

    DEBUG_PRINTLN("  ERROR: Failed to attach to network");
    delay(2000);
  }

  return false;
}

/**
 * Capture image and save to SD card
 * Saves two copies: permanent sequential file and temp file for upload
 */
bool takePictureAndSave() {
  CamImage img = theCamera.takePicture();

  if (!img.isAvailable()) {
    DEBUG_PRINTLN("  ERROR: Failed to capture image");
    DEBUG_PRINTLN("  Hint: Check Memory setting in Arduino IDE (must be 1536 KB)");
    return false;
  }

  DEBUG_PRINT("  Image captured: ");
  DEBUG_PRINT(img.getImgSize());
  DEBUG_PRINTLN(" bytes");

  // Save permanent copy with sequential filename
  char permanentFilename[32];
  sprintf(permanentFilename, "/images/PICT%04d.JPG", pictureCount);

  File permFile = theSD.open(permanentFilename, FILE_WRITE);
  if (!permFile) {
    DEBUG_PRINTLN("  ERROR: Cannot create permanent file on SD card");
    return false;
  }

  size_t writtenPerm = permFile.write(img.getImgBuff(), img.getImgSize());
  permFile.close();

  if (writtenPerm != img.getImgSize()) {
    DEBUG_PRINTLN("  ERROR: Permanent file write incomplete");
    return false;
  }

  DEBUG_PRINT("  Saved as: ");
  DEBUG_PRINTLN(permanentFilename);

  // Save temp copy for upload (overwrite previous)
  if (theSD.exists(TEMP_IMAGE_FILE)) {
    theSD.remove(TEMP_IMAGE_FILE);
  }

  File tempFile = theSD.open(TEMP_IMAGE_FILE, FILE_WRITE);
  if (!tempFile) {
    DEBUG_PRINTLN("  WARNING: Cannot create temp file");
    pictureCount++;
    return true;
  }

  size_t writtenTemp = tempFile.write(img.getImgBuff(), img.getImgSize());
  tempFile.close();

  if (writtenTemp == img.getImgSize()) {
    DEBUG_PRINT("  Also saved as temp: ");
    DEBUG_PRINTLN(TEMP_IMAGE_FILE);
  }

  pictureCount++;
  return true;
}

/**
 * Upload image file to AWS via API Gateway (HTTPS POST)
 * Returns true only when HTTP 200 is received
 */
bool uploadImage(const char* filename) {
  File imageFile = theSD.open(filename, FILE_READ);
  if (!imageFile) {
    DEBUG_PRINT("  ERROR: Cannot open image file: ");
    DEBUG_PRINTLN(filename);
    return false;
  }

  size_t imageSize = imageFile.size();
  DEBUG_PRINT("  Image file size: ");
  DEBUG_PRINT(imageSize);
  DEBUG_PRINTLN(" bytes");

  if (imageSize == 0) {
    DEBUG_PRINTLN("  ERROR: Image file is empty");
    imageFile.close();
    return false;
  }

  // Connect to API Gateway
  DEBUG_PRINT("  Connecting to ");
  DEBUG_PRINT(API_HOST);
  DEBUG_PRINTLN("...");

  bool connected = false;
  for (int i = 0; i < HTTP_CONNECT_RETRY && !connected; i++) {
    if (client.connect(API_HOST, API_PORT)) {
      connected = true;
      DEBUG_PRINTLN("  Connected to API Gateway");
    } else {
      DEBUG_PRINT("  Connection failed. Retry ");
      DEBUG_PRINT(i + 1);
      DEBUG_PRINT("/");
      DEBUG_PRINTLN(HTTP_CONNECT_RETRY);
      delay(2000);
    }
  }

  if (!connected) {
    DEBUG_PRINTLN("  ERROR: Failed to connect to API Gateway");
    imageFile.close();
    return false;
  }

  // Send HTTP POST request headers
  DEBUG_PRINTLN("  Sending HTTP POST request...");
  client.print("POST ");
  client.print(API_PATH);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(API_HOST);
  client.println("Content-Type: image/jpeg");
  client.print("Content-Length: ");
  client.println(imageSize);
  client.println("Connection: close");
  client.println();

  // Send image data in chunks
  DEBUG_PRINTLN("  Sending image data...");
  uint8_t buffer[READ_BUFFER_SIZE];
  size_t totalSent  = 0;
  size_t lastProgress = 0;

  while (imageFile.available()) {
    size_t bytesRead    = imageFile.read(buffer, READ_BUFFER_SIZE);
    size_t bytesWritten = client.write(buffer, bytesRead);

    if (bytesWritten != bytesRead) {
      DEBUG_PRINTLN("  ERROR: Failed to write data");
      imageFile.close();
      client.stop();
      return false;
    }

    totalSent += bytesWritten;

    // Show progress every 20%
    size_t progress = (totalSent * 100) / imageSize;
    if (progress >= lastProgress + 20) {
      DEBUG_PRINT("  Progress: ");
      DEBUG_PRINT(progress);
      DEBUG_PRINTLN("%");
      lastProgress = progress;
    }
  }

  imageFile.close();

  DEBUG_PRINT("  Total sent: ");
  DEBUG_PRINT(totalSent);
  DEBUG_PRINTLN(" bytes");

  // Wait for response
  DEBUG_PRINTLN("  Waiting for response...");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 10000) {
      DEBUG_PRINTLN("  ERROR: Response timeout");
      client.stop();
      return false;
    }
    delay(100);
  }

  // Check HTTP status code
  int statusCode = readResponse();
  client.stop();

  if (statusCode == 200) {
    return true;
  } else {
    DEBUG_PRINT("  ERROR: Server returned HTTP ");
    DEBUG_PRINTLN(statusCode);
    return false;
  }
}

/**
 * Read HTTP response and return status code
 * Parses first line (e.g. "HTTP/1.1 200 OK") to extract status code
 */
int readResponse() {
  int statusCode = 0;
  String firstLine = "";
  bool firstLineRead = false;

  DEBUG_PRINTLN("  ---- Response ----");

  while (client.available()) {
    char c = client.read();

    if (!firstLineRead) {
      if (c == '\n') {
        // Extract status code from "HTTP/1.1 200 OK"
        int firstSpace  = firstLine.indexOf(' ');
        int secondSpace = firstLine.indexOf(' ', firstSpace + 1);
        if (firstSpace >= 0 && secondSpace > firstSpace) {
          statusCode = firstLine.substring(firstSpace + 1, secondSpace).toInt();
        }
        firstLineRead = true;
        if (serialEnabled) Serial.println(firstLine);
      } else if (c != '\r') {
        firstLine += c;
      }
    } else {
      if (serialEnabled) Serial.print(c);
    }
  }

  DEBUG_PRINTLN("  ---- End Response ----");
  return statusCode;
}

/**
 * Print upload statistics to Serial Monitor
 */
void printStats() {
  if (!serialEnabled) return;

  DEBUG_PRINTLN();
  DEBUG_PRINTLN("--- Statistics ---");
  DEBUG_PRINT("Pictures taken:  ");
  DEBUG_PRINTLN(pictureCount);
  DEBUG_PRINT("Upload success:  ");
  DEBUG_PRINTLN(uploadSuccessCount);
  DEBUG_PRINT("Upload failed:   ");
  DEBUG_PRINTLN(uploadFailCount);
  DEBUG_PRINT("Success rate:    ");
  if (pictureCount > 0) {
    DEBUG_PRINT((uploadSuccessCount * 100) / pictureCount);
    DEBUG_PRINTLN("%");
  } else {
    DEBUG_PRINTLN("N/A");
  }
  DEBUG_PRINTLN("------------------");
}
