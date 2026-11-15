#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ModelResults {
struct RepResult {
  unsigned long index;
  bool wrong;
  float pWrong;
  float durationS;
  char exercise[4];
};
static RepResult latestRepResult = {};
static char repVerdictLine[64] = "";
static char verdictLine[40] = "";
static unsigned long receivedRepCount = 0;
static bool awaitingVerdict = false;
static unsigned long verdictWaitStartMs = 0;
static char replyInputLine[64];
static size_t replyInputLength = 0;
static bool discardReplyLine = false;

// Add OLED/save/forwarding code here. Do not delay() in these callbacks.
static void onRep() {
  Serial.print(F("# "));
  Serial.println(repVerdictLine);
  // latestRepResult: index, wrong, pWrong, exercise, durationS.
  // Called for every rep; the next rep replaces these stored values.
}
static void onSet() {
  Serial.print(F("# "));
  Serial.println(verdictLine);
  // verdictLine holds the complete final V result, including unknown.
}
static void note(const __FlashStringHelper *message) {
  Serial.print(F("# "));
  Serial.println(message);
}
static bool busy() { return awaitingVerdict; }
static bool timedOut() {
  return awaitingVerdict && millis() - verdictWaitStartMs >= 30000UL;
}
// Call once, immediately BEFORE sending E done / E aborted / E overflow.
static bool beginWait() {
  if (awaitingVerdict) return false;
  receivedRepCount = 0;
  latestRepResult = RepResult{};
  repVerdictLine[0] = verdictLine[0] = '\0';
  replyInputLength = 0;
  discardReplyLine = false;
  verdictWaitStartMs = millis();
  awaitingVerdict = true;
  return true;
}

static void processVerdict(char *line) {
  char tag[4] = {0}, verdict[16] = {0}, probability[16] = {0},
       exercise[16] = {0}, trailing = '\0';
  char *end = NULL;
  bool shapeOk = sscanf(line, "%3s %15s %15s %15s %c", tag, verdict,
                        probability, exercise, &trailing) == 4 &&
    strcmp(tag, "V") == 0 &&
    (strcmp(verdict, "correct") == 0 || strcmp(verdict, "wrong") == 0 ||
     strcmp(verdict, "unknown") == 0);
  bool probabilityOk = false;
  bool exerciseOk = shapeOk && (strcmp(exercise, "EAH") == 0 ||
                    strcmp(exercise, "EFE") == 0 || strcmp(exercise, "SQZ") == 0);
  if (shapeOk && strcmp(verdict, "unknown") == 0) {
    probabilityOk = strcmp(probability, "nan") == 0 && strcmp(exercise, "-") == 0;
  } else if (shapeOk) {
    double p = strtod(probability, &end);
    probabilityOk = end != probability && *end == '\0' && isfinite(p) &&
                    p >= 0.0 && p <= 1.0 && exerciseOk;
  }
  if (shapeOk && probabilityOk && awaitingVerdict) {
    strncpy(verdictLine, line, sizeof(verdictLine) - 1);
    verdictLine[sizeof(verdictLine) - 1] = '\0';
    awaitingVerdict = false;
    onSet();
  } else {
    note(F("ignored invalid or unsolicited verdict"));
  }
}

