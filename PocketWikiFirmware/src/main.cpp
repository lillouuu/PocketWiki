#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// ── Colors ───────────────────────────────────────────────────────
#define DARK_GRAY    0x2104
#define MEDIUM_GRAY  0x4208
#define LIGHT_GRAY   0xC618
#define OFF_WHITE    0xEF7D

// ── Sleep ────────────────────────────────────────────────────────
unsigned long lastTouchTime = 0;
#define SLEEP_TIMEOUT 120000  // 2 min

// ── Screen state ─────────────────────────────────────────────────
enum Screen { HOME, SEARCH, RESULTS, ARTICLE };
Screen currentScreen = HOME;

// ── RTC Memory ───────────────────────────────────────────────────
// used to restore exactly where the user was before the device slept
RTC_DATA_ATTR int  rtcScreen    = 0;
RTC_DATA_ATTR int  rtcPage      = 0;
RTC_DATA_ATTR char rtcTitle[64] = "";
RTC_DATA_ATTR char rtcQuery[32] = "";

// ── TFT pins ─────────────────────────────────────────────────────
#define TFT_CS   2
#define TFT_DC   4
#define TFT_RST  22
#define TFT_LED  15

// ── Touch pins ───────────────────────────────────────────────────
#define T_IRQ    27
#define T_CS     21

// ── SD pin ───────────────────────────────────────────────────────
#define SD_CS    5

// ── Keyboard ─────────────────────────────────────────────────────
char keys[3][10] = {
    {'Q','W','E','R','T','Y','U','I','O','P'},
    {'A','S','D','F','G','H','J','K','L',' '},
    {'Z','X','C','V','B','N','M','#',' ',' '}
};
int rowSizes[]   = {10, 9, 8};
int rowOffsets[] = {20, 34, 20};

#define KEY_W   26
#define KEY_H   24
#define KEY_GAP  2
#define KB_Y   118

// ── Search ───────────────────────────────────────────────────────
String searchInput = "";

// ── Article ──────────────────────────────────────────────────────
#define CHARS_PER_PAGE 1000
int    currentPage  = 0;
int    totalPages   = 0;
String currentTitle = "";

// ── Results ──────────────────────────────────────────────────────
#define MAX_RESULTS 5
String results[MAX_RESULTS];
int resultCount  = 0;
int resultOffset = 0;

// ── Objects ──────────────────────────────────────────────────────
Adafruit_ILI9341    tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen touch(T_CS, T_IRQ);


// ═══════════════════════════════════════════════════════════════
//  SD helpers
// ═══════════════════════════════════════════════════════════════
bool initSD() {
    if (!SD.begin(SD_CS)) {
        Serial.println("SD card FAILED");
        return false;
    }
    Serial.println("SD card OK");
    return true;
}

// read only one page worth of chars starting at 'start'
String readFilePage(String path, int start, int length) {
    File file = SD.open(path);
    if (!file) {
        Serial.println("failed to open: " + path);
        return "";
    }
    file.seek(start);
    String content = "";
    int count = 0;
    while (file.available() && count < length) {
        content += (char)file.read();
        count++;
    }
    file.close();
    return content;
}

// build nested folder path from title
String buildPath(String title, bool includeFile = true) {
    String path  = "/";
    int    depth = 0;
    for (int i = 0; i < (int)title.length() && depth < 10; i++) {
        char c = tolower(title[i]);
        if (isAlpha(c)) {
            path += c;
            path += "/";
            depth++;
        } else if (isDigit(c)) {
            path += "#/";
            depth++;
        }
    }
    if (includeFile) path += title + ".txt";
    return path;
}


// ═══════════════════════════════════════════════════════════════
//  Touch helper
// ═══════════════════════════════════════════════════════════════
TS_Point getTouch() {
    TS_Point p = touch.getPoint();
    p.x = map(p.x, 200, 3900, 0, 320);
    p.y = map(p.y, 200, 3900, 0, 240);
    return p;
}


// ═══════════════════════════════════════════════════════════════
//  No results / results
// ═══════════════════════════════════════════════════════════════
void showNoResults() {
    tft.fillRect(0, 88, 320, 127, ILI9341_BLACK);
    tft.setTextColor(MEDIUM_GRAY);
    tft.setTextSize(1);
    tft.setCursor(20, 140);
    tft.print("No articles found");
}

