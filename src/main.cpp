#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <ble_keyboard_mouse_client.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config.h"
#include "cyrillic_font.h"
#include "words.h"
#include "words_ru.h"

namespace {

constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;
constexpr int16_t GRID_X = 111;
constexpr int16_t FIBBLE_GRID_LEFT_X = 52;
constexpr int16_t FIBBLE_GRID_RIGHT_X = 170;
constexpr int16_t GRID_Y = 15;
constexpr int16_t TILE_SIZE = 18;
constexpr int16_t TILE_GAP = 2;
constexpr int16_t STATUS_Y = 135;
constexpr int16_t STATUS_HEIGHT = 12;
constexpr int16_t TOP_STATUS_X = 244;
constexpr int16_t TOP_STATUS_WIDTH = SCREEN_WIDTH - TOP_STATUS_X;
constexpr int16_t TOP_STATUS_HEIGHT = 14;
constexpr int16_t KEYBOARD_Y = 148;
constexpr int16_t KEY_HEIGHT = 27;
constexpr int16_t KEY_GAP = 2;
constexpr int16_t KEY_ROW3_Y = KEYBOARD_Y + 2 * (KEY_HEIGHT + KEY_GAP);

// Zoomed Fibble row inspector (drawn over the keyboard area).
constexpr int16_t INSPECTOR_TILE = 36;
constexpr int16_t INSPECTOR_GAP = 6;
constexpr int16_t INSPECTOR_X0 =
    (SCREEN_WIDTH - (5 * (INSPECTOR_TILE + INSPECTOR_GAP) - INSPECTOR_GAP)) / 2;
constexpr int16_t INSPECTOR_TILE_Y = 160;

// Wordle-style palette with boosted contrast: the green is deepened and the
// yellow brightened so the two differ strongly in lightness, which keeps them
// distinguishable for colour-blind players while staying classic Wordle.
constexpr uint16_t COLOR_BACKGROUND = 0x1082;    // #121213
constexpr uint16_t COLOR_PANEL = 0x2104;         // #212023 card/panel surface
constexpr uint16_t COLOR_TILE_BORDER = 0x39C7;   // #3A3A3C empty tile outline
constexpr uint16_t COLOR_TILE_ACTIVE = 0x52AB;   // #565758 outline once a letter is typed
constexpr uint16_t COLOR_ABSENT = 0x39C7;        // #3A3A3C
constexpr uint16_t COLOR_PRESENT = 0xEE28;       // #EEC643 bright yellow
constexpr uint16_t COLOR_CORRECT = 0x2BE6;       // #2E7D32 deep green
constexpr uint16_t COLOR_KEY = 0x8410;           // #818384
constexpr uint16_t COLOR_KEY_PRESSED = 0xAD55;   // lighter gray press feedback
constexpr uint16_t COLOR_TEXT = TFT_WHITE;
constexpr uint16_t COLOR_TEXT_DIM = 0x9CF3;      // #999C9E secondary text
constexpr uint16_t COLOR_ACCENT = 0x057D;
constexpr uint16_t COLOR_CONFIRMED = TFT_WHITE;
constexpr uint16_t COLOR_SUSPECT = TFT_BLACK;    // black flag reads on every tile colour

constexpr char KEY_ROWS[3][11] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
constexpr uint8_t KEY_COUNTS[3] = {10, 9, 7};

// Russian ЙЦУКЕН layout as alphabet indices (А=0 .. Я=31, Ё folded into Е).
constexpr uint8_t KEY_ROWS_RU[3][12] = {
    {9, 22, 19, 10, 5, 13, 3, 24, 25, 7, 21, 26},   // ЙЦУКЕНГШЩЗХЪ
    {20, 27, 2, 0, 15, 16, 14, 11, 4, 6, 29, 0},    // ФЫВАПРОЛДЖЭ
    {31, 23, 17, 12, 8, 18, 28, 1, 30, 0, 0, 0},    // ЯЧСМИТЬБЮ
};
constexpr uint8_t KEY_COUNTS_RU[3] = {12, 11, 9};

const char COMMON_ANSWERS[][6] PROGMEM = {
    "SALAD", "AGONY", "SHELL", "TARDY", "TEASE", "WAGON", "SWARM", "SWORD",
    "SHARP", "INERT", "CEASE", "FAMED", "PIXEL", "PRIZE", "MAPLE", "JUDGE",
    "SHINE", "SHORT", "LEARN", "SLIME", "THING", "SHORE", "DOUBT", "ALIVE",
    "MOGUL", "RAINY", "MIDGE", "NIECE", "EVOKE", "LEAFY", "BLANK", "TOXIC",
    "REACH", "GRIND", "TREAD", "FEVER", "GLOVE", "AGAIN", "MOTEL", "UPPER",
    "IDIOT", "CHEST", "WAVES", "VALVE", "SMITH", "KARMA", "FACTS", "SHOCK",
    "DAUNT", "CIDER", "SNOWY", "WORSE", "TONIC", "CROWN", "DANCE", "GRAIL",
    "LOTUS", "ETHIC", "BRIBE", "NYMPH", "MAGIC", "FOCUS", "STONY", "SNAIL",
    "CHAIR", "SPEND", "PIETY", "GLOBE", "LOVER", "WATCH", "WAIST", "PENNY",
    "LOYAL", "BAKED", "BRACE", "DEALT", "FLYER", "GUMMY", "TIARA", "ELATE",
    "MERCY", "AFFIX", "WHICH", "GROVE", "WASTE", "CLASH", "DRUID", "HYENA",
    "BEGUN", "GRAND", "ADAPT", "COUGH", "NINJA", "CROSS", "DENSE", "ROLLS",
    "SKIER", "SLUMP", "SOBER", "CRATE", "SOLAR", "COUCH", "FLOUT", "BIOME"
};
constexpr size_t ANSWER_COUNT = sizeof(COMMON_ANSWERS) / sizeof(COMMON_ANSWERS[0]);

enum class Mark : uint8_t { Empty = 0, Absent = 1, Present = 2, Correct = 3 };
enum class Annotation : uint8_t { None, Suspect, Confirmed };
enum class GameMode : uint8_t { Wordle, Fibble, DontWordle };
enum class Language : uint8_t { English = 0, Russian = 1 };
enum class KeyType : uint8_t {
    None,
    Letter,
    Enter,
    Delete,
    Tile,
    RandomStart,
    Undo,
    NewGame,
    ModeMenu,
    MenuButton,
    InspectorTile,
    InspectorClose,
    InspectorClear,
    ResetStats,
    ToggleLanguage,
    ChooseWordle,
    ChooseFibble,
    ChooseDontWordle
};

struct KeyHit {
    KeyType type;
    char letter;
    uint8_t row;
    uint8_t column;
};

// Paused game state, one slot per mode, so players can switch games and return.
struct GameSnapshot {
    bool active;
    Language language;
    char answer[6];
    char guesses[9][6];
    Mark marks[9][5];
    Annotation annotations[9][5];
    bool annotationTrueFirst[9][5];
    uint8_t lieColumns[9];
    Mark keyMarks[32];
    char currentGuess[6];
    uint8_t currentLength;
    uint8_t currentRow;
    uint8_t undosRemaining;
    size_t validWordsRemaining;
    uint32_t elapsedMs;
};

// Lifetime statistics per mode, persisted in NVS flash.
struct ModeStats {
    uint32_t played;
    uint32_t wins;
    uint32_t totalWinMs;
};

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN);
BLEHIDClient bluetoothHid;
QueueHandle_t bluetoothKeyQueue = nullptr;
char answer[6] = {};
char guesses[9][6] = {};
Mark marks[9][5] = {};
Annotation annotations[9][5] = {};
bool annotationTrueFirst[9][5] = {};
uint8_t lieColumns[9] = {};
Mark keyMarks[32] = {};
char currentGuess[6] = {};
uint8_t currentLength = 0;
uint8_t currentRow = 0;
uint8_t undosRemaining = 5;
size_t validWordsRemaining = WORD_COUNT;
GameMode gameMode = GameMode::Wordle;
Language language = Language::English;
const char (*activeWordList)[6] = WORD_LIST;
size_t activeWordCount = WORD_COUNT;
const char (*activeAnswers)[6] = COMMON_ANSWERS;
size_t activeAnswerCount = ANSWER_COUNT;
bool modeSelected = false;
bool gameOver = false;
bool inspectorOpen = false;
uint8_t inspectorRow = 0;
GameSnapshot snapshots[3] = {};
ModeStats stats[2][3] = {};  // per language, per mode
bool resetArmed = false;
Preferences preferences;
bool touchArmed = true;
uint32_t lastTouchMs = 0;
uint32_t touchReleaseStartedMs = 0;
uint32_t lastTouchDiagnosticMs = 0;
uint32_t gameStartedMs = 0;
uint32_t gameElapsedMs = 0;
uint32_t lastTopStatusPollMs = 0;
uint32_t renderedElapsedSeconds = UINT32_MAX;
int8_t renderedBluetoothState = -1;

