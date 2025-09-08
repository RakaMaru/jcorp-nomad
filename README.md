# <div align="center">Jcorp Nomad</div>

<div align="center">
  <img src="Screenshot 2025-08-10 220031.png" alt="Jcorp Nomad Offline Media Server" width="800">
</div>

<p align="center"><b>A portable, offline media server powered by the ESP32-S3 in a thumbdrive form factor.</b><br>
Stream movies, music, books, and shows anywhere — no internet required.</p>

<p align="center">
  <img src="https://img.shields.io/badge/license-CC--BY--NC--SA%204.0-blue.svg" alt="License: CC BY-NC-SA 4.0" />
  <img src="https://img.shields.io/badge/platform-ESP32--S3-orange" alt="Platform: ESP32-S3" />
  <img src="https://img.shields.io/badge/status-beta-lightgrey" alt="Status: Beta" />
</p>

---

## Major Update: Experimental Features Merged Into Main

The **latest update** moves the majority of polished experimental branch features into the `main` branch!  
This means a more stable, refined experience for everyone with all the new goodies tested and ready.

This version also fixed the library errors. You must install the following libraraies in the manager.
  
  "ArduinoJson" by Benoit Blanchon v7.3.0
  
  "Async TCP" by ESP32Async v3.4.7
  
  "ESP Async Webserver" by ESP32Async v3.7.1
  
  "LVGL" by kisvegabor v8.4.0

---

## File Compatibility & Streaming

A compatibility guide for supported media types, streaming expectations, and additional notes on performance.

| Category      | Supported Formats                    | Notes                                                                                   |
|---------------|------------------------------------|-----------------------------------------------------------------------------------------|
| **Video**     | `.mp4`, `.mov`, `.mkv`, `.webm`   | Reliable playback for these formats. `.avi` and `.flv` are **not supported**.           |
| **Audio**     | `.mp3`, `.flac`, `.wav`             | Other audio formats are **not supported** by the built-in player.                       |
| **Books / Docs** | `.pdf` (recommended), `.epub` (download only) | `.pdf` files can be opened and read directly in the browser. `.epub` files are downloadable but cannot be viewed in the built-in reader. |

**Important:** Nomad uses FAT32 storage by default, which limits individual file sizes to under 4 GB.

---

### Streaming Performance (Typical)

The following stream counts are based on a high endurance Grade 10 microSD card under ideal conditions:

| Resolution            | Typical Concurrent Streams |
|-----------------------|----------------------------|
| 480p                  | 6–8 streams                |
| 720p                  | 3–5 streams                |
| 1080p                 | 2–3 streams                |
| Ultra-HD / High bitrate| ~1 stream                  |

**Disclaimer:** Actual streaming performance varies depending on:  
- File encoding and bitrate  
- Network conditions  
- Client device capabilities  
- Number of simultaneous users  

Encoding video files using **H.264** video codec and **AAC** audio codec tends to improve streaming performance and allow more concurrent streams.

---


## Known Issues (Being Worked On)

- Large directories (80+ GB) can cause the admin file browser to crash when opened.  
- Some bugs remain with SD Card storage reading on both the Screen UI and the admin panel.

These issues are actively being addressed and will be fixed in upcoming releases.

---

## Future Plans

Here’s what’s coming next for Nomad:

- **Custom JS Content Players:**  
  Rebuild the video, audio, and ebook players with custom JavaScript libraries to enable:  
  - Better video support including multiple audio tracks and subtitles  
  - Proper EPUB support with in-browser reading  
  - CBX/CBZ comic book support with better navigation  

- **Custom Emojis and Icons:**  
  Devices currently recolor default emojis used in the UI inconsistently. Custom emojis/icons will provide consistent branding and better UX.

- **Advanced Caching System:**  
  A complex browser-side caching mechanism to:  
  - Track progress of media playback (movies, shows)  
  - Offer "resume where you left off" options for the last 5 watched items

---

## New Store and Promo Videos

You can now **buy your own Nomad Dev Kit** and get started immediately!  
The product is open source, but this saves you time and effort on assembly.