void displayResults() {
    // clear results area only
    tft.fillRect(0, 88, 320, 127, ILI9341_BLACK);

    // results label
    tft.setTextColor(LIGHT_GRAY);
    tft.setTextSize(1);
    tft.setCursor(20, 91);
    tft.print("RESULTS:");
    tft.fillRect(20, 101, 280, 1, LIGHT_GRAY);

    if (resultCount == 0) {
        showNoResults();
        return;
    }

    // draw each result
    for (int i = 0; i < resultCount; i++) {
        int y = 106 + i * 22;
        tft.drawRoundRect(10, y, 255, 18, 3, MEDIUM_GRAY);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(1);
        tft.setCursor(18, y + 5);
        tft.print(results[i]);
    }

    // load more button
    if (resultCount == MAX_RESULTS) {
        tft.drawRoundRect(270, 106, 45, 5 * 22, 3, LIGHT_GRAY);
        tft.setTextColor(LIGHT_GRAY);
        tft.setTextSize(1);
        tft.setCursor(273, 106 + MAX_RESULTS * 11);
        tft.print("more");
    }
}


// ═══════════════════════════════════════════════════════════════
//  Search articles
// ═══════════════════════════════════════════════════════════════
void searchArticles(String query, int offset = 0) {
    if (query.length() == 0) return;
    String folderPath = buildPath(query, false);
    File   dir        = SD.open(folderPath);
    if (!dir) {
        showNoResults();
        return;
    }
    int skipped = 0;
    resultCount = 0;
    File entry  = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            if (skipped < offset) {
                skipped++;
            } else if (resultCount < MAX_RESULTS) {
                results[resultCount] = String(entry.name());
                resultCount++;
            } else {
                break;
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
    displayResults();
}


// ═══════════════════════════════════════════════════════════════
//  Article display
// ═══════════════════════════════════════════════════════════════
void displayArticlePage(int page) {
    tft.fillScreen(ILI9341_BLACK);

    // header
    tft.fillRect(0, 0, 320, 20, OFF_WHITE);
    tft.fillRect(0, 20, 320, 2, ILI9341_WHITE);
    tft.setTextColor(ILI9341_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 6);
    tft.print(currentTitle);
    tft.setCursor(260, 6);
    tft.print("p." + String(page + 1) + "/" + String(totalPages));

    // load page text directly from SD
    String path     = buildPath(currentTitle, true);
    String pageText = readFilePage(path, page * CHARS_PER_PAGE, CHARS_PER_PAGE);

    // display with word wrapping
    String word = "";
    int x = 5, y = 28;
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    for (int i = 0; i < (int)pageText.length(); i++) {
        // get current char — add space at end to flush last word
        char c = (i < (int)pageText.length()) ? pageText[i] : ' ';
        if (c == ' ' || c == '\n') {
        if (word.length() > 0) {
            // calculate how wide this word is in pixels
            // each character at textSize 1 = 6px wide
            int wordWidth = word.length() * 6;

            // does the word fit on current line?
            if (x + wordWidth > 314) {
                // no — move to next line
                x  = 5;
                y += 10;
                if (y > 210) break;  // stop before footer
            }

            // draw the word
            tft.setCursor(x, y);
            tft.print(word);
            x += wordWidth + 4;  // 4px space between words
            word = "";
        }

        // handle newline
        if (c == '\n') {
            x  = 5;
            y += 10;
            if (y > 210) break;
        }

    } else {
        // build the word character by character
        word += c;
    }
    }

    // footer
    tft.fillRect(0, 218, 320, 2, ILI9341_WHITE);
    tft.fillRect(0, 220, 320, 20, OFF_WHITE);
    tft.setTextColor(ILI9341_BLACK);
    tft.setTextSize(1);
    tft.setCursor(5, 226);
    tft.print("[BACK]");
    if (page > 0) {
        tft.setCursor(90, 226);
        tft.print("[< PREV]");
    }
    if (page < totalPages - 1) {
        tft.setCursor(220, 226);
        tft.print("[NEXT >]");
    }
}

void loadArticle(String title) {
    currentTitle = title;
    currentPage  = 0;
    String path  = buildPath(title, true);
    // get file size to calculate total pages
    File f = SD.open(path);
    if (f) {
        int fileSize = f.size();
        totalPages   = (fileSize / CHARS_PER_PAGE) + 1;
        f.close();
    }
    currentScreen = 3;
    displayArticlePage(0);
}


// ═══════════════════════════════════════════════════════════════
//  Keyboard
// ═══════════════════════════════════════════════════════════════
void showKey(int x, int y, char letter) {
    tft.fillRoundRect(x, y, KEY_W, KEY_H, 3, ILI9341_BLACK);
    tft.drawRoundRect(x, y, KEY_W, KEY_H, 3, ILI9341_WHITE);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 8);
    tft.print(letter);
}