void debugGame(const char* message) {
    if (SERIAL_DEBUG) {
        Serial.println(message);
    }
}

void applyLanguage() {
    if (language == Language::Russian) {
        activeWordList = WORD_LIST_RU;
        activeWordCount = WORD_COUNT_RU;
        activeAnswers = COMMON_ANSWERS_RU;
        activeAnswerCount = ANSWER_COUNT_RU;
    } else {
        activeWordList = WORD_LIST;
        activeWordCount = WORD_COUNT;
        activeAnswers = COMMON_ANSWERS;
        activeAnswerCount = ANSWER_COUNT;
    }
}

uint16_t markColor(Mark mark) {
    switch (mark) {
        case Mark::Correct: return COLOR_CORRECT;
        case Mark::Present: return COLOR_PRESENT;
        case Mark::Absent: return COLOR_ABSENT;
        default: return COLOR_BACKGROUND;
    }
}

uint16_t keyColor(Mark mark) {
    return mark == Mark::Empty ? COLOR_KEY : markColor(mark);
}

void drawCenteredText(const char* text, int16_t centerX, int16_t centerY, uint8_t font = 1,
                      uint16_t color = COLOR_TEXT) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(font);
    tft.setTextColor(color);
    tft.drawString(text, centerX, centerY);
}

// Draws a game letter centred at (cx, cy). English uses the built-in TFT
// fonts; Russian uses embedded normal-weight Terminus glyphs sized to match:
// enFont 1 pairs with the 6x12 face (8 px caps), larger fonts with the 8x16
// face (10 px caps). ruScale multiplies the Cyrillic pixel size.
void drawLetterCentered(char letter, int16_t cx, int16_t cy, uint8_t enFont,
                        uint8_t ruScale, uint16_t color = COLOR_TEXT) {
    if (language == Language::English) {
        char label[2] = {letter, '\0'};
        drawCenteredText(label, cx, cy, enFont, color);
        return;
    }
    const uint8_t index = letter - 'A';
    if (index >= 32) {
        return;
    }
    const bool small = enFont <= 1;
    const uint8_t width = small ? 6 : 8;
    const uint8_t height = small ? 12 : 16;
    // Offsets centre the capital-letter window (not the whole cell), so the
    // baseline lines up with the Latin fonts.
    const int16_t x0 = cx - (width / 2) * ruScale;
    const int16_t y0 = cy - (small ? 6 : 7) * ruScale;
    for (uint8_t row = 0; row < height; ++row) {
        const uint8_t bits = small ? pgm_read_byte(&CYRILLIC_FONT_6X12[index][row])
                                   : pgm_read_byte(&CYRILLIC_FONT_8X16[index][row]);
        for (uint8_t column = 0; column < width; ++column) {
            if (bits & (0x80 >> column)) {
                tft.fillRect(x0 + column * ruScale, y0 + row * ruScale,
                             ruScale, ruScale, color);
            }
        }
    }
}

uint8_t maximumGuesses() {
    return gameMode == GameMode::Fibble ? 9 : 6;
}

void tileGeometry(uint8_t row, uint8_t column, int16_t& x, int16_t& y) {
    uint8_t displayRow = row;
    x = GRID_X;
    if (gameMode == GameMode::Fibble) {
        x = row < 5 ? FIBBLE_GRID_LEFT_X : FIBBLE_GRID_RIGHT_X;
        displayRow = row < 5 ? row : row - 5;
    }
    x += column * (TILE_SIZE + TILE_GAP);
    y = GRID_Y + displayRow * (TILE_SIZE + TILE_GAP);
}

void drawTitle() {
    tft.fillRect(0, 0, TOP_STATUS_X, TOP_STATUS_HEIGHT, COLOR_BACKGROUND);
    tft.fillRoundRect(2, 0, 40, 13, 3, COLOR_PANEL);
    drawCenteredText("MENU", 22, 7, 1, COLOR_TEXT_DIM);
    const char* title = gameMode == GameMode::Fibble ? "FIBBLE" :
                        gameMode == GameMode::DontWordle ? "DON'T WORDLE" : "WORDLE";
    drawCenteredText(title, SCREEN_WIDTH / 2, 7, 1, COLOR_TEXT_DIM);
}

void drawTile(uint8_t row, uint8_t column, int16_t dx, int16_t dy) {
    int16_t x, y;
    tileGeometry(row, column, x, y);
    x += dx;
    y += dy;
    const Mark mark = marks[row][column];
    char letter = guesses[row][column];
    if (row == currentRow && !gameOver) {
        letter = column < currentLength ? currentGuess[column] : '\0';
    }
    const uint16_t fill = markColor(mark);
    const uint16_t border = mark != Mark::Empty ? fill :
                            letter != '\0' ? COLOR_TILE_ACTIVE : COLOR_TILE_BORDER;
    // Clear the full square first so annotation pixels outside the rounded
    // corners never linger after a mark is cleared.
    tft.fillRect(x, y, TILE_SIZE, TILE_SIZE, COLOR_BACKGROUND);
    tft.fillRoundRect(x, y, TILE_SIZE, TILE_SIZE, 3, fill);
    tft.drawRoundRect(x, y, TILE_SIZE, TILE_SIZE, 3, border);
    if (letter != '\0') {
        drawLetterCentered(letter, x + TILE_SIZE / 2, y + TILE_SIZE / 2 + 1, 2, 1);
    }
    if (annotations[row][column] == Annotation::Suspect) {
        tft.fillTriangle(x + TILE_SIZE - 9, y, x + TILE_SIZE - 1, y,
                         x + TILE_SIZE - 1, y + 8, COLOR_TEXT);
        tft.fillTriangle(x + TILE_SIZE - 7, y + 1, x + TILE_SIZE - 2, y + 1,
                         x + TILE_SIZE - 2, y + 6, COLOR_SUSPECT);
    } else if (annotations[row][column] == Annotation::Confirmed) {
        tft.drawRoundRect(x, y, TILE_SIZE, TILE_SIZE, 3, COLOR_CONFIRMED);
    }
}

void renderTile(uint8_t row, uint8_t column) {
    drawTile(row, column, 0, 0);
}

void renderGrid() {
    for (uint8_t row = 0; row < maximumGuesses(); ++row) {
        for (uint8_t column = 0; column < 5; ++column) {
            renderTile(row, column);
        }
    }
}

// Brief outline pulse when a letter is typed into a tile.
void animatePop(uint8_t row, uint8_t column) {
    int16_t x, y;
    tileGeometry(row, column, x, y);
    tft.drawRoundRect(x - 1, y - 1, TILE_SIZE + 2, TILE_SIZE + 2, 3, COLOR_TILE_ACTIVE);
    delay(40);
    tft.drawRoundRect(x - 1, y - 1, TILE_SIZE + 2, TILE_SIZE + 2, 3, COLOR_BACKGROUND);
}

// Horizontal shake for rejected guesses.
void animateShakeRow(uint8_t row) {
    int16_t x0, y0;
    tileGeometry(row, 0, x0, y0);
    const int16_t width = 5 * (TILE_SIZE + TILE_GAP) - TILE_GAP;
    static constexpr int8_t offsets[] = {4, -4, 3, -3, 2, 0};
    for (int8_t offset : offsets) {
        tft.fillRect(x0 - 5, y0, width + 10, TILE_SIZE, COLOR_BACKGROUND);
        for (uint8_t column = 0; column < 5; ++column) {
            drawTile(row, column, offset, 0);
        }
        delay(26);
    }
}

