# Setup Guide

Complete setup instructions for the Spresense LTE Camera to AWS Pipeline.

---

## Prerequisites

### Hardware

| Component | Details |
|-----------|---------|
| Spresense Main Board | Sony CXD5602 (ARM Cortex-M4F) |
| Spresense Camera Board | 5MP CMOS sensor |
| Spresense LTE Extension Board | LTE Cat-M1 modem |
| SD Card | FAT32, 32 GB or less |
| Tact Switch | Connected between D01 pin and GND |
| SIM Card | Data SIM with APN credentials |
| Power Supply | USB or mobile battery (5V / 1A or more) |

### Software

- [Arduino IDE](https://www.arduino.cc/en/software) with Spresense board support
- AWS account

---

## AWS Setup

### 1. S3 Bucket

1. Open the S3 console and click **Create bucket**
2. Configure:
   ```
   Bucket name : your-bucket-name
   Region      : ap-northeast-1 (Tokyo)
   ```
3. Leave all other settings at default and click **Create bucket**

---

### 2. Lambda Function #1 — Image Uploader

**Create function:**

1. Open the Lambda console and click **Create function**
2. Select **Author from scratch**
3. Configure:
   ```
   Function name : spresense-image-uploader
   Runtime       : Python 3.12
   Architecture  : x86_64
   ```
4. Click **Create function**

**Deploy code:**

1. Replace the default code with the contents of `lambda/lambda_uploader.py`
2. Click **Deploy**

**Set environment variable:**

1. Go to **Configuration** → **Environment variables** → **Edit**
2. Add:
   ```
   Key   : BUCKET_NAME
   Value : your-bucket-name
   ```
3. Click **Save**

**Add IAM permission:**

1. Go to **Configuration** → **Permissions**
2. Click the execution role name to open IAM
3. Click **Add permissions** → **Attach policies**
4. Search for and attach: `AmazonS3FullAccess`

---

### 3. Lambda Function #2 — Email Sender

**Create function:**

1. Open the Lambda console and click **Create function**
2. Select **Author from scratch**
3. Configure:
   ```
   Function name : spresense-image-email-sender
   Runtime       : Python 3.12
   Architecture  : x86_64
   ```
4. Click **Create function**

**Deploy code:**

1. Replace the default code with the contents of `lambda/lambda_email_sender.py`
2. Update the email addresses in the code:
   ```python
   SENDER_EMAIL    = 'your-email@example.com'
   RECIPIENT_EMAIL = 'your-email@example.com'
   ```
3. Click **Deploy**

**Add IAM permissions:**

1. Go to **Configuration** → **Permissions**
2. Click the execution role name to open IAM
3. Click **Add permissions** → **Attach policies**
4. Search for and attach both:
   - `AmazonS3ReadOnlyAccess`
   - `AmazonSESFullAccess`

**Add S3 trigger:**

1. Click **+ Add trigger**
2. Select **S3**
3. Configure:
   ```
   Bucket      : your-bucket-name
   Event type  : PUT
   Suffix      : .jpg
   ```
4. Click **Add**

---

### 4. API Gateway

**Create API:**

1. Open the API Gateway console and click **Create API**
2. Select **HTTP API** → **Build**
3. Configure:
   ```
   API name : SpresenseImageAPI
   ```
4. Click **Next** → **Next** → **Create**

**Add route and integration:**

1. Go to **Develop** → **Routes** → **Create**
2. Configure:
   ```
   Method : ANY
   Path   : /image-upload
   ```
3. Click **Create**
4. Select the route and click **Attach integration** → **Create and attach**
5. Configure:
   ```
   Integration type : Lambda function
   Lambda function  : spresense-image-uploader
   ```
6. Click **Create**

**Note your endpoint URL:**

Go to **Deploy** → **Stages** → `$default` and copy the **Invoke URL**.
Your full endpoint will be:
```
https://your-api-id.execute-api.ap-northeast-1.amazonaws.com/image-upload
```

---

### 5. Amazon SES

**Verify email address:**

1. Open the SES console (region: **ap-northeast-1**)
2. Go to **Verified identities** → **Create identity**
3. Select **Email address** and enter your email
4. Click **Create identity**
5. Check your inbox and click the verification link

> **Note:** In SES Sandbox mode, both sender and recipient must be verified.
> This is sufficient for personal use. To send to unverified addresses,
> you must request production access.

---

## Spresense Setup

### Hardware Connections

```
Spresense Main Board
  ↓ (camera connector)
Spresense Camera Board
  ↓ (extension connector)
Spresense LTE Extension Board

Button wiring:
  D01 pin ──── [Tact Switch] ──── GND
```

### Arduino IDE Settings

1. Install Spresense board support in Arduino IDE
2. Select **Tools** → **Board** → **Spresense**
3. Configure:
   ```
   Memory : 1536 KB  ← Required (minimum: 1024 KB)
   ```

> **Why 1536 KB?**
> The camera, LTE modem, and TLS stack all require memory simultaneously.
> With 896 KB, the LTE modem fails with `lte_set_report_restart error: -12`.

### SD Card Structure

Format SD card as FAT32 and create the following structure:

```
SD Card
├── certs/
│   └── AmazonRootCA1.pem   ← Download from link below
└── images/                  ← Created automatically on first capture
```

**Download Amazon Root CA 1 certificate:**

```
URL: https://www.amazontrust.com/repository/AmazonRootCA1.pem
Expected size: 1188 bytes
```

Verify the file starts with `-----BEGIN CERTIFICATE-----`.

### APN Configuration

Open `firmware/spresense_lte_camera.ino` and update the APN settings:

```cpp
#define APP_LTE_APN       "your.apn.here"
#define APP_LTE_USER_NAME "your_username"
#define APP_LTE_PASSWORD  "your_password"
```

Also update the API Gateway endpoint:

```cpp
#define API_HOST "your-api-id.execute-api.ap-northeast-1.amazonaws.com"
#define API_PATH "/image-upload"
```

### Upload to Spresense

1. Connect Spresense to PC via USB
2. Open `firmware/spresense_lte_camera.ino` in Arduino IDE
3. Verify Memory is set to **1536 KB**
4. Click **Upload**

---

## First Run

1. Open Serial Monitor at **115200 baud**
2. Power on Spresense
3. Watch the initialization sequence:
   ```
   [1/5] Initializing SD card...     → LED0 ON
   [2/5] Initializing camera...
   [3/5] Loading certificate...      → LED1 ON
   [4/5] Connecting to LTE network... → LED2 ON (takes ~30 sec)
   [5/5] System ready!               → LED3 ON
   ```
4. Press the button on D01
5. All LEDs turn off during upload
6. LEDs restore and LED3 blinks 3 times on success
7. Check your email — the image should arrive within 20 seconds

For LED status details, see [LED_GUIDE.md](LED_GUIDE.md).  
For troubleshooting, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
