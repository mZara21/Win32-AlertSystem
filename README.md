# Win32 Alert System

![C](https://img.shields.io/badge/c-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)
![Systems Programming](https://img.shields.io/badge/Domain-Systems_Programming-8A2BE2?style=for-the-badge)
![IPC](https://img.shields.io/badge/Architecture-IPC_&_Multithreading-25A162?style=for-the-badge)

A multi-process alert management system developed in **C** using the **Windows API (Win32)**. This project demonstrates advanced Operating Systems concepts, specifically Inter-Process Communication (IPC), multithreading, and process synchronization.

## System Architecture

The project is divided into three distinct components that run as independent processes and communicate in real-time:

### 1. Central (Server)
The core of the system. It acts as a server managing multiple client connections concurrently.
* **Admin CLI:** Allows an administrator to broadcast alerts (`alerta <msg> <duration> <id>`), cancel alerts, list active boards, or shutdown the platform.
* **Connection Management:** Uses **Overlapped Named Pipes** to handle up to 20 client connections simultaneously without blocking the main thread.
* **Data Broadcasting:** Writes the current state of all active alerts to a **Shared Memory** block so the Monitor GUI can read it.

### 2. Placar (Client)
A console-based client application representing a physical alert board.
* Connects to the Central via Named Pipes to receive alert.
* Uses **Waitable Timers** to track the exact duration of an alert. Once the timer expires, it automatically notifies the Central.
* Stores and retrieves the pipe connection string using the **Windows Registry** (`HKEY_CURRENT_USER\Software\TrabSO2`).

### 3. Monitor (GUI)
A visual dashboard built with the Win32 API to display all active alerts in real-time.
* Uses **Shared Memory (File Mapping)** to read the data provided by the Central.
* Implements a background listener thread that waits for an **Event** signal from the Central, ensuring the GUI only repaints when data actually changes (avoiding busy-waiting).
* Features pagination (Page Up/Page Down) and a configuration dialog box to adjust display limits.

## Core OS Concepts Applied

* **Inter-Process Communication (IPC):** 
  * `Named Pipes` for bidirectional communication between Central and Placares.
  * `Shared Memory (CreateFileMapping)` for fast data sharing between Central and Monitor.
* **Synchronization:** Avoidance of race conditions and deadlocks using `Mutexes` and `Critical Sections`.
* **Event-Driven Programming:** Heavy use of `CreateEvent` and `WaitForMultipleObjects` to handle asynchronous I/O operations and timer expirations.
* **Multithreading:** Separation of UI/CLI logic from background I/O listening tasks using `CreateThread`.

## How to Run

1. Compile the three projects (`Central`, `Placar`, and `Monitor`).
2. Run the **Central** executable first. You must provide a pipe name as an argument (e.g., `central.exe canal_alertas`).
3. Run one or more **Placar** instances (e.g., `placar.exe canal_alertas`). Type `ligar` to connect them to the Central.
4. Run the **Monitor** executable to visually track the active alerts.
5. In the Central console, use the `alerta` command to send messages to specific boards.