// Wordle-style flip: each tile collapses, then expands in its scored color.
void animateRevealRow(uint8_t row) {
    for (uint8_t column = 0; column < 5; ++column) {
        int16_t x, y;
        tileGeometry(row, column, x, y);
        const uint16_t fill = markColor(marks[row][column]);
        for (int16_t h = TILE_SIZE - 4; h > 2; h -= 5) {
            tft.fillRect(x, y, TILE_SIZE, TILE_SIZE, COLOR_BACKGROUND);
            tft.fillRect(x, y + (TILE_SIZE - h) / 2, TILE_SIZE, h, COLOR_TILE_ACTIVE);
            delay(16);
        }
        for (int16_t h = 4; h < TILE_SIZE; h += 5) {
            tft.fillRect(x, y, TILE_SIZE, TILE_SIZE, COLOR_BACKGROUND);
            tft.fillRect(x, y + (TILE_SIZE - h) / 2, TILE_SIZE, h, fill);
            delay(16);
        }
        tft.fillRect(x, y, TILE_SIZE, TILE_SIZE, COLOR_BACKGROUND);
        renderTile(row, column);
    }
}

// Left-to-right bounce across the winning row.
void animateWinRow(uint8_t row) {
    static constexpr int8_t lifts[] = {-3, -6, -7, -6, -3, 0};
    for (uint8_t column = 0; column < 5; ++column) {
        int16_t x, y;
        tileGeometry(row, column, x, y);
        for (int8_t dy : lifts) {
            tft.fillRect(x, y - 8, TILE_SIZE, TILE_SIZE + 8, COLOR_BACKGROUND);
            drawTile(row, column, 0, dy);
            delay(22);
        }
        delay(30);
    }
    // Repair the strip that the bounce cleared above the row.
    int16_t x, y;
    tileGeometry(row, 0, x, y);
    if (y - 8 < GRID_Y) {
        drawTitle();
    } else if (row > 0) {
        for (uint8_t column = 0; column < 5; ++column) {
            renderTile(row - 1, column);
        }
    }
}

void keyGeometry(uint8_t row, uint8_t index, int16_t& x, int16_t& y, int16_t& width) {
    y = KEYBOARD_Y + row * (KEY_HEIGHT + KEY_GAP);
    if (language == Language::Russian) {
        if (row == 0) {
            width = 24;
            x = 5 + index * (width + KEY_GAP);
        } else if (row == 1) {
            width = 26;
            x = 7 + index * (width + KEY_GAP);
        } else {
            width = 24;
            x = 44 + index * (width + KEY_GAP);
        }
        return;
    }
    if (row == 0) {
        width = 29;
        x = 6 + index * (width + KEY_GAP);
    } else if (row == 1) {
        width = 30;
        x = 17 + index * (width + KEY_GAP);
    } else {
        width = 27;
        x = 58 + index * (width + KEY_GAP);
    }
}

uint8_t keyRowCount(uint8_t row) {
    return language == Language::Russian ? KEY_COUNTS_RU[row] : KEY_COUNTS[row];
}

char keyLetterAt(uint8_t row, uint8_t index) {
    return language == Language::Russian ?
           static_cast<char>('A' + KEY_ROWS_RU[row][index]) : KEY_ROWS[row][index];
}

void enterKeyGeometry(int16_t& x, int16_t& width) {
    x = language == Language::Russian ? 4 : 6;
    width = language == Language::Russian ? 36 : 50;
}

void deleteKeyGeometry(int16_t& x, int16_t& width) {
    x = language == Language::Russian ? 280 : 263;
    width = language == Language::Russian ? 36 : 51;
}

void drawKey(int16_t x, int16_t y, int16_t width, const char* label, uint16_t color) {
    tft.fillRoundRect(x, y, width, KEY_HEIGHT, 4, color);
    drawCenteredText(label, x + width / 2, y + KEY_HEIGHT / 2 + 1, 1);
}

// True when the player confirmed any tile of this letter as truthful in
// Fibble; the key's own colour then tells what was confirmed (green/yellow:
// letter is in the word, gray: letter is definitely absent).
bool letterConfirmedInWord(char letter) {
    if (gameMode != GameMode::Fibble) {
        return false;
    }
    for (uint8_t row = 0; row < currentRow; ++row) {
        for (uint8_t column = 0; column < 5; ++column) {
            if (guesses[row][column] == letter &&
                annotations[row][column] == Annotation::Confirmed) {
                return true;
            }
        }
    }
    return false;
}

void drawLetterKey(int16_t x, int16_t y, int16_t width, char letter, uint16_t color) {
    tft.fillRoundRect(x, y, width, KEY_HEIGHT, 4, color);
    if (letterConfirmedInWord(letter)) {
        tft.drawRoundRect(x, y, width, KEY_HEIGHT, 4, COLOR_CONFIRMED);
    }
    drawLetterCentered(letter, x + width / 2, y + KEY_HEIGHT / 2 + 1, 1, 1);
}

void renderKeyboard() {
    tft.fillRect(0, KEYBOARD_Y, SCREEN_WIDTH, SCREEN_HEIGHT - KEYBOARD_Y, COLOR_BACKGROUND);
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t index = 0; index < keyRowCount(row); ++index) {
            int16_t x, y, width;
            keyGeometry(row, index, x, y, width);
            const char letter = keyLetterAt(row, index);
            drawLetterKey(x, y, width, letter, keyColor(keyMarks[letter - 'A']));
        }
    }
    int16_t x, width;
    enterKeyGeometry(x, width);
    drawKey(x, KEY_ROW3_Y, width, language == Language::Russian ? "OK" : "ENTER", COLOR_KEY);
    deleteKeyGeometry(x, width);
    drawKey(x, KEY_ROW3_Y, width, "DEL", COLOR_KEY);
}

void flashLetterKey(char letter) {
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t index = 0; index < keyRowCount(row); ++index) {
            if (keyLetterAt(row, index) != letter) {
                continue;
            }
            int16_t x, y, width;
            keyGeometry(row, index, x, y, width);
            drawLetterKey(x, y, width, letter, COLOR_KEY_PRESSED);
            delay(45);
            drawLetterKey(x, y, width, letter, keyColor(keyMarks[letter - 'A']));
            return;
        }
    }
}

void flashEnterKey() {
    int16_t x, width;
    enterKeyGeometry(x, width);
    const char* label = language == Language::Russian ? "OK" : "ENTER";
    drawKey(x, KEY_ROW3_Y, width, label, COLOR_KEY_PRESSED);
    delay(45);
    drawKey(x, KEY_ROW3_Y, width, label, COLOR_KEY);
}

void flashDeleteKey() {
    int16_t x, width;
    deleteKeyGeometry(x, width);
    drawKey(x, KEY_ROW3_Y, width, "DEL", COLOR_KEY_PRESSED);
    delay(45);
    drawKey(x, KEY_ROW3_Y, width, "DEL", COLOR_KEY);
}

void renderDontWordleDashboard() {
    if (gameMode != GameMode::DontWordle) return;
    tft.fillRect(0, 15, 104, 116, COLOR_BACKGROUND);
    tft.fillRoundRect(4, 16, 96, 66, 5, COLOR_PANEL);
    drawCenteredText("WORDS LEFT", 52, 26, 1, COLOR_TEXT_DIM);
    char countText[12];
    snprintf(countText, sizeof(countText), "%u", static_cast<unsigned>(validWordsRemaining));
    drawCenteredText(countText, 52, 42, 2);
    drawCenteredText("UNDOS", 52, 58, 1, COLOR_TEXT_DIM);
    snprintf(countText, sizeof(countText), "%u", undosRemaining);
    drawCenteredText(countText, 52, 72, 2);
    const bool canAct = currentRow == 0 || undosRemaining > 0;
    tft.fillRoundRect(8, 89, 88, 30, 5, canAct ? COLOR_ACCENT : COLOR_ABSENT);
    drawCenteredText(currentRow == 0 ? "RANDOM" : "UNDO", 52, 104, 1);
}

void showStatus(const char* message, uint16_t color = COLOR_TEXT_DIM) {
    tft.fillRect(0, STATUS_Y, SCREEN_WIDTH, STATUS_HEIGHT, COLOR_BACKGROUND);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(color);
    tft.drawString(message, SCREEN_WIDTH / 2, STATUS_Y + STATUS_HEIGHT / 2);
}

// --- Fibble row inspector: zoomed tiles for easy finger marking ---

Annotation cycleAnnotation(uint8_t row, uint8_t column) {
    Annotation& annotation = annotations[row][column];
    bool rowHasSuspect = false;
    for (uint8_t c = 0; c < 5; ++c) {
        if (c != column && annotations[row][c] == Annotation::Suspect) {
            rowHasSuspect = true;
            break;
        }
    }
    if (annotation == Annotation::None) {
        annotationTrueFirst[row][column] = rowHasSuspect;
        annotation = rowHasSuspect ? Annotation::Confirmed : Annotation::Suspect;
    } else if (annotationTrueFirst[row][column]) {
        annotation = annotation == Annotation::Confirmed ? Annotation::Suspect :
                                                           Annotation::None;
    } else {
        annotation = annotation == Annotation::Suspect ? Annotation::Confirmed :
                                                         Annotation::None;
    }
    return annotation;
}

