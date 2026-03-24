#include <Arduino.h>
#include <WiFiNINA.h>
#include <ESP_Mail_Client.h>
#include <HX711_ADC.h>
#include <FlashStorage.h>
#include "arduino_secrets.h" // Separate file to hold credentials and other important info

// Wifi Credentials
char WIFI_SSID[] =  SECRET_WIFI_SSID;
char WIFI_PASSWORD[] = SECRET_WIFI_PASSWORD;

// Pins:
const int HX711_dout = 4; 
const int HX711_sck = 5; 

// SMTP
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT esp_mail_smtp_port_587 

// Email credentials
char AUTHOR_EMAIL[] = SECRET_AUTHOR_EMAIL;
char AUTHOR_PASSWORD[] = SECRET_AUTHOR_PASSWORD;

/* Recipient email address */
char RECIPIENT_EMAIL[] = SECRET_RECIPIENT_EMAIL;

// HX711 Constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);

// Emulated EEPROM address:
FlashStorage(calibrationValueStorage, float); // Flash storage for calibration value
float calibrationValue = 1.0;                // Default calibration value

unsigned long t = 0;

/* Declare the global used SMTPSession object for SMTP transport */
SMTPSession smtp;

/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status);

// Change to false after message sent
bool running = true;

float previousWeight = 99999.9; // Variable to store the last weight reading

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for the serial port to connect. Needed for native USB
  }

  // Messaging Setup
  Serial.println();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Load Cell Setup:
  LoadCell.begin();
  unsigned long stabilizingtime = 5000; // Stabilizing time after power-up
  boolean _tare = true; // Perform tare at startup
  LoadCell.start(stabilizingtime, _tare);

  if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  } else {
    // Load calibration value from emulated EEPROM
    float storedCalValue = calibrationValueStorage.read();
    if (!isnan(storedCalValue) && storedCalValue != 0) {
      calibrationValue = storedCalValue;
    }
    LoadCell.setCalFactor(calibrationValue);
    Serial.println("Startup is complete");
  }

  while (!LoadCell.update());
  calibrate(); // Start calibration procedure
}

void loop() {
  static boolean newDataReady = 0;
  const int serialPrintInterval = 0; // Increase to slow down serial output

  if (running) {
    // Check for new data/start next conversion:
    if (LoadCell.update()) newDataReady = true;

    if (newDataReady) {
      if (millis() > t + serialPrintInterval) {
        float currentWeight = LoadCell.getData();
        Serial.print("Load_cell output val: ");
        Serial.println(currentWeight);

        // Check for a sudden increase in weight
        if (currentWeight > previousWeight + 20.0) { // Adjust threshold as needed
          Serial.println("Sudden increase in weight detected. Sending email...");
          sendEmail();
          running = false; // Stop further checks after sending the email
        }

        previousWeight = currentWeight; // Update the previous weight
        newDataReady = 0;
        t = millis();
      }
    }

    // Receive commands from the Serial Monitor
    if (Serial.available() > 0) {
      char inByte = Serial.read();
      if (inByte == 't') LoadCell.tareNoDelay(); // Tare
      else if (inByte == 'r') calibrate();       // Calibrate
      else if (inByte == 'c') changeSavedCalFactor(); // Edit calibration value manually
    }

    // Check if the last tare operation is complete
    if (LoadCell.getTareStatus() == true) {
      Serial.println("Tare complete");
    }
  }
}

