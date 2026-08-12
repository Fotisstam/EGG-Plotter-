# 32-Channel EEG Signal Analyzer

An open-source, high-density real-time Python desktop application for acquiring, processing, and analyzing 32-channel EEG (Electroencephalogram) signal data streamed over a serial (UART) connection from an STM32 board.

---

## Key Software Features

* **32-Channel 10-20 System Support:** Full support for 32 standard EEG electrode positions (`Fp1`, `Fpz`, `Fp2`, `F7`, `F3`, `Fz`, `F4`, `F8`, `FT7`, `FC3`, etc.) with individual visibility toggles and customizable channel color palettes.
* **Hardware Connection Manager:** Dedicated hardware link module featuring port scanning, connection management, and real-time connection status monitoring for STM32 serial communications.
* **Real-Time Frequency Analysis & Peak Tracking:** Live spectral analysis per channel with configurable `X Max (Hz)` (up to 128 Hz) and `Y Max (mag)` limits, complete with automated peak frequency detection (`peak: X Hz`).
* **Flexible Display Geometry & Grid Customization:** Multi-column layout controls (`Cols`), merge view options, dynamic plot height adjustment, scale sliders, and one-click `Fit to Data`.
* **Session Controls & Data Capture:** Built-in display freeze (`Freeze`), band filter toggles (`Bands`), live data session recording (`Record`), and instant frame captures (`Snapshot`).
* **Electrode Matrix Control:** Quick-access `All On` / `All Off` switches alongside individual electrode matrix checkable lists for targeted multi-lead inspection.

---

## Application Interface

<p align="center">
  <img width="100%" alt="32-Channel EEG Signal Analyzer Interface" src="<img width="3440" height="1392" alt="image" src="https://github.com/user-attachments/assets/ff66bf79-1eae-45ab-b116-4c3c7e43797d"
" />
</p>

---