void drawRowHighlight(uint8_t row, uint16_t color) {
    int16_t x, y;
    tileGeometry(row, 0, x, y);
    const int16_t width = 5 * (TILE_SIZE + TILE_GAP) - TILE_GAP;
    tft.drawRoundRect(x - 2, y - 2, width + 4, TILE_SIZE + 4, 4, color);
}

void drawInspectorTile(uint8_t column) {
    const int16_t x = INSPECTOR_X0 + column * (INSPECTOR_TILE + INSPECTOR_GAP);
    const int16_t y = INSPECTOR_TILE_Y;
    tft.fillRect(x, y, INSPECTOR_TILE, INSPECTOR_TILE, COLOR_BACKGROUND);
    tft.fillRoundRect(x, y, INSPECTOR_TILE, INSPECTOR_TILE, 5,
                      markColor(marks[inspectorRow][column]));
    drawLetterCentered(guesses[inspectorRow][column], x + INSPECTOR_TILE / 2,
                       y + INSPECTOR_TILE / 2 + 1, 2, 2);
    const Annotation annotation = annotations[inspectorRow][column];
    if (annotation == Annotation::Suspect) {
        tft.fillTriangle(x + INSPECTOR_TILE - 16, y, x + INSPECTOR_TILE - 1, y,
                         x + INSPECTOR_TILE - 1, y + 15, COLOR_TEXT);
        tft.fillTriangle(x + INSPECTOR_TILE - 13, y + 1, x + INSPECTOR_TILE - 2, y + 1,
                         x + INSPECTOR_TILE - 2, y + 12, COLOR_SUSPECT);
    } else if (annotation == Annotation::Confirmed) {
        tft.drawRoundRect(x, y, INSPECTOR_TILE, INSPECTOR_TILE, 5, COLOR_CONFIRMED);
    }
}

void openInspector(uint8_t row) {
    if (inspectorOpen && inspectorRow != row) {
        drawRowHighlight(inspectorRow, COLOR_BACKGROUND);
    }
    inspectorOpen = true;
    inspectorRow = row;
    drawRowHighlight(row, COLOR_ACCENT);
    tft.fillRect(0, KEYBOARD_Y, SCREEN_WIDTH, SCREEN_HEIGHT - KEYBOARD_Y, COLOR_BACKGROUND);
    char label[16];
    snprintf(label, sizeof(label), "MARK ROW %u", row + 1);
    drawCenteredText(label, SCREEN_WIDTH / 2, 152, 1, COLOR_TEXT_DIM);
    for (uint8_t column = 0; column < 5; ++column) {
        drawInspectorTile(column);
    }
    tft.fillRoundRect(30, 202, 120, 32, 6, COLOR_KEY);
    drawCenteredText("CLEAR", 90, 218, 2);
    tft.fillRoundRect(170, 202, 120, 32, 6, COLOR_ACCENT);
    drawCenteredText("DONE", 230, 218, 2);
    showStatus("Tap tiles to mark, rows to switch");
}

void closeInspector() {
    inspectorOpen = false;
    drawRowHighlight(inspectorRow, COLOR_BACKGROUND);
    renderKeyboard();
    showStatus("Tap a row to mark clues");
}

uint8_t bluetoothStatus() {
    if (!BLUETOOTH_KEYBOARD_ENABLED || bluetoothKeyQueue == nullptr) {
        return 0;
    }
    return NimBLEDevice::getConnectedClients().empty() ? 1 : 2;
}

void drawBluetoothIcon(int16_t cx, int16_t cy, uint16_t color) {
    tft.drawFastVLine(cx, cy - 5, 11, color);
    tft.drawLine(cx, cy - 5, cx + 3, cy - 3, color);
    tft.drawLine(cx + 3, cy - 3, cx - 3, cy + 3, color);
    tft.drawLine(cx, cy + 5, cx + 3, cy + 3, color);
    tft.drawLine(cx + 3, cy + 3, cx - 3, cy - 3, color);
}

void renderTopStatus(bool force = false) {
    const uint32_t elapsedMs = modeSelected ? (gameOver ? gameElapsedMs : millis() - gameStartedMs) : 0;
    const uint32_t elapsedSeconds = elapsedMs / 1000;
    const uint8_t btState = bluetoothStatus();
    if (!force && elapsedSeconds == renderedElapsedSeconds &&
        btState == renderedBluetoothState) {
        return;
    }
    renderedElapsedSeconds = elapsedSeconds;
    renderedBluetoothState = btState;

    char timer[6];
    if (modeSelected) {
        const uint32_t minutes = min<uint32_t>(elapsedSeconds / 60, 99);
        snprintf(timer, sizeof(timer), "%02u:%02u",
                 static_cast<unsigned>(minutes),
                 static_cast<unsigned>(elapsedSeconds % 60));
    } else {
        memcpy(timer, "--:--", sizeof(timer));
    }

    tft.fillRect(TOP_STATUS_X, 0, TOP_STATUS_WIDTH, TOP_STATUS_HEIGHT, COLOR_BACKGROUND);
    tft.fillRoundRect(TOP_STATUS_X, 0, TOP_STATUS_WIDTH, TOP_STATUS_HEIGHT, 4, COLOR_PANEL);
    tft.setTextFont(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_TEXT);
    tft.drawString(timer, TOP_STATUS_X + 8, TOP_STATUS_HEIGHT / 2);
    drawBluetoothIcon(TOP_STATUS_X + 60, TOP_STATUS_HEIGHT / 2,
                      btState == 2 ? COLOR_CORRECT :
                      btState == 1 ? COLOR_PRESENT : COLOR_ABSENT);
}

void pollTopStatus() {
    if (millis() - lastTopStatusPollMs < 250) {
        return;
    }
    lastTopStatusPollMs = millis();
    renderTopStatus();
}

void renderEndPanel(bool won) {
    tft.fillRect(0, KEYBOARD_Y, SCREEN_WIDTH, SCREEN_HEIGHT - KEYBOARD_Y, COLOR_BACKGROUND);
    // Reveal the answer as a row of mini tiles.
    constexpr int16_t miniSize = 18;
    constexpr int16_t miniGap = 3;
    int16_t x = (SCREEN_WIDTH - (5 * (miniSize + miniGap) - miniGap)) / 2;
    const uint16_t fill = won ? COLOR_CORRECT : COLOR_PRESENT;
    for (uint8_t i = 0; i < 5; ++i) {
        tft.fillRoundRect(x, 150, miniSize, miniSize, 3, fill);
        drawLetterCentered(answer[i], x + miniSize / 2, 150 + miniSize / 2 + 1, 1, 1);
        x += miniSize + miniGap;
    }
    tft.fillRoundRect(34, 174, 120, 38, 6, COLOR_ACCENT);
    drawCenteredText("NEW GAME", 94, 193, 2);
    tft.fillRoundRect(166, 174, 120, 38, 6, COLOR_KEY);
    drawCenteredText("MODES", 226, 193, 2);
    drawCenteredText(won ? "Nice! Play again?" : "Better luck next time",
                     SCREEN_WIDTH / 2, 224, 1, COLOR_TEXT_DIM);
}

constexpr char TITLE_WORD[] = "WORDLE";
constexpr uint16_t TITLE_TILE_COLORS[6] = {
    COLOR_CORRECT, COLOR_PRESENT, COLOR_ABSENT,
    COLOR_CORRECT, COLOR_ABSENT, COLOR_PRESENT};

void drawTitleTiles(int16_t y, int16_t size, int16_t gap, bool animate) {
    int16_t x = (SCREEN_WIDTH - (6 * (size + gap) - gap)) / 2;
    for (uint8_t i = 0; i < 6; ++i) {
        if (animate) {
            for (int16_t h = 4; h < size; h += 6) {
                tft.fillRect(x, y, size, size, COLOR_BACKGROUND);
                tft.fillRect(x, y + (size - h) / 2, size, h, TITLE_TILE_COLORS[i]);
                delay(16);
            }
        }
        tft.fillRoundRect(x, y, size, size, 4, TITLE_TILE_COLORS[i]);
        char label[2] = {TITLE_WORD[i], '\0'};
        drawCenteredText(label, x + size / 2, y + size / 2 + 1, 2);
        x += size + gap;
    }
}