void sendEmail() {

  // Set the network reconnection option 
  MailClient.networkReconnect(true);

  smtp.debug(1);

  // Set the callback function to get the sending results 
  smtp.callback(smtpCallback);
  // Declare the Session_Config for user defined session credentials 
  Session_Config config;

  // Set the session config 
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;

  config.login.user_domain = F("127.0.0.1");
  config.time.ntp_server = F("pool.ntp.org,time.nist.gov");
  config.time.gmt_offset = 3;
  config.time.day_light_offset = 0;

  // Declare the message class 
  SMTP_Message message;

  // Set the message headers 
  message.sender.name = F("Package Sensor");
  message.sender.email = AUTHOR_EMAIL;

  String subject = "Package Received";
  message.subject = subject;

  message.addRecipient(F("Someone "), RECIPIENT_EMAIL);

  String textMsg = "A Package has been placed on the doormat";

  message.text.content = textMsg;
  message.text.transfer_encoding = "base64"; 
  message.text.charSet = F("utf-8");
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;
  message.addHeader(F("Message-ID: <abcde.fghij@gmail.com>"));

  // Connect to the server 
  if (!smtp.connect(&config)) {
    MailClient.printf("Connection error, Status Code: %d, Error Code: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return;
  }

  if (!MailClient.sendMail(&smtp, &message)) {
    MailClient.printf("Error, Status Code: %d, Error Code: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
  } else {
    Serial.println("Email sent successfully.");
  }
}

void calibrate() {
  Serial.println("***");
  Serial.println("Start calibration:");
  Serial.println("Place the load cell on a level, stable surface.");
  Serial.println("Remove any load applied to the load cell.");
  Serial.println("Send 't' from Serial Monitor to set the tare offset.");

  boolean _resume = false;
  while (!_resume) {
    LoadCell.update();
    if (Serial.available() > 0) {
      char inByte = Serial.read();
      if (inByte == 't') LoadCell.tareNoDelay();
    }
    if (LoadCell.getTareStatus() == true) {
      Serial.println("Tare complete");
      _resume = true;
    }
  }

  Serial.println("Now, place your known mass on the load cell.");
  Serial.println("Send the weight of this mass (e.g., 100.0) from Serial Monitor.");

  float known_mass = 0;
  _resume = false;
  while (!_resume) {
    LoadCell.update();
    if (Serial.available() > 0) {
      known_mass = Serial.parseFloat();
      if (known_mass != 0) {
        Serial.print("Known mass is: ");
        Serial.println(known_mass);
        _resume = true;
      }
    }
  }

  LoadCell.refreshDataSet(); // Refresh the dataset to ensure accurate measurement
  float newCalibrationValue = LoadCell.getNewCalibration(known_mass);

  Serial.print("New calibration value has been set to: ");
  Serial.print(newCalibrationValue);
  Serial.println(". Use this as the calibration value (calFactor) in your project sketch.");
  Serial.print("Save this value to flash storage? (y/n)");

  _resume = false;
  while (!_resume) {
    if (Serial.available() > 0) {
      char inByte = Serial.read();
      if (inByte == 'y') {
        calibrationValueStorage.write(newCalibrationValue);
        Serial.println("Calibration value saved.");
        _resume = true;
      } else if (inByte == 'n') {
        Serial.println("Calibration value not saved.");
        _resume = true;
      }
    }
  }

  Serial.println("End calibration");
}

void changeSavedCalFactor() {
  float oldCalibrationValue = LoadCell.getCalFactor();
  Serial.println("***");
  Serial.print("Current calibration value is: ");
  Serial.println(oldCalibrationValue);
  Serial.println("Send the new calibration value from Serial Monitor (e.g., 696.0)");

  boolean _resume = false;
  float newCalibrationValue;
  while (!_resume) {
    if (Serial.available() > 0) {
      newCalibrationValue = Serial.parseFloat();
      if (newCalibrationValue != 0) {
        Serial.print("New calibration value is: ");
        Serial.println(newCalibrationValue);
        LoadCell.setCalFactor(newCalibrationValue);
        calibrationValueStorage.write(newCalibrationValue);
        Serial.println("Calibration value saved.");
        _resume = true;
      }
    }
  }

  Serial.println("End change calibration value");
}

/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status)
{
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success())
  {

    Serial.println("----------------");
    MailClient.printf("Message sent success: %d\n", status.completedCount());
    MailClient.printf("Message sent failed: %d\n", status.failedCount());
    Serial.println("----------------\n");

    for (size_t i = 0; i < smtp.sendingResult.size(); i++)
    {
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);

      MailClient.printf("Message No: %d\n", i + 1);
      MailClient.printf("Status: %s\n", result.completed ? "success" : "failed");
      MailClient.printf("Date/Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%B %d, %Y %H:%M:%S").c_str());
      MailClient.printf("Recipient: %s\n", result.recipients.c_str());
      MailClient.printf("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------\n");

    // You need to clear sending result as the memory usage will grow up.
    smtp.sendingResult.clear();
  }
}