**Promo Video:**  
[https://www.youtube.com/shorts/k8XJGTmbzzc](https://www.youtube.com/shorts/k8XJGTmbzzc)

**Demo Video**  
[https://www.youtube.com/watch?v=zEjgI8bhxzk&t=2s](https://www.youtube.com/watch?v=zEjgI8bhxzk&t=2s)

**Official Website:**  
[https://nomad.jcorptech.net](https://nomad.jcorptech.net)

---

<!-- Existing README content continues below -->

## Polish Update Disclaimer

This is a major update focused on **polish, stability, and usability improvements**.  
It includes:

- Full cleanup of reported bugs  
- Improved user interface  
- Rebuilt admin panel  
- New features across every section  
- All configuration options now available in the **frontend** — no need to modify firmware to change settings  
- Greatly enhanced file and media support  

---

## New Features in the Polish Update

### Music Page

- Song downloads  
- Loop songs  
- Sort songs A–Z  
- Shuffle songs and playlists  
- Playlist support via subfolders (flexible usage)  
- Playlists loop automatically  
- Expanded file type support: tested with `.mp3`, `.wav`, and `.flac` (others may work, untested)

### Books Page

- EPUB support added (no web reader yet, but download works)  
- Fixed footer CSS display issue

### Admin Panel (Completely Rebuilt)

- Restart the device directly from the panel  
- Switch to USB Mass Storage Mode with one click  
- SD card usage tracking with visual indicators  
- Control RGB LED color and mode  
- Change or disable the admin password (default is "password")  
- Change Wi-Fi SSID and password  
- Set device brightness (off option coming soon)  
- Toggle `media.json` auto-generation on boot  
- Generate `media.json` manually  
- Display connected Wi-Fi info and number of connected users  
- Integrated file system browser with jQueryFileTree (upload, delete, rename, download, create folder, inline editing)

### Gallery Page (New)

- Displays images with basic zoom functionality  
- Video playback support  
- Sortable display grid

### Files Page (New)

- Store and download any file type  
- Limited to FAT32 max file size (4 GB)  
- Useful for firmware, PDFs, and other non-media files

### Interface + UI Improvements

- LCD clearly shows USB Mass Storage Mode  
- Real-time file progress shown on screen during `media.json` generation  
- More intuitive screen feedback during long operations

### File Type & Playback Support

- Improved video file detection and browser compatibility  
- Works with most browser-supported video types  
- Unsupported files fall back to download behavior

### Admin Access Notes

- New "Settings" button leads to Admin Panel  
- Admin Password disabled by default
- If locked out, inspect (F12) the overlay and delete it manually  
- Settings persist across reboots

---


## What is Nomad

Jcorp Nomad is an open-source offline media server built for travel, remote work, classrooms, camping, and more. It runs entirely on an ESP32-S3 dev board, creates a local Wi-Fi hotspot, and serves media through a browser-accessible interface. It does not require internet access and works similarly to in-flight entertainment systems. It also allows multiple users watching seperate media streams at the same time. 

This project is designed to be compact, simple, and easily modifiable. It includes optional 3D-printable hardware and a fully open source firmware and web interface.

---

## Project Inspiration
This project was inspired by my experience running a Jellyfin server at home. I love having full control over my media library, and Jellyfin gave me everything I wanted > streaming movies, shows, books, and music, all from my own hardware. Naturally, I started looking for ways to take that experience on the go.

My first thought was to build a portable server, but I quickly ran into some major hurdles:

- Power-hungry hardware — Even low-power x86 boxes needed a hefty battery setup to stay running for long trips.

- High cost — SBCs like the Raspberry Pi 4, plus power banks, USB storage, and screen interfaces added up quickly.

- Heat and reliability — Trying to cram a full stack of services into a compact case often led to thermal issues and instability when running software meant for server hardware.

That’s when I pivoted.

Instead of replicating a full home media server, I focused on delivering the core experience:

- Offline access

- Local Wi-Fi hotspot

- Simple media browsing and playback

- Support for multiple users

The ESP32-S3 offered just enough performance to handle all of that - with a fraction of the power draw and cost.
The result is Nomad: a minimalist, reliable, and low-cost media server that delivers the essential features of a home streaming setup in a smaller than pocket sized format.

Is it fancy? No.
Does it work? Absolutely.
And it’s open-source, so anyone can expand, improve, and adapt it for their own needs.

---

## Features

- Creates a local Wi-Fi hotspot with captive portal  
- Serves HTML media interface to any connected device  
- Streams content directly from a FAT32-formatted microSD card  
- Supports simultaneous connections (tested with up to 4 video streams)  
- No app or internet connection required  
- Open source firmware and UI  
- Customizable web interface / Features

---


## Hardware Requirements

**Disclaimer:**  
The following links are Amazon affiliate links. Purchasing through these links supports this project at no additional cost to you. Thank you for your support!

- **Waveshare ESP32-S3 Dev Board (1.47" LCD version)**  
  [https://amzn.to/4ktB6oT](https://amzn.to/4ktB6oT)

- **FAT32-formatted microSD card (16 GB minimum recommended, 64 GB preferred)**  
  [https://amzn.to/44tM1c4](https://amzn.to/44tM1c4)

- **SD-Card Extender** Not needed, but useful for the latest case version, lets you get to the SD card without disasembly. 
  [https://amzn.to/45IWIJz](https://amzn.to/45IWIJz)

- **USB power source** (e.g., battery bank or computer)

- **Optional:** 3D-printed enclosure (STL files included in the repository)

---

## Software Requirements

- Arduino IDE or PlatformIO (to flash firmware)
- Python 3.x (to run `media.py`)
- SquareLine Studio (optional, for screen UI editing)

All software used is free and available on Windows, macOS, and Linux.

---

## Quick Start

1. Flash the ESP32-S3 with the firmware in the `/firmware/` directory.
2. Format your SD card as **FAT32** and copy the web files from `/SD_Card_Template/`.
3. Place your media files into the appropriate folders (see structure below).
4. Run `media.py` to generate `media.json` automatically.
5. Insert the SD card and power the device.
6. Connect to the Wi-Fi network named `NomadServer`.
7. Your browser will be redirected to the offline media interface.

---

## Folder Structure (on SD Card)

```
/Movies/
    Interstellar.mp4
    Interstellar.jpg
/Shows/
    The Office/
        S01E01 - Pilot.mp4
        S01E02 - Diversity Day.mp4
    The Office.jpg
/Books/
    The Martian.pdf
    The Martian.jpg
/Music/
    track01.mp3

index.html
appleindex.html
menu.html
movies.html
shows.html
books.html
music.html
media.py
media.json
placeholder.jpg
Logo.png
favicon.ico
```
---

## Supported Formats

- Video: `.mp4`
- Audio: `.mp3`
- Books: `.pdf`
- Images: `.jpg` (used for covers and folder images only)

Ensure all images and media files use matching names for proper display.

---

## Customization

- Wi-Fi name and password can be changed in `firmware/Nomad.ino`
- Branding (logo, favicon) can be replaced in `/SD_Card_Template/`
- Sections (Movies, Music, etc.) can be removed from `menu.html`
- Web interface can be edited using any text editor
- The screen UI can be edited with SquareLine Studio (the free version is fine) 

---

## What's Included

- `/firmware/` – Arduino firmware for ESP32-S3
- `/SD_Card_Template/` – Web UI, HTML files, and `media.py`
- `/case/` – STL files for optional 3D-printed enclosure
- `/docs/` – Logos, screenshots, and design files

---

## Troubleshooting

Common issues and their solutions are detailed in the Troubleshooting section on instructibles.

Topics covered include:

- SD card read errors
- Captive portal issues on some phones
- Video playback and seeking bugs
- UI glitches or screen failure

---

## Future Plans

These are features I'd like to explore in future updates. If you'd like to contribute, feel free to fork the repo or open a pull request!


### Offline Maps with GPS Support
Inspired by [Backcountry Beacon](https://www.instructables.com/USB-Powered-Offline-Map-Server/), the goal is to serve cached map tiles and display the live GPS position from the user's phone or connected device — entirely offline.

### HTML5 Games
Similar to [Gams Offline](https://github.com/Gams-Offline/Gams), I would like to embed simple HTML5 games that work in-browser. The selection in Gams is amazing and make me think of cool math games, would be neat to have even though most require a keyboard.

### Audiobook-Friendly Mode
Improve playback for long-form audio by adding bookmarks, chapter display, and smart pause/resume. Intended for better audiobook handling in the Music section. Potentialy add an entire new audiobook section or a seperate handling system for it in books (Under read it could have a listen option).

---

## Build Guide on Instructables

Looking for a step-by-step tutorial?  
Check out the full build guide on **Instructables** for detailed instructions, photos, and tips on setting up Jcorp Nomad.

👉 [Read the Instructables Guide](https://www.instructables.com/Jcorp-Nomad-Mini-WIFI-Media-Server/) 

---

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License](https://creativecommons.org/licenses/by-nc-sa/4.0/).

You may remix, adapt, and share this project **for non-commercial use**, as long as you give credit and share under the same terms.

For commercial licensing, please contact the author.


---

## Credits

Developed by **Jackson Studner (Jcorp Tech)**.  
Inspired by open-source offline projects like Backcountry Beacon.

If you build, remix, or improve this project, please consider submitting a pull request or tagging the project.

---


 