void renderSplash() {
    tft.fillScreen(COLOR_BACKGROUND);
    drawTitleTiles(92, 30, 5, true);
    drawCenteredText("ESP32 CYD WORD GAMES", SCREEN_WIDTH / 2, 148, 1, COLOR_TEXT_DIM);
    delay(700);
}

void renderModeMenu() {
    modeSelected = false;
    gameOver = false;
    inspectorOpen = false;
    tft.fillScreen(COLOR_BACKGROUND);
    drawTitleTiles(16, 26, 4, false);
    drawCenteredText("CHOOSE A GAME MODE", SCREEN_WIDTH / 2, 60, 1, COLOR_TEXT_DIM);
    tft.fillRoundRect(2, 0, 40, 13, 3, COLOR_PANEL);
    drawCenteredText(language == Language::Russian ? "RU" : "EN", 22, 7, 1, COLOR_TEXT);
    resetArmed = false;
    tft.fillRoundRect(46, 0, 54, 13, 3, COLOR_PANEL);
    drawCenteredText("RESET", 73, 7, 1, COLOR_TEXT_DIM);

    struct ModeCard {
        const char* name;
        uint16_t color;
        const char* line1;
        const char* line2;
    };
    static const ModeCard cards[3] = {
        {"WORDLE", COLOR_CORRECT, "Guess the word", "in six tries"},
        {"FIBBLE", COLOR_PRESENT, "One clue lies", "in every row"},
        {"DON'T", COLOR_ACCENT, "Avoid the word", "for six turns"},
    };    for (uint8_t i = 0; i < 3; ++i) {
        const int16_t x = 8 + i * 104;
        tft.fillRoundRect(x, 78, 96, 54, 6, COLOR_PANEL);
        tft.fillRoundRect(x, 78, 96, 20, 6, cards[i].color);
        tft.fillRect(x, 90, 96, 8, cards[i].color);
        drawCenteredText(cards[i].name, x + 48, 88, 1);
        if (snapshots[i].active) {
            drawCenteredText("IN PROGRESS", x + 48, 106, 1, COLOR_CORRECT);
            tft.fillRoundRect(x + 6, 114, 84, 14, 3, COLOR_KEY);
            drawCenteredText("NEW GAME", x + 48, 121, 1);
        } else {
            drawCenteredText(cards[i].line1, x + 48, 108, 1, COLOR_TEXT_DIM);
            drawCenteredText(cards[i].line2, x + 48, 121, 1, COLOR_TEXT_DIM);
        }

        char line[20];
        const ModeStats& modeStats = stats[static_cast<uint8_t>(language)][i];
        snprintf(line, sizeof(line), "P:%u  W:%u",
                 static_cast<unsigned>(modeStats.played),
                 static_cast<unsigned>(modeStats.wins));
        drawCenteredText(line, x + 48, 142, 1, COLOR_TEXT_DIM);
        if (modeStats.wins > 0) {
            const uint32_t averageSeconds = modeStats.totalWinMs / modeStats.wins / 1000;
            snprintf(line, sizeof(line), "avg %u:%02u",
                     static_cast<unsigned>(averageSeconds / 60),
                     static_cast<unsigned>(averageSeconds % 60));
        } else {
            snprintf(line, sizeof(line), "avg -:--");
        }
        drawCenteredText(line, x + 48, 155, 1, COLOR_TEXT_DIM);
    }
    drawCenteredText("Touchscreen or BLE keyboard", SCREEN_WIDTH / 2, 175, 1, COLOR_TEXT_DIM);
    drawCenteredText(language == Language::Russian ?
                         "Russian words - tap RU to switch" :
                         "English words - tap EN to switch",
                     SCREEN_WIDTH / 2, 190, 1, COLOR_TEXT_DIM);
    renderTopStatus(true);
}

// --- Pause / resume and persistent statistics ---

void saveCurrentGame() {
    if (!modeSelected) {
        return;
    }
    GameSnapshot& snap = snapshots[static_cast<uint8_t>(gameMode)];
    if (gameOver) {
        snap.active = false;
        return;
    }
    snap.active = true;
    snap.language = language;
    memcpy(snap.answer, answer, sizeof(answer));
    memcpy(snap.guesses, guesses, sizeof(guesses));
    memcpy(snap.marks, marks, sizeof(marks));
    memcpy(snap.annotations, annotations, sizeof(annotations));
    memcpy(snap.annotationTrueFirst, annotationTrueFirst, sizeof(annotationTrueFirst));
    memcpy(snap.lieColumns, lieColumns, sizeof(lieColumns));
    memcpy(snap.keyMarks, keyMarks, sizeof(keyMarks));
    memcpy(snap.currentGuess, currentGuess, sizeof(currentGuess));
    snap.currentLength = currentLength;
    snap.currentRow = currentRow;
    snap.undosRemaining = undosRemaining;
    snap.validWordsRemaining = validWordsRemaining;
    snap.elapsedMs = millis() - gameStartedMs;
}

void resumeGame() {
    GameSnapshot& snap = snapshots[static_cast<uint8_t>(gameMode)];
    snap.active = false;
    if (snap.language != language) {
        language = snap.language;
        preferences.putUChar("lang", static_cast<uint8_t>(language));
        applyLanguage();
    }
    memcpy(answer, snap.answer, sizeof(answer));
    memcpy(guesses, snap.guesses, sizeof(guesses));
    memcpy(marks, snap.marks, sizeof(marks));
    memcpy(annotations, snap.annotations, sizeof(annotations));
    memcpy(annotationTrueFirst, snap.annotationTrueFirst, sizeof(annotationTrueFirst));
    memcpy(lieColumns, snap.lieColumns, sizeof(lieColumns));
    memcpy(keyMarks, snap.keyMarks, sizeof(keyMarks));
    memcpy(currentGuess, snap.currentGuess, sizeof(currentGuess));
    currentLength = snap.currentLength;
    currentRow = snap.currentRow;
    undosRemaining = snap.undosRemaining;
    validWordsRemaining = snap.validWordsRemaining;
    modeSelected = true;
    gameOver = false;
    inspectorOpen = false;
    gameStartedMs = millis() - snap.elapsedMs;
    gameElapsedMs = 0;

    tft.fillScreen(COLOR_BACKGROUND);
    drawTitle();
    renderTopStatus(true);
    renderGrid();
    renderDontWordleDashboard();
    showStatus("Game resumed");
    renderKeyboard();
    debugGame("Resumed saved game");
}

void loadStats() {
    preferences.begin("cydwordle", false);
    language = preferences.getUChar("lang", 0) == 1 ? Language::Russian : Language::English;
    applyLanguage();
    for (uint8_t lang = 0; lang < 2; ++lang) {
        for (uint8_t i = 0; i < 3; ++i) {
            char key[6];
            char legacy[4];
            // English stats fall back to the pre-language key names.
            snprintf(legacy, sizeof(legacy), "p%u", i);
            snprintf(key, sizeof(key), "p%u%u", lang, i);
            stats[lang][i].played =
                preferences.getUInt(key, lang == 0 ? preferences.getUInt(legacy, 0) : 0);
            snprintf(legacy, sizeof(legacy), "w%u", i);
            snprintf(key, sizeof(key), "w%u%u", lang, i);
            stats[lang][i].wins =
                preferences.getUInt(key, lang == 0 ? preferences.getUInt(legacy, 0) : 0);
            snprintf(legacy, sizeof(legacy), "t%u", i);
            snprintf(key, sizeof(key), "t%u%u", lang, i);
            stats[lang][i].totalWinMs =
                preferences.getUInt(key, lang == 0 ? preferences.getUInt(legacy, 0) : 0);
        }
    }
}

void recordGameResult(bool won) {
    const uint8_t mode = static_cast<uint8_t>(gameMode);
    const uint8_t lang = static_cast<uint8_t>(language);
    ModeStats& modeStats = stats[lang][mode];
    ++modeStats.played;
    if (won) {
        ++modeStats.wins;
        modeStats.totalWinMs += gameElapsedMs;
    }
    char key[6];
    snprintf(key, sizeof(key), "p%u%u", lang, mode);
    preferences.putUInt(key, modeStats.played);
    snprintf(key, sizeof(key), "w%u%u", lang, mode);
    preferences.putUInt(key, modeStats.wins);
    snprintf(key, sizeof(key), "t%u%u", lang, mode);
    preferences.putUInt(key, modeStats.totalWinMs);
}