void showKeyboard() {
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < rowSizes[row]; col++) {
            int x = rowOffsets[row] + col * (KEY_W + KEY_GAP);
            int y = KB_Y + row * (KEY_H + KEY_GAP);
            showKey(x, y, keys[row][col]);
        }
    }
    // backspace
    int bsX = 20 + 8 * (KEY_W + KEY_GAP);
    int bsY = KB_Y + 2 * (KEY_H + KEY_GAP);
    tft.fillRoundRect(bsX, bsY, KEY_W, KEY_H, 3, ILI9341_BLACK);
    tft.drawRoundRect(bsX, bsY, KEY_W, KEY_H, 3, ILI9341_WHITE);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(bsX + 3, bsY + 8);
    tft.print("<-");
    // space bar
    int spY = KB_Y + 3 * (KEY_H + KEY_GAP);
    tft.fillRoundRect(20, spY, 140, KEY_H, 3, ILI9341_BLACK);
    tft.drawRoundRect(20, spY, 140, KEY_H, 3, ILI9341_WHITE);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(62, spY + 8);
    tft.print("SPACE");
    // search button
    tft.fillRoundRect(170, spY, 130, KEY_H, 3, ILI9341_WHITE);
    tft.setTextColor(ILI9341_BLACK);
    tft.setTextSize(1);
    tft.setCursor(185, spY + 8);
    tft.print("[ SEARCH ]");
}

char handleKeyPress(TS_Point p) {
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < rowSizes[row]; col++) {
            int x = rowOffsets[row] + col * (KEY_W + KEY_GAP);
            int y = KB_Y + row * (KEY_H + KEY_GAP);
            if (p.x >= x && p.x <= x + KEY_W &&
                p.y >= y && p.y <= y + KEY_H) {
                return keys[row][col];
            }
        }
    }
    int bsX = 20 + 8 * (KEY_W + KEY_GAP);
    int bsY = KB_Y + 2 * (KEY_H + KEY_GAP);
    if (p.x >= bsX && p.x <= bsX + KEY_W &&
        p.y >= bsY && p.y <= bsY + KEY_H) return '\b';

    int spY = KB_Y + 3 * (KEY_H + KEY_GAP);
    if (p.x >= 20  && p.x <= 160 &&
        p.y >= spY && p.y <= spY + KEY_H) return ' ';

    if (p.x >= 170 && p.x <= 300 &&
        p.y >= spY && p.y <= spY + KEY_H) return '\n';

    return 0;
}


// ═══════════════════════════════════════════════════════════════
//  Results tap detection
// ═══════════════════════════════════════════════════════════════
int getTappedResult(TS_Point p) {
    for (int i = 0; i < resultCount; i++) {
        int y = 106 + i * 22;
        if (p.x >= 10 && p.x <= 265 &&
            p.y >= y  && p.y <= y + 18) return i;
    }
    return -1;
}


// ═══════════════════════════════════════════════════════════════
//  Screens
// ═══════════════════════════════════════════════════════════════
void updateInputBox() {
    tft.fillRoundRect(10, 68, 300, 35, 3, ILI9341_BLACK);
    tft.drawRoundRect(10, 68, 300, 35, 3, ILI9341_WHITE);
    tft.drawRoundRect(12, 70, 296, 31, 3, MEDIUM_GRAY);
    tft.setTextSize(1);
    tft.setCursor(20, 82);
    if (searchInput.length() == 0) {
        tft.setTextColor(MEDIUM_GRAY);
        tft.print("Type to search...");
    } else {
        tft.setTextColor(ILI9341_WHITE);
        tft.print(searchInput);
    }
}