static void processRepVerdict(char *line) {
  char tag[3] = {}, number[16] = {}, verdict[16] = {}, probability[16] = {},
       exercise[8] = {}, duration[16] = {}, trailing = '\0';
  bool shapeOk = sscanf(line, "%2s %15s %15s %15s %7s %15s %c", tag, number,
                        verdict, probability, exercise, duration, &trailing) == 6;
  if (!awaitingVerdict || !shapeOk || strcmp(tag, "R") != 0 ||
      (strcmp(verdict, "correct") != 0 && strcmp(verdict, "wrong") != 0) ||
      (strcmp(exercise, "EAH") != 0 && strcmp(exercise, "EFE") != 0 &&
       strcmp(exercise, "SQZ") != 0) || number[0] == '\0' ||
      strspn(number, "0123456789") != strlen(number)) {
    note(F("ignored invalid or unsolicited rep result"));
    return;
  }
  unsigned long index = strtoul(number, NULL, 10);
  char *pEnd = NULL, *dEnd = NULL;
  double probabilityValue = strtod(probability, &pEnd);
  double durationValue = strtod(duration, &dEnd);
  if (index != receivedRepCount + 1 || pEnd == probability || *pEnd != '\0' ||
      dEnd == duration || *dEnd != '\0' || !isfinite(probabilityValue) ||
      probabilityValue < 0 || probabilityValue > 1 ||
      !isfinite(durationValue) || durationValue <= 0 || durationValue > 600) {
    note(F("ignored invalid or out-of-order rep result"));
    return;
  }
  latestRepResult.index = index;
  latestRepResult.wrong = strcmp(verdict, "wrong") == 0;
  latestRepResult.pWrong = (float)probabilityValue;
  latestRepResult.durationS = (float)durationValue;
  strcpy(latestRepResult.exercise, exercise);
  strcpy(repVerdictLine, line); // Caller supplies a bounded, complete line.
  receivedRepCount = index;
  verdictWaitStartMs = millis(); // Valid progress restarts the inactivity timer.
  onRep();
  // OLED / forwarding hook: latestRepResult holds this rep's estimated result.
  // Stay awaitingVerdict: only the final V line completes the set.
}

// Use this entry point if YOUR firmware already reads Serial1.
// Pass one complete, null-terminated line; do not also call poll().
static void acceptResultsLine(const char *line) {
  if (!line) return;
  char complete[64];
  size_t n = 0;
  while (n < sizeof(complete) && line[n] != '\0') ++n;
  if (n == 0 || n >= sizeof(complete)) return;
  memcpy(complete, line, n);
  while (n > 0 && (complete[n-1] == '\r' || complete[n-1] == '\n')) --n;
  complete[n] = '\0';
  if (strncmp(complete, "R ", 2) == 0) processRepVerdict(complete);
  else if (strncmp(complete, "V ", 2) == 0) processVerdict(complete);
}

static void poll() {
  // The tally script's reply arrives on the same UART that carries samples.
  // Bound work per loop and discard overlong lines through their newline.
  for (uint8_t processed = 0; processed < 16 && Serial1.available() > 0; ++processed) {
    char c = (char)Serial1.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (!discardReplyLine) {
        replyInputLine[replyInputLength] = '\0';
        acceptResultsLine(replyInputLine);
      }
      replyInputLength = 0;
      discardReplyLine = false;
    } else if (discardReplyLine) {
      continue;
    } else if (replyInputLength + 1 < sizeof(replyInputLine)) {
      replyInputLine[replyInputLength++] = c;
    } else {
      replyInputLength = 0;
      discardReplyLine = true;
      note(F("reply line too long; discarded"));
    }
  }
}

} // namespace ModelResults

/* MERGE POINTS: add these statements to YOUR existing handlers.
   Do not paste a second setup() or loop().

   Existing setup(), once:
     Serial1.begin(115200);
     Serial.begin(115200);  // Optional USB diagnostics used by onRep/onSet.

   Existing Start event:
     If already recording or ModelResults::busy() is true, ignore Start.
     Otherwise send the header and begin your existing 10 ms sample loop.

   Existing Done event, AFTER stopping sample transmission:
     if (ModelResults::beginWait()) Serial1.println("E done");
     For Cancel/failure, use the same guard and E aborted / E overflow.

   Existing loop(), called frequently without delay:
     ModelResults::poll();
     If ModelResults::timedOut(), show Unable to score; keep Start locked.

   Alternative when your existing UART reader already assembles lines:
     ModelResults::acceptResultsLine(yourCompleteLine);
     Use this INSTEAD of poll().

   Edit onRep() and onSet() above for your OLED/save/forwarding output.
   Handle each onRep() immediately; the next rep overwrites latestRepResult.
   Timeout recovery: restart Python AND reset Arduino before the next set.
*/