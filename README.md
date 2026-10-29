# Chronos — Ubuntu Alarm Clock

A local web dashboard backed by a native C/POSIX service. The alarm scheduler is a separate child process created with `fork()`, uses `SIGALRM` for its one-second clock tick, and shares alarm state with the HTTP parent process. Open the dashboard in a browser while the Ubuntu service runs.

The browser dashboard uses a soft pink theme.

## Run on Ubuntu

Install the small set of build dependencies:

```bash
sudo apt update
sudo apt install build-essential python3
```

From the project directory:

```bash
make
make run
```

Then open **http://localhost:8080** in a browser. Stop the service with **Ctrl+C**. You can choose another port with `./build/chronos 8090`.

`make test` builds the project and runs a local HTTP smoke test. Alarm definitions and firing history are stored in `data/alarms.tsv` and `data/history.tsv`; delete a fired entry from the dashboard's **Alarm history** list using its × button. To make an alarm repeat, select one or more weekdays in the **New alarm** dialog; leave them unchecked for a one-time alarm. Runtime files are intentionally excluded from version control. Set `ALARM_DATA_DIR=/path/to/data` to store them elsewhere.

## Open this folder in VS Code connected to Ubuntu

### Windows + WSL 2

1. Install a WSL 2 Ubuntu distribution and the **WSL** extension in Windows VS Code.
2. Open Ubuntu from the Start menu. In Ubuntu, go to the project folder and open it with VS Code:

   ```bash
   cd /mnt/c/Users/HP/OneDrive/Desktop/Os
   code .
   ```

   For best Linux file-system performance, you can instead copy/clone the project under your Ubuntu home directory (for example, `~/projects/chronos`) and run `code .` there.
3. Confirm VS Code's lower-left remote indicator says **WSL: Ubuntu**. Open a VS Code terminal; it should be an Ubuntu Bash terminal. Install the dependencies above in that terminal. If prompted to install the recommended C/C++ extension **in WSL**, accept it so VS Code can understand Linux system headers and enable F5 debugging.
4. To run directly from VS Code, select **Run and Debug** in the left sidebar, choose **Chronos: Run in Ubuntu** in the dropdown, then click the green start button (or press **F5**). This builds and starts the server. Open `http://localhost:8080` in your browser; WSL forwards localhost ports automatically. Stop it with the red stop button in VS Code. After changing the C service, stop and start this VS Code configuration again so it runs the newly built server.

   You can also choose **Terminal → Run Task → Chronos: run** to run without the debugger. C/C++ IntelliSense is configured to use Ubuntu GCC while the folder is open in the WSL window.

### Native Ubuntu

Open this directory in VS Code running on Ubuntu, install `build-essential` and `python3`, then use the same build and run commands. The workspace tasks are in `.vscode/tasks.json`.

## Project phases

| Phase | Implemented |
| --- | --- |
| 1. Project structure | `src/`, `public/`, `data/`, `scripts/`, `tests/`, and `.vscode/` |
| 2. Basic alarm + system time | Local clock display and native Unix-time alarm scheduling |
| 3. `fork()` + parent/child processes | HTTP parent and independent scheduler child with shared memory |
| 4. Signals + alarm triggering | Child uses `SIGALRM`; parent handles shutdown signals and alarm event notifications |
| 5. Multiple alarms | Manage up to 64 persisted one-time or weekday-repeating alarms; enable, pause, snooze, and delete them |
| 6. Alarm storage/history | TSV alarm storage and firing history under `data/` |
| 7. Sound + notifications | Repeating browser alarm tone with stop/snooze controls and optional desktop notifications when the dashboard is open |
| 8. Timer | Browser timer with start, pause, resume, reset, and completion alert |
| 9. Stopwatch | Start, pause, resume, lap, and reset |
| 10. World Clock + time zones | Live city clocks, clickable map markers, and search across browser-supported IANA time zones |
| 11. Browser UI | Responsive dashboard served by the C service |
| 12. C ↔ Web communication | Same-origin JSON HTTP API from the C service to the browser |
| 13. OS monitoring/dashboard | Omitted from the user interface |
| 14. Testing + polish | Build/run/test tasks, smoke test, responsive layout, and this guide |

## Notes

- Alarms are triggered by the Ubuntu scheduler even if the browser tab is closed, but the machine must remain awake and the service must remain running. Browser sound and desktop notifications require the dashboard to be open.
- When the dashboard is open, alarm and timer audio repeats until you click **Stop sound**; clock alarms can also be silenced and rescheduled with **Snooze 5 min**. Desktop notifications are optional.
- The timer and stopwatch run in the browser; alarms are handled by the native Ubuntu service. The world map has selectable city markers, and the search suggestions cover time zones provided by the browser (not every town or neighborhood).
- Alarm repeats use the weekday selection and the browser's local time zone; leave every day unchecked for a one-time alarm.
- The HTTP service listens only on `127.0.0.1` and is intended for local development, not public deployment.