void showHomeScreen() {
    tft.fillScreen(ILI9341_BLACK);
    tft.fillRect(0, 0, 320, 50, OFF_WHITE);
    tft.fillRect(0, 50, 320, 4, ILI9341_WHITE);
    tft.setTextColor(ILI9341_BLACK);
    tft.setTextSize(2);
    tft.setCursor(75, 16);
    tft.println("POCKETWIKI");
    tft.fillRect(20, 65, 280, 1, LIGHT_GRAY);
    tft.fillRect(20, 68, 280, 1, LIGHT_GRAY);
    tft.setTextColor(LIGHT_GRAY);
    tft.setTextSize(1);
    tft.setCursor(60, 78);
    tft.println("Your offline encyclopedia");
    tft.fillRect(20, 92, 280, 1, LIGHT_GRAY);
    tft.fillRect(20, 95, 280, 1, LIGHT_GRAY);
    tft.fillRoundRect(40, 115, 240, 50, 4, ILI9341_BLACK);
    tft.drawRoundRect(40, 115, 240, 50, 4, ILI9341_WHITE);
    tft.drawRoundRect(42, 117, 236, 46, 4, MEDIUM_GRAY);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(82, 132);
    tft.println("[ SEARCH ]");
    tft.fillRect(0, 215, 320, 1, LIGHT_GRAY);
    tft.fillRect(0, 218, 320, 1, LIGHT_GRAY);
    tft.setTextColor(MEDIUM_GRAY);
    tft.setTextSize(1);
    tft.setCursor(105, 226);
    tft.println("made with love <3");
}

void showSearchScreen() {
    tft.fillScreen(ILI9341_BLACK);
    tft.fillRect(0, 0, 320, 55, OFF_WHITE);
    tft.fillRect(0, 55, 320, 4, ILI9341_WHITE);
    tft.setTextColor(ILI9341_BLACK);
    tft.setTextSize(1);
    tft.setCursor(8, 21);
    tft.println("[ < BACK ]");
    tft.setTextSize(2);
    tft.setCursor(105, 16);
    tft.println("SEARCH");
    tft.fillRect(20, 62, 280, 1, LIGHT_GRAY);
    tft.fillRect(20, 65, 280, 1, LIGHT_GRAY);
    updateInputBox();
    showKeyboard();
}

void showResultsScreen() {
    tft.fillScreen(ILI9341_BLACK);
    
    // header
    tft.fillRect(0, 0, 320, 50, OFF_WHITE);
    tft.fillRect(0, 50, 320, 4, ILI9341_WHITE);
    
    // back button
    tft.setTextColor(ILI9341_BLACK);
    tft.setTextSize(1);
    tft.setCursor(8, 21);
    tft.println("[ < BACK ]");
    
    // title
    tft.setTextSize(2);
    tft.setCursor(85, 16);
    tft.println("RESULTS");
    
    // decorative lines
    tft.fillRect(20, 65, 280, 1, LIGHT_GRAY);
    tft.fillRect(20, 68, 280, 1, LIGHT_GRAY);
    
    // query text
    tft.setTextColor(MEDIUM_GRAY);
    tft.setTextSize(1);
    tft.setCursor(20, 72);
    tft.print("Query: ");
    tft.print(searchInput);
    
    // divider under query
    tft.fillRect(20, 83, 280, 1, LIGHT_GRAY);
    
    // footer
    tft.fillRect(0, 215, 320, 1, LIGHT_GRAY);
    tft.fillRect(0, 218, 320, 1, LIGHT_GRAY);
    tft.setTextColor(MEDIUM_GRAY);
    tft.setCursor(105, 226);
    tft.println("made with love <3");
    
    // show results
    displayResults();
}


// ═══════════════════════════════════════════════════════════════
//  Sleep
// ═══════════════════════════════════════════════════════════════
void goToSleep() {
    // save state to RTC memory before sleeping
    rtcScreen = (int)currentScreen;
    rtcPage   = currentPage;
    currentTitle.toCharArray(rtcTitle, 64);
    searchInput.toCharArray(rtcQuery, 32);
 
    digitalWrite(TFT_LED, LOW);
    tft.fillScreen(ILI9341_BLACK);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)T_IRQ, 0);
    esp_deep_sleep_start();

}