void resetStats() {
    const uint8_t lang = static_cast<uint8_t>(language);
    for (uint8_t mode = 0; mode < 3; ++mode) {
        stats[lang][mode] = {};
        char key[6];
        snprintf(key, sizeof(key), "p%u%u", lang, mode);
        preferences.remove(key);
        snprintf(key, sizeof(key), "w%u%u", lang, mode);
        preferences.remove(key);
        snprintf(key, sizeof(key), "t%u%u", lang, mode);
        preferences.remove(key);
        if (lang == 0) {
            // Also drop the pre-language keys so they cannot resurrect
            // the English stats through the loadStats() fallback.
            snprintf(key, sizeof(key), "p%u", mode);
            preferences.remove(key);
            snprintf(key, sizeof(key), "w%u", mode);
            preferences.remove(key);
            snprintf(key, sizeof(key), "t%u", mode);
            preferences.remove(key);
        }
    }
}

void copyProgmemWord(const char source[][6], size_t index, char destination[6]) {
    for (uint8_t i = 0; i < 5; ++i) {
        destination[i] = static_cast<char>(pgm_read_byte(&source[index][i]));
    }
    destination[5] = '\0';
}

bool wordIsValid(const char* word) {
    char candidate[6];
    size_t low = 0;
    size_t high = activeWordCount;
    while (low < high) {
        const size_t index = low + (high - low) / 2;
        copyProgmemWord(activeWordList, index, candidate);
        const int comparison = memcmp(candidate, word, 5);
        if (comparison == 0) {
            return true;
        }
        if (comparison < 0) {
            low = index + 1;
        } else {
            high = index;
        }
    }
    return false;
}

void scoreGuess(const char* guess, Mark result[5]) {
    uint8_t remaining[32] = {};
    for (uint8_t i = 0; i < 5; ++i) {
        if (guess[i] == answer[i]) {
            result[i] = Mark::Correct;
        } else {
            result[i] = Mark::Absent;
            ++remaining[answer[i] - 'A'];
        }
    }
    for (uint8_t i = 0; i < 5; ++i) {
        const uint8_t letterIndex = guess[i] - 'A';
        if (result[i] != Mark::Correct && remaining[letterIndex] > 0) {
            result[i] = Mark::Present;
            --remaining[letterIndex];
        }
    }
}

bool respectsDontWordleClues(const char* candidate) {
    uint8_t candidateCounts[32] = {};
    for (uint8_t column = 0; column < 5; ++column) {
        ++candidateCounts[candidate[column] - 'A'];
    }

    for (uint8_t row = 0; row < currentRow; ++row) {
        uint8_t totalCounts[32] = {};
        uint8_t coloredCounts[32] = {};
        for (uint8_t column = 0; column < 5; ++column) {
            const uint8_t letter = guesses[row][column] - 'A';
            ++totalCounts[letter];
            if (marks[row][column] == Mark::Correct) {
                ++coloredCounts[letter];
                if (candidate[column] != guesses[row][column]) return false;
            } else if (marks[row][column] == Mark::Present) {
                ++coloredCounts[letter];
                if (candidate[column] == guesses[row][column]) return false;
            }
        }
        for (uint8_t letter = 0; letter < 32; ++letter) {
            if (candidateCounts[letter] < coloredCounts[letter]) return false;
            if (totalCounts[letter] > coloredCounts[letter] &&
                candidateCounts[letter] > coloredCounts[letter]) return false;
        }
    }
    return true;
}

bool wasAlreadyGuessed(const char* candidate) {
    for (uint8_t row = 0; row < currentRow; ++row) {
        if (memcmp(candidate, guesses[row], 5) == 0) return true;
    }
    return false;
}

size_t countValidDontWordleWords() {
    size_t count = 0;
    char candidate[6];
    for (size_t index = 0; index < activeWordCount; ++index) {
        copyProgmemWord(activeWordList, index, candidate);
        if (!wasAlreadyGuessed(candidate) && respectsDontWordleClues(candidate)) {
            ++count;
        }
    }
    return count;
}

void updateKeyboard(const char* guess, const Mark result[5]) {
    for (uint8_t i = 0; i < 5; ++i) {
        Mark& stored = keyMarks[guess[i] - 'A'];
        if (static_cast<uint8_t>(result[i]) > static_cast<uint8_t>(stored)) {
            stored = result[i];
        }
    }
}

void addFibbleLie(Mark result[5], uint8_t row) {
    const uint8_t lieColumn = esp_random() % 5;
    lieColumns[row] = lieColumn;
    const Mark truth = result[lieColumn];
    Mark lie;
    do {
        lie = static_cast<Mark>(1 + esp_random() % 3);
    } while (lie == truth);
    result[lieColumn] = lie;
    if (SERIAL_DEBUG) {
        Serial.printf("Fibble lie: row=%u column=%u truth=%u shown=%u\n",
                      row + 1, lieColumn + 1, static_cast<uint8_t>(truth),
                      static_cast<uint8_t>(lie));
    }
}

void seedFibbleGuess() {
    char seed[6];
    do {
        copyProgmemWord(activeAnswers, esp_random() % activeAnswerCount, seed);
    } while (memcmp(seed, answer, 5) == 0);

    memcpy(guesses[0], seed, sizeof(seed));
    scoreGuess(seed, marks[0]);
    addFibbleLie(marks[0], 0);
    updateKeyboard(seed, marks[0]);
    currentRow = 1;
    if (SERIAL_DEBUG) {
        Serial.printf("Fibble seed guess: %s\n", seed);
    }
}

void startNewGame() {
    snapshots[static_cast<uint8_t>(gameMode)].active = false;
    memset(guesses, 0, sizeof(guesses));
    memset(marks, 0, sizeof(marks));
    memset(annotations, 0, sizeof(annotations));
    memset(annotationTrueFirst, 0, sizeof(annotationTrueFirst));
    memset(lieColumns, 0, sizeof(lieColumns));
    memset(keyMarks, 0, sizeof(keyMarks));
    memset(currentGuess, 0, sizeof(currentGuess));
    currentLength = 0;
    currentRow = 0;
    undosRemaining = 5;
    validWordsRemaining = activeWordCount;
    modeSelected = true;
    gameOver = false;
    inspectorOpen = false;
    gameStartedMs = millis();
    gameElapsedMs = 0;

    copyProgmemWord(activeAnswers, esp_random() % activeAnswerCount, answer);
    if (gameMode == GameMode::Fibble) {
        seedFibbleGuess();
    }
    tft.fillScreen(COLOR_BACKGROUND);
    drawTitle();
    renderTopStatus(true);
    renderGrid();
    renderDontWordleDashboard();
    showStatus(gameMode == GameMode::Fibble ? "One clue lies in every row" :
               gameMode == GameMode::DontWordle ? "Avoid the hidden word" :
                                                   "Enter a five-letter word");
    renderKeyboard();

    if (SERIAL_DEBUG) {
        Serial.printf("New game. Answer: %s\n", answer);
    }
}

void finishGame(bool won) {
    gameElapsedMs = millis() - gameStartedMs;
    gameOver = true;
    snapshots[static_cast<uint8_t>(gameMode)].active = false;
    recordGameResult(won);
    renderTopStatus(true);
    if (gameMode == GameMode::Fibble) {
        memset(annotations, 0, sizeof(annotations));
        // The winning row has no lie, so reveal lies only in earlier rows.
        const uint8_t lieRows = won ? currentRow : currentRow + 1;
        for (uint8_t row = 0; row < lieRows; ++row) {
            annotations[row][lieColumns[row]] = Annotation::Suspect;
        }
        renderGrid();
    }
    if (won && gameMode != GameMode::DontWordle) {
        animateWinRow(currentRow);
    }
    char message[28];
    if (gameMode == GameMode::DontWordle) {
        snprintf(message, sizeof(message), won ? "You avoided it!" :
                 language == Language::Russian ? "You hit the hidden word!" : "Oops: %s",
                 answer);
    } else if (won) {
        snprintf(message, sizeof(message), "Solved in %u/%u!", currentRow + 1,
                 maximumGuesses());
    } else if (language == Language::Russian) {
        snprintf(message, sizeof(message), "The answer is shown below");
    } else {
        snprintf(message, sizeof(message), "Answer: %s", answer);
    }
    showStatus(message, won ? COLOR_CORRECT : COLOR_PRESENT);
    renderEndPanel(won);
    debugGame(won ? "Game won" : "Game lost");
}

