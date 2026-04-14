# Troubleshooting Guide

Common issues and solutions for the Spresense LTE Camera to AWS Pipeline.

---

## HTTP 500 Internal Server Error

**Symptom:**
```
HTTP/1.1 500 Internal Server Error
{"message":"Internal Server Error"}
```

**Most likely cause:** Lambda resource-based policy has an outdated route name.

When the API Gateway route is renamed (e.g. from `/old-path` to `/image-upload`),
the ARN in Lambda's resource-based policy must be updated to match.

**How to fix:**

1. Open **Lambda** → `spresense-image-uploader`
2. Go to **Configuration** → **Permissions**
3. Scroll to **Resource-based policy statements**
4. Check the `Condition` ARN — the suffix must match your current route name:
   ```json
   {
     "ArnLike": {
       "AWS:SourceArn": "arn:aws:execute-api:ap-northeast-1:your-account-id:your-api-id/*/*/image-upload"
     }
   }
   ```
5. If it shows an old route name, click **Edit** and update the ARN suffix

**Checklist when changing the API Gateway route name:**

```
□ Update route name in API Gateway
□ Update ARN suffix in Lambda resource-based policy
□ Update API_PATH in spresense_lte_camera.ino
□ Re-upload firmware to Spresense
□ Verify operation
```

---

## Device Does Not Work on Mobile Battery

**Symptom:** Works when connected to PC via USB, but does nothing on battery.

**Cause:** `Serial.print()` blocks execution when no serial connection is present.
The internal buffer fills up and the program freezes.

**Solution (already implemented in v2+):**

The firmware auto-detects serial connection at startup and skips all
`Serial.print()` calls when running on battery:

```cpp
// Wait up to 3 seconds for serial connection
bool serialEnabled = false;
unsigned long t = millis();
while (!Serial && (millis() - t < 3000)) { ; }
serialEnabled = (bool)Serial;

// Conditional output macro
#define DEBUG_PRINTLN(x) if (serialEnabled) Serial.println(x)
```

If you experience this issue, make sure you are using firmware **v2 or later**.

---

## LTE Modem Fails to Start

**Symptom:**
```
ERROR: lte_set_report_restart result error : -12
ERROR: Failed to start LTE modem
```

**Cause:** Insufficient memory. The LTE modem cannot initialize with less than 1024 KB.

**Solution:** In Arduino IDE, set Memory to **1536 KB**:

```
Tools → Memory → 1536 KB
```

**Memory setting reference:**

| Setting | Result |
|---------|--------|
| 896 KB  | LTE modem initialization fails (`error: -12`) |
| 1024 KB | Minimum working configuration (not recommended) |
| 1536 KB | ✅ Recommended — stable with camera + LTE + TLS |

---

## SD Card Not Detected

**Symptom:**
```
ERROR: SD card initialization failed!
LED0 blinking
```

**Checklist:**
- SD card is formatted as **FAT32** (not exFAT or NTFS)
- SD card capacity is **32 GB or less**
- SD card is fully inserted into the LTE extension board slot
- `/certs/AmazonRootCA1.pem` exists on the card (1188 bytes)
- `/images/` folder exists on the card

---

## Certificate Error / TLS Handshake Failure

**Symptom:**
```
ERROR: Certificate loading failed!
```
or TLS connection errors in serial output.

**Checklist:**
- File path on SD card is exactly `/certs/AmazonRootCA1.pem`
- File size is **1188 bytes** (verify in Windows Explorer or similar)
- File content starts with `-----BEGIN CERTIFICATE-----`
- Download source: `https://www.amazontrust.com/repository/AmazonRootCA1.pem`

---

## Email Not Received

**Checklist:**

1. **SES verification** — Both sender and recipient email addresses must be
   verified in SES console (region: ap-northeast-1)

2. **SES region** — Confirm the SES console is set to **ap-northeast-1 (Tokyo)**,
   not another region

3. **Lambda #2 trigger** — Confirm the S3 trigger is configured:
   - Bucket: your bucket name
   - Suffix: `.jpg`

4. **Lambda #2 IAM** — Confirm the execution role has:
   - `AmazonS3ReadOnlyAccess`
   - `AmazonSESFullAccess`

5. **Check spam folder** — SES emails may be filtered as spam initially

6. **CloudWatch logs** — Check Lambda #2 logs for error messages:
   ```
   Lambda → spresense-image-email-sender → Monitor → View CloudWatch logs
   ```

---

## Upload Appears Successful But Image Not in S3

**Symptom:** Serial output shows `OK: Upload successful!` but no file in S3.

This was a bug in firmware v3 and earlier. The firmware incorrectly returned
success whenever any HTTP response was received, even a 500 error.

**Solution:** Update to firmware **v4 or later**, which checks the HTTP status code:

```cpp
int statusCode = readResponse();
if (statusCode == 200) {
    return true;
} else {
    return false; // Correctly reports failure on 500 errors
}
```

---

## LTE Connection Takes Too Long

**Note:** Initial LTE connection typically takes **30–60 seconds**.
This is normal behavior for LTE Cat-M1 modem attachment.

The firmware retries up to 3 times with a 2-second delay between attempts.
If connection consistently fails, check:
- SIM card PIN lock is disabled (disable via smartphone settings)
- Data plan is active on the SIM
- APN settings are correct for your carrier