// ═══════════════════════════════════════════════════════════════
//  State handlers
// ═══════════════════════════════════════════════════════════════
void handleHome(TS_Point p) {
    if (p.x >= 40 && p.x <= 280 &&
        p.y >= 115 && p.y <= 165) {
        currentScreen = SEARCH;
        searchInput   = "";
        resultOffset  = 0;
        resultCount   = 0;
        showSearchScreen();
    }
}
 
void handleSearch(TS_Point p) {
    // back button
    if (p.x >= 0 && p.x <= 80 &&
        p.y >= 0 && p.y <= 55) {
        currentScreen = HOME;
        showHomeScreen();
        return;
    }
    char key = handleKeyPress(p);
    if (key != 0) {
        if (key == '\b') {
            if (searchInput.length() > 0)
                searchInput = searchInput.substring(0, searchInput.length() - 1);
            updateInputBox();
        } else if (key == ' ') {
            searchInput += ' ';
            updateInputBox();
        } else if (key == '\n') {
            resultOffset = 0;
            resultCount  = 0;
            searchArticles(searchInput, 0);
            currentScreen = RESULTS;
            showResultsScreen();
        } else {
            searchInput += key;
            updateInputBox();
        }
    }
}
 
void handleResults(TS_Point p) {
    // back → search
    if (p.x >= 0 && p.x <= 80 &&
        p.y >= 0 && p.y <= 54) {
        currentScreen = SEARCH;
        searchInput   = "";
        showSearchScreen();
        return;
    }
    // load more
    if (p.x >= 270 && p.x <= 315 &&
        p.y >= 106 && p.y <= 106 + 5 * 22) {
        resultOffset += MAX_RESULTS;
        searchArticles(searchInput, resultOffset);
        return;
    }
    // tap result
    int tapped = getTappedResult(p);
    if (tapped != -1) {
        String title = results[tapped];
        title.replace(".txt", "");
        loadArticle(title);
    }
}
 
void handleArticle(TS_Point p) {
    // back → results
    if (p.x >= 0 && p.x <= 60 && p.y >= 220) {
        currentScreen = RESULTS;
        showResultsScreen();
        return;
    }
    // prev page
    if (p.x >= 80 && p.x <= 160 &&
        p.y >= 220 && currentPage > 0) {
        currentPage--;
        displayArticlePage(currentPage);
    }
    // next page
    if (p.x >= 200 && p.x <= 300 &&
        p.y >= 220 && currentPage < totalPages - 1) {
        currentPage++;
        displayArticlePage(currentPage);
    }
}
 
 
// ═══════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, HIGH);
    touch.begin();
    tft.begin();
    tft.setRotation(1);
    initSD();
 
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("woke from sleep!");
        // NEW: restore state from RTC memory after waking
        currentScreen = (Screen)rtcScreen;
        currentPage   = rtcPage;
        currentTitle  = String(rtcTitle);
        searchInput   = String(rtcQuery);
 
        if (currentScreen == ARTICLE) {
            loadArticle(currentTitle);
        } else if (currentScreen == RESULTS) {
            searchArticles(searchInput, 0);
            showResultsScreen();
        } else {
            currentScreen = HOME;
            showHomeScreen();
        }
    } else {
        showHomeScreen();
    }
 
    lastTouchTime = millis();
}
// ═══════════════════════════════════════════════════════════════
//  Loop
// ═══════════════════════════════════════════════════════════════
void loop() {
    // sleep check
    if (digitalRead(T_IRQ) == HIGH &&
        millis() - lastTouchTime > SLEEP_TIMEOUT) {
        goToSleep();
    }
 
    if (!touch.touched()) return;
 
    TS_Point p = getTouch();
    lastTouchTime = millis();
    delay(50);  // debounce
 
    switch (currentScreen) {
        case HOME:    handleHome(p);    break;
        case SEARCH:  handleSearch(p);  break;
        case RESULTS: handleResults(p); break;
        case ARTICLE: handleArticle(p); break;
    }
}