void submitGuess() {
    if (currentLength != 5) {
        animateShakeRow(currentRow);
        showStatus("Enter exactly five letters", COLOR_PRESENT);
        return;
    }
    if (!wordIsValid(currentGuess)) {
        animateShakeRow(currentRow);
        showStatus("Not in word list", COLOR_PRESENT);
        debugGame("Rejected invalid word");
        return;
    }
    if (gameMode == GameMode::DontWordle && !respectsDontWordleClues(currentGuess)) {
        animateShakeRow(currentRow);
        showStatus("Guess breaks known clues", COLOR_PRESENT);
        debugGame("Rejected clue-breaking guess");
        return;
    }

    memcpy(guesses[currentRow], currentGuess, 6);
    scoreGuess(currentGuess, marks[currentRow]);
    const bool won = memcmp(currentGuess, answer, 5) == 0;
    if (gameMode == GameMode::Fibble && !won) {
        addFibbleLie(marks[currentRow], currentRow);
    }
    updateKeyboard(currentGuess, marks[currentRow]);
    animateRevealRow(currentRow);
    if (gameMode == GameMode::DontWordle) {
        if (won) {
            finishGame(false);
            return;
        }
        if (currentRow + 1 == maximumGuesses()) {
            finishGame(true);
            return;
        }
        ++currentRow;
        currentLength = 0;
        memset(currentGuess, 0, sizeof(currentGuess));
        validWordsRemaining = countValidDontWordleWords();
        renderDontWordleDashboard();
        showStatus(validWordsRemaining == 0 ? "No legal words: use UNDO" : "Keep avoiding it");
        renderKeyboard();
        return;
    }

    if (SERIAL_DEBUG) {
        Serial.printf("Accepted guess %u: %s\n", currentRow + 1, currentGuess);
    }
    if (won) {
        finishGame(true);
        return;
    }
    if (currentRow + 1 == maximumGuesses()) {
        finishGame(false);
        return;
    }

    ++currentRow;
    currentLength = 0;
    memset(currentGuess, 0, sizeof(currentGuess));
    showStatus(gameMode == GameMode::Fibble ? "Tap a row to mark clues" : "Keep going");
    renderKeyboard();
}

void rebuildKeyboardMarks() {
    memset(keyMarks, 0, sizeof(keyMarks));
    for (uint8_t row = 0; row < currentRow; ++row) {
        updateKeyboard(guesses[row], marks[row]);
    }
}

void undoDontWordleGuess() {
    if (currentRow == 0 || undosRemaining == 0) {
        showStatus(currentRow == 0 ? "Nothing to undo" : "No undos remaining", COLOR_PRESENT);
        return;
    }
    --currentRow;
    --undosRemaining;
    memset(guesses[currentRow], 0, sizeof(guesses[currentRow]));
    memset(marks[currentRow], 0, sizeof(marks[currentRow]));
    memset(currentGuess, 0, sizeof(currentGuess));
    currentLength = 0;
    rebuildKeyboardMarks();
    validWordsRemaining = countValidDontWordleWords();
    renderGrid();
    renderDontWordleDashboard();
    renderKeyboard();
    showStatus("Last guess undone");
    if (SERIAL_DEBUG) {
        Serial.printf("Undo used. Remaining: %u\n", undosRemaining);
    }
}

void startRandomDontWordleGuess() {
    do {
        copyProgmemWord(activeAnswers, esp_random() % activeAnswerCount, currentGuess);
    } while (memcmp(currentGuess, answer, 5) == 0);
    currentLength = 5;
    if (SERIAL_DEBUG) {
        Serial.printf("Don't Wordle random start: %s\n", currentGuess);
    }
    submitGuess();
}

KeyHit hitTest(int16_t x, int16_t y) {
    if (!modeSelected) {
        if (x < 44 && y < 15) {
            return {KeyType::ToggleLanguage, '\0', 0, 0};
        }
        if (x >= 46 && x < 100 && y < 15) {
            return {KeyType::ResetStats, '\0', 0, 0};
        }
        if (y >= 78 && y < 136) {
            // Tapping the NEW GAME strip of an in-progress card discards the
            // paused game (row = 1) instead of resuming it.
            const uint8_t forceNew = y >= 114 && y < 129 ? 1 : 0;
            if (x >= 8 && x < 104) return {KeyType::ChooseWordle, '\0', forceNew, 0};
            if (x >= 112 && x < 208) return {KeyType::ChooseFibble, '\0', forceNew, 0};
            if (x >= 216 && x < 312) return {KeyType::ChooseDontWordle, '\0', forceNew, 0};
        }
        return {KeyType::None, '\0', 0, 0};
    }

    if (inspectorOpen) {
        if (y >= INSPECTOR_TILE_Y && y < INSPECTOR_TILE_Y + INSPECTOR_TILE) {
            for (uint8_t column = 0; column < 5; ++column) {
                const int16_t tileX = INSPECTOR_X0 + column * (INSPECTOR_TILE + INSPECTOR_GAP);
                if (x >= tileX && x < tileX + INSPECTOR_TILE) {
                    return {KeyType::InspectorTile, '\0', inspectorRow, column};
                }
            }
        }
        if (y >= 202 && y < 234) {
            if (x >= 30 && x < 150) {
                return {KeyType::InspectorClear, '\0', 0, 0};
            }
            if (x >= 170 && x < 290) {
                return {KeyType::InspectorClose, '\0', 0, 0};
            }
        }
        // Tapping another submitted row on the grid switches the inspector
        // to that row without needing to close it first.
        for (uint8_t row = 0; row < currentRow; ++row) {
            for (uint8_t column = 0; column < 5; ++column) {
                int16_t tileX, tileY;
                tileGeometry(row, column, tileX, tileY);
                if (x >= tileX && x < tileX + TILE_SIZE &&
                    y >= tileY && y < tileY + TILE_SIZE) {
                    return {KeyType::Tile, '\0', row, column};
                }
            }
        }
        return {KeyType::None, '\0', 0, 0};
    }

    if (x < 44 && y < 15) {
        return {KeyType::MenuButton, '\0', 0, 0};
    }

    if (gameMode == GameMode::DontWordle && x >= 8 && x < 96 && y >= 89 && y < 119) {
        return {currentRow == 0 ? KeyType::RandomStart : KeyType::Undo, '\0', 0, 0};
    }
    if (gameOver) {
        if (x >= 34 && x < 154 && y >= 174 && y < 212)
            return {KeyType::NewGame, '\0', 0, 0};
        if (x >= 166 && x < 286 && y >= 174 && y < 212)
            return {KeyType::ModeMenu, '\0', 0, 0};
        return {KeyType::None, '\0', 0, 0};
    }

    if (gameMode == GameMode::Fibble) {
        for (uint8_t row = 0; row < currentRow; ++row) {
            for (uint8_t column = 0; column < 5; ++column) {
                int16_t tileX, tileY;
                tileGeometry(row, column, tileX, tileY);
                if (x >= tileX && x < tileX + TILE_SIZE &&
                    y >= tileY && y < tileY + TILE_SIZE) {
                    return {KeyType::Tile, '\0', row, column};
                }
            }
        }
    }

    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t index = 0; index < keyRowCount(row); ++index) {
            int16_t keyX, keyY, keyWidth;
            keyGeometry(row, index, keyX, keyY, keyWidth);
            if (x >= keyX && x < keyX + keyWidth && y >= keyY && y < keyY + KEY_HEIGHT) {
                return {KeyType::Letter, keyLetterAt(row, index), 0, 0};
            }
        }
    }
    if (y >= KEY_ROW3_Y && y < KEY_ROW3_Y + KEY_HEIGHT) {
        int16_t keyX, keyWidth;
        enterKeyGeometry(keyX, keyWidth);
        if (x >= keyX && x < keyX + keyWidth) return {KeyType::Enter, '\0', 0, 0};
        deleteKeyGeometry(keyX, keyWidth);
        if (x >= keyX && x < keyX + keyWidth) return {KeyType::Delete, '\0', 0, 0};
    }
    return {KeyType::None, '\0', 0, 0};
}

