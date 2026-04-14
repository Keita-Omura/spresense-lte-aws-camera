# Development History

A record of the problems encountered, root causes, and solutions
across each version of the Spresense LTE Camera to AWS Pipeline.

---

## Version Overview

| Version | Date | Key Change |
|---------|------|------------|
| v1 | 2025-10-20 | Initial implementation |
| v2 | 2025-10-21 | Fixed Serial blocking issue for battery operation |
| v3 | 2025-10-21 | Added LED visual feedback during upload |
| v4 | 2026-04-15 | Fixed HTTP status code check bug |

---

## v1 — Initial Implementation (2025-10-20)

**What was built:**

End-to-end pipeline from button press to email delivery:

```
Spresense → LTE → API Gateway → Lambda #1 → S3 → Lambda #2 → SES → Email
```

**Worked on first attempt:** PC-connected USB operation with Serial Monitor.

---

## v2 — Serial Blocking Fix (2025-10-21)

**Problem:**

Device worked when connected to PC but did nothing on mobile battery.
Initialization appeared to complete (all LEDs lit), but pressing the button
produced no response and no files appeared in S3.

**Root cause:**

`Serial.print()` blocks execution when no serial connection is present.
The internal UART buffer fills up, causing the program to freeze silently
mid-operation — typically during the image capture or upload phase.

**Investigation process:**

1. Removed `while (!Serial)` — LEDs still all lit, so setup() was completing
2. Stripped all `Serial.print()` calls from the entire sketch
3. Battery test → worked immediately
4. Confirmed: `Serial.print()` was the sole cause

**Solution:**

Auto-detect serial connection at startup and skip all output when not connected:

```cpp
// Wait up to 3 seconds for serial connection
bool serialEnabled = false;
unsigned long t = millis();
while (!Serial && (millis() - t < 3000)) { ; }
serialEnabled = (bool)Serial;

// Conditional output — zero cost when serial is absent
#define DEBUG_PRINT(x)   if (serialEnabled) Serial.print(x)
#define DEBUG_PRINTLN(x) if (serialEnabled) Serial.println(x)
```

**Result:** Single codebase works for both PC-connected debugging and
standalone battery operation.

---

## v3 — LED Visual Feedback (2025-10-21)

**Problem:**

With battery operation confirmed, there was no way to know whether the
device was uploading, had succeeded, or had failed — without a serial monitor.

**Solution:**

Used the four built-in LEDs to communicate system state:

- Sequential LED lighting during initialization (LED0 → LED1 → LED2 → LED3)
- **All LEDs OFF during upload** — unambiguous "transmitting" signal
- Slow 3-blink on success, fast 5-blink on failure

**Key design decision — all LEDs off during upload:**

Rather than blinking one LED, turning off all four creates a stark contrast
that is immediately visible even in bright outdoor conditions.
No LED activity = data is being sent right now.

```cpp
// Turn off all LEDs at upload start
digitalWrite(LED0, LOW);
digitalWrite(LED1, LOW);
digitalWrite(LED2, LOW);
digitalWrite(LED3, LOW);

// ... send image data ...

// Restore all LEDs on completion (success or failure)
digitalWrite(LED0, HIGH);
digitalWrite(LED1, HIGH);
digitalWrite(LED2, HIGH);
digitalWrite(LED3, HIGH);
```

---

## v4 — HTTP Status Code Fix (2026-04-15)

**Problem:**

Serial Monitor showed `OK: Upload successful!` even when the server returned
`HTTP/1.1 500 Internal Server Error`. No image appeared in S3 or email inbox,
but the firmware reported success.

**Root cause:**

`uploadImage()` called `printResponse()` which only printed the response
and discarded it. The function returned `true` unconditionally whenever
any HTTP response was received — regardless of status code.

```cpp
// v3 — always returned true after receiving any response
printResponse();
client.stop();
return true;  // ← bug: ignores HTTP 500
```

**Solution:**

Replaced `printResponse()` with `readResponse()` that parses and returns
the HTTP status code. `uploadImage()` now returns `true` only on HTTP 200.

```cpp
// v4 — checks status code before returning
int statusCode = readResponse();
client.stop();
if (statusCode == 200) {
    return true;
} else {
    DEBUG_PRINT("  ERROR: Server returned HTTP ");
    DEBUG_PRINTLN(statusCode);
    return false;
}
```

**Status code parsing:**

```cpp
// Parses "HTTP/1.1 200 OK" → extracts 200
int firstSpace  = firstLine.indexOf(' ');
int secondSpace = firstLine.indexOf(' ', firstSpace + 1);
statusCode = firstLine.substring(firstSpace + 1, secondSpace).toInt();
```

**Also fixed in v4:**

Replaced one remaining raw `Serial.print()` call in `printStats()` with
the `DEBUG_PRINT()` macro for consistency (no functional impact, since
`printStats()` already guards with `if (!serialEnabled) return`).

---

## AWS Infrastructure Changes (2026-04-15)

During the GitHub publication preparation, Lambda function names and
the API Gateway route were renamed for clarity:

| Resource | Old Name | New Name |
|----------|----------|----------|
| Lambda #1 | `spresense_presigned_URL` | `spresense-image-uploader` |
| Lambda #2 | `s3_image_email_sender` | `spresense-image-email-sender` |
| API Gateway route | `/spresense_presigned_URL` | `/image-upload` |

**Why the original names were misleading:**

The project originally attempted a Presigned URL upload approach
(Spresense → get presigned URL from Lambda → PUT directly to S3).
This was abandoned due to Spresense memory constraints and the added
complexity of two HTTP round-trips.
The final implementation uses a single direct POST to API Gateway,
but the function name `spresense_presigned_URL` remained as a historical artifact
until this cleanup.

**Important lesson — route rename checklist:**

Renaming the API Gateway route caused `HTTP 500` errors because the ARN
in Lambda's resource-based policy still referenced the old route name.
Both must be updated together:

```
□ API Gateway route name
□ Lambda resource-based policy ARN (suffix must match route name)
□ API_PATH in firmware
□ Re-upload firmware to Spresense
```
