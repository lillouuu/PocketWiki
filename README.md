# PocketWiki

A portable, offline Wikipedia reader built on ESP32. No internet required  browse thousands of encyclopedia articles anywhere, anytime.

---

## What is PocketWiki?

PocketWiki is a handheld device that lets you read Wikipedia articles without an internet connection. Articles are stored on a MicroSD card and navigated through a touchscreen interface. Built for students, curious minds, and anyone in low-connectivity environments.

---

##  Features

-  **Touch keyboard search**  type any topic and find matching articles instantly
-  **Paginated reading**  articles split into pages with PREV/NEXT navigation
-  **Smart sleep/wake**  auto sleeps after 2 minutes of inactivity, resumes exactly where you left off
-  **Nested folder structure**  lightning fast search even with thousands of articles
-  **4-screen navigation**  Home � Search � Results � Article
-  **Battery powered**  LiPo battery + TP4056 charging module
-  **Retro UI**  clean black and white aesthetic

---

##  Hardware Requirements

| Component | Details |
|---|---|
| Microcontroller | ESP32 Development Board |
| Display | 2.4" ILI9341 SPI TFT LCD (MSP2402) |
| Touch controller | XPT2046 (built into MSP2402) |
| Storage | MicroSD card 8GB+ formatted FAT32 |
| Power | LiPo battery 3.7V + TP4056 charging module |
| Button | Momentary tactile push button (EN pin reset) |
| Misc | Breadboard + jumper wires |

---

##  Wiring

| TFT/SD/Touch Pin | ESP32 GPIO |
|---|---|
| VCC | 3.3V |
| GND | GND |
| MOSI | 23 |
| MISO | 19 |
| SCK | 18 |
| TFT CS | 2 |
| DC | 4 |
| RST | 22 |
| LED | 15 |
| SD CS | 5 |
| T_CS | 21 |
| T_IRQ | 27 |
| Button | EN + GND |

---

##  Project Structure

```
PocketWiki/
 firmware/                  # ESP32 Arduino firmware
    src/
       main.cpp           # Main firmware code
    platformio.ini         # PlatformIO config + libraries
 downloadArticles.py        # Python script to download Wikipedia articles
 .gitignore
 README.md
```

---

##  Getting Started

### Step 1  Download Wikipedia articles

Install dependencies:
```bash
pip install wikipedia-api requests google-generativeai python-dotenv
```

Create a `.env` file in the root:
```
GEMINI_API_KEY=your-key-here
```

Run the download script:
```bash
python downloadArticles.py
```

This will:
1. Use Gemini AI to generate 50 interesting Wikipedia categories
2. Fetch up to 200 articles per category
3. Clean and save them as `.txt` files in a nested folder structure
4. Copy them to your MicroSD card

---

### Step 2  Flash the firmware

1. Install [VSCode](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/) extension
2. Open the `firmware/` folder in VSCode
3. PlatformIO will automatically install all libraries
4. Connect your ESP32 via USB
5. Click the **� Upload** button in the bottom toolbar

---

### Step 3  Assemble hardware

1. Wire components according to the wiring table above
2. Insert the MicroSD card with downloaded articles
3. Power via USB or LiPo battery
4. Press the EN button to boot

---

##  How It Works

### Article Storage
Articles are stored in a nested folder structure based on the title:
```
articles/
  p/
    y/
      t/
        h/
          o/
            n/
              python.txt
```
This allows the ESP32 to search without loading thousands of filenames into RAM.

### Navigation Flow
```
Home Screen
    � tap [ SEARCH ]
Search Screen (keyboard)
    � type + tap [ SEARCH ]
Results Screen
    � tap an article
Article Screen
    � PREV / NEXT / BACK
```

### Sleep & Wake
- Device sleeps after **2 minutes** of no touch
- Touch screen to wake up
- RTC memory saves your exact position  resumes where you left off

---

##  Built With

- [Arduino Framework](https://www.arduino.cc/)
- [PlatformIO](https://platformio.org/)
- [Adafruit ILI9341](https://github.com/adafruit/Adafruit_ILI9341)
- [XPT2046 Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)
- [Wikipedia API (Python)](https://pypi.org/project/Wikipedia-API/)
- [Google Gemini API](https://ai.google.dev/)

---

##  Libraries (PlatformIO)

```ini
lib_deps = 
    adafruit/Adafruit ILI9341
    adafruit/Adafruit GFX Library
    paulstoffregen/XPT2046_Touchscreen
```


##  Author

Made with love <3 by a student who wanted Wikipedia in their pocket.

---

##  License

MIT License  free to use, modify and distribute.