void handleInput(const KeyHit& hit) {
    switch (hit.type) {
        case KeyType::Letter:
            if (currentLength < 5) {
                flashLetterKey(hit.letter);
                currentGuess[currentLength++] = hit.letter;
                currentGuess[currentLength] = '\0';
                renderTile(currentRow, currentLength - 1);
                animatePop(currentRow, currentLength - 1);
                if (currentLength == 5) {
                    showStatus("Tap ENTER to submit");
                }
            }
            break;
        case KeyType::Delete:
            flashDeleteKey();
            if (currentLength > 0) {
                --currentLength;
                currentGuess[currentLength] = '\0';
                renderTile(currentRow, currentLength);
            }
            break;
        case KeyType::Enter:
            flashEnterKey();
            submitGuess();
            break;
        case KeyType::Tile:
            openInspector(hit.row);
            break;
        case KeyType::InspectorTile: {
            const Annotation annotation = cycleAnnotation(hit.row, hit.column);
            drawInspectorTile(hit.column);
            renderTile(hit.row, hit.column);
            showStatus(annotation == Annotation::Suspect ? "Corner flag: suspected lie" :
                       annotation == Annotation::Confirmed ? "White outline: known true" :
                                                              "Clue mark cleared");
            break;
        }
        case KeyType::InspectorClose:
            closeInspector();
            break;
        case KeyType::InspectorClear:
            for (uint8_t column = 0; column < 5; ++column) {
                annotations[inspectorRow][column] = Annotation::None;
                annotationTrueFirst[inspectorRow][column] = false;
                drawInspectorTile(column);
                renderTile(inspectorRow, column);
            }
            showStatus("Row marks cleared");
            break;
        case KeyType::ToggleLanguage:
            language = language == Language::Russian ? Language::English : Language::Russian;
            preferences.putUChar("lang", static_cast<uint8_t>(language));
            applyLanguage();
            renderModeMenu();
            break;
        case KeyType::ResetStats:
            if (!resetArmed) {
                resetArmed = true;
                tft.fillRoundRect(46, 0, 54, 13, 3, COLOR_PRESENT);
                drawCenteredText("SURE?", 73, 7, 1, COLOR_BACKGROUND);
                break;
            }
            resetStats();
            renderModeMenu();
            break;
        case KeyType::Undo:
            undoDontWordleGuess();
            break;
        case KeyType::RandomStart:
            startRandomDontWordleGuess();
            break;
        case KeyType::NewGame:
            startNewGame();
            break;
        case KeyType::MenuButton:
        case KeyType::ModeMenu:
            saveCurrentGame();
            renderModeMenu();
            break;
        case KeyType::ChooseWordle:
            gameMode = GameMode::Wordle;
            snapshots[0].active && hit.row == 0 ? resumeGame() : startNewGame();
            break;
        case KeyType::ChooseFibble:
            gameMode = GameMode::Fibble;
            snapshots[1].active && hit.row == 0 ? resumeGame() : startNewGame();
            break;
        case KeyType::ChooseDontWordle:
            gameMode = GameMode::DontWordle;
            snapshots[2].active && hit.row == 0 ? resumeGame() : startNewGame();
            break;
        default:
            break;
    }
}

void onBluetoothKeyPressed(bool isModifier, uint8_t usage) {
    if (isModifier || bluetoothKeyQueue == nullptr) {
        return;
    }
    xQueueSend(bluetoothKeyQueue, &usage, 0);
}

void onBluetoothKeyReleased(bool, uint8_t) {}

// Maps an HID usage code to an internal letter for the active language.
// Russian uses the standard ЙЦУКЕН mapping of the physical QWERTY keys.
char letterFromUsage(uint8_t usage) {
    if (language == Language::Russian) {
        static constexpr uint8_t QWERTY_TO_RU[26] = {
            20, 8, 17, 2, 19, 0, 15, 16, 24, 14, 11, 4, 28,
            18, 25, 7, 9, 10, 27, 5, 3, 12, 22, 23, 13, 31};
        if (usage >= 0x04 && usage <= 0x1D) {
            return static_cast<char>('A' + QWERTY_TO_RU[usage - 0x04]);
        }
        switch (usage) {
            case 0x2F: return 'A' + 21;  // [ -> Х
            case 0x30: return 'A' + 26;  // ] -> Ъ
            case 0x33: return 'A' + 6;   // ; -> Ж
            case 0x34: return 'A' + 29;  // ' -> Э
            case 0x36: return 'A' + 1;   // , -> Б
            case 0x37: return 'A' + 30;  // . -> Ю
            default: return '\0';
        }
    }
    if (usage >= 0x04 && usage <= 0x1D) {
        return static_cast<char>('A' + usage - 0x04);
    }
    return '\0';
}

void beginBluetoothKeyboard() {
    if (!BLUETOOTH_KEYBOARD_ENABLED) {
        return;
    }
    bluetoothKeyQueue = xQueueCreate(8, sizeof(uint8_t));
    if (bluetoothKeyQueue == nullptr) {
        debugGame("Bluetooth keyboard queue allocation failed");
        return;
    }
    bluetoothHid.begin(BLUETOOTH_DEVICE_NAME, true, false);
    bluetoothHid.get_keyboard().on_key_pressed(onBluetoothKeyPressed);
    bluetoothHid.get_keyboard().on_key_released(onBluetoothKeyReleased);
    debugGame("Bluetooth keyboard scanning started");
}

void pollBluetoothKeyboard() {
    if (!BLUETOOTH_KEYBOARD_ENABLED || bluetoothKeyQueue == nullptr) {
        return;
    }
    bluetoothHid.loop();

    uint8_t usage = 0;
    while (xQueueReceive(bluetoothKeyQueue, &usage, 0) == pdTRUE) {
        if (!modeSelected || gameOver || inspectorOpen) {
            continue;
        }
        const char letter = letterFromUsage(usage);
        if (letter != '\0') {
            handleInput({KeyType::Letter, letter, 0, 0});
        } else if (usage == 0x28 || usage == 0x58) {
            handleInput({KeyType::Enter, '\0', 0, 0});
        } else if (usage == 0x2A || usage == 0x4C) {
            handleInput({KeyType::Delete, '\0', 0, 0});
        }
    }
}

void pollTouch() {
    const bool touched = touch.touched();
    TS_Point point;
    const bool diagnosticDue = TOUCH_DIAGNOSTICS &&
                               millis() - lastTouchDiagnosticMs >= TOUCH_DIAGNOSTIC_INTERVAL_MS;
    if (touched || diagnosticDue) {
        point = touch.getPoint();
        if (diagnosticDue) {
            lastTouchDiagnosticMs = millis();
            Serial.printf("Touch raw: x=%d y=%d z=%d irq=%d\n",
                          point.x, point.y, point.z, digitalRead(TOUCH_IRQ_PIN));
        }
    }

    const bool pressed = touched && point.z >= TOUCH_PRESSURE_MIN;
    if (pressed) {
        touchReleaseStartedMs = 0;
    }

    if (pressed && touchArmed && millis() - lastTouchMs >= TOUCH_DEBOUNCE_MS) {
        touchArmed = false;
        lastTouchMs = millis();
        const uint16_t x = constrain(map(point.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX,
                                         0, SCREEN_WIDTH - 1), 0, SCREEN_WIDTH - 1);
        const uint16_t y = constrain(map(point.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX,
                                         0, SCREEN_HEIGHT - 1), 0, SCREEN_HEIGHT - 1);
        if (SERIAL_DEBUG) {
            Serial.printf("Touch mapped: x=%u y=%u z=%d\n", x, y, point.z);
        }
        handleInput(hitTest(x, y));
    }

    if (!pressed && !touchArmed) {
        if (touchReleaseStartedMs == 0) {
            touchReleaseStartedMs = millis();
        } else if (millis() - touchReleaseStartedMs >= TOUCH_RELEASE_STABLE_MS) {
            touchArmed = true;
            touchReleaseStartedMs = 0;
            if (SERIAL_DEBUG) {
                Serial.println("Touch rearmed after stable release");
            }
        }
    }
}

}  // namespace

void setup() {
    if (SERIAL_DEBUG) {
        Serial.begin(SERIAL_BAUD);
    }
    pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(TFT_BACKLIGHT_PIN, TFT_BACKLIGHT_ON);

    tft.init();
    tft.setRotation(TFT_ROTATION);
    tft.setTextWrap(false);

    touchSpi.begin(TOUCH_SCLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
    touch.begin(touchSpi);
    touch.setRotation(TOUCH_ROTATION);
    pinMode(TOUCH_IRQ_PIN, INPUT);
    loadStats();
    renderSplash();
    renderModeMenu();
    beginBluetoothKeyboard();
}

void loop() {
    pollTouch();
    pollBluetoothKeyboard();
    pollTopStatus();
}
