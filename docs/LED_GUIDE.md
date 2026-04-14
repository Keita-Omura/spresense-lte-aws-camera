# LED Status Guide

The Spresense main board has four built-in LEDs (LED0–LED3) that indicate
system status without requiring a serial connection.
This makes field operation with a mobile battery fully observable.

---

## LED Assignment

| LED | Color | Indicates |
|-----|-------|-----------|
| LED0 | Red | SD card initialized |
| LED1 | Red | Certificate loaded |
| LED2 | Red | LTE connected |
| LED3 | Red | System ready / operation status |

---

## Startup Sequence

```
Power ON
  ⚫ ⚫ ⚫ ⚫   All LEDs off

SD card init complete
  🔴 ⚫ ⚫ ⚫   LED0 ON

Certificate loaded
  🔴 🔴 ⚫ ⚫   LED1 ON

LTE connecting... (~30 seconds)
  🔴 🔴 ⚫ ⚫   Waiting for LTE

LTE connected
  🔴 🔴 🔴 ⚫   LED2 ON

System ready
  🔴 🔴 🔴 🔴   LED3 ON  ← Button is now active
```

---

## Operation Sequence (Button Press)

```
Standby
  🔴 🔴 🔴 🔴   Waiting for button

Button pressed
  🔴 🔴 🔴 ⚫   LED3 OFF — processing started

Capture complete
  🔴 🔴 🔴 🔴   LED3 blinks once briefly

Uploading ★ Key indicator ★
  ⚫ ⚫ ⚫ ⚫   ALL LEDs OFF — data is being transmitted
              (lasts approximately 5–15 seconds)

Upload complete
  🔴 🔴 🔴 🔴   All LEDs restored

Success
  🔴 🔴 🔴 ·   LED3 blinks slowly × 3

Ready for next capture
  🔴 🔴 🔴 🔴   Back to standby
```

> **Why all LEDs turn off during upload:**
> Turning off all four LEDs is an intentional design choice.
> It provides a clear, unambiguous visual signal that data transmission
> is in progress — no serial monitor required.

---

## Error Patterns

| Pattern | Meaning |
|---------|---------|
| LED0 blinking | SD card initialization failed |
| LED1 blinking | Certificate load failed |
| LED2 blinks × 3 (at startup) | LTE connection failed (warning — camera still works) |
| LED3 blinks × 5 fast | Capture or upload failed |

---

## Field Operation Reference

```
All LEDs ON  → Ready, press button anytime
All LEDs OFF → Uploading, do not power off
LED3 slow ×3 → Upload succeeded, email on the way
LED3 fast ×5 → Something went wrong, try again
```

For error diagnosis, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
