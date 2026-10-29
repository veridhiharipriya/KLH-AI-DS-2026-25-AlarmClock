#!/usr/bin/env python3
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from datetime import datetime
from zoneinfo import ZoneInfo


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(url, method="GET", payload=None):
    body = None if payload is None else json.dumps(payload).encode()
    headers = {"Content-Type": "application/json"} if body else {}
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=3) as response:
        return response.status, response.read().decode()


def start_server(executable, port, env):
    return subprocess.Popen(
        [executable, str(port)],
        cwd=os.path.dirname(os.path.dirname(executable)),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )


def main():
    executable = os.path.abspath(sys.argv[1])
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    with tempfile.TemporaryDirectory(prefix="chronos-test-") as data_dir:
        env = os.environ.copy()
        env["ALARM_DATA_DIR"] = data_dir
        process = start_server(executable, port, env)
        try:
            for _ in range(50):
                if process.poll() is not None:
                    raise RuntimeError(process.stderr.read().decode())
                try:
                    status, page = request(base + "/")
                    if status == 200 and "Chronos" in page:
                        break
                except (urllib.error.URLError, TimeoutError):
                    time.sleep(0.1)
            else:
                raise RuntimeError("Server did not become ready")

            assert "function refreshState" in request(base + "/app.js")[1]
            app_script = request(base + "/app.js")[1]
            assert "function playAlarmPulse" in app_script
            assert "setInterval(() =>" in app_script
            assert "function stopRinging" in app_script
            assert "function snoozeRingingAlarms" in app_script
            assert "oscillator.stop(context.currentTime + 0.52)" in app_script
            assert "$('#ring-stop').addEventListener" in app_script
            assert "deleteHistoryEntry" in app_script
            assert "--accent" in request(base + "/style.css")[1]
            page = request(base + "/")[1]
            assert 'id="world-map"' in page
            assert 'id="ring-stop"' in page and 'id="ring-snooze"' in page
            assert 'class="repeat-days"' in page
            assert 'value="0"><span>Sun</span>' in page
            assert 'id="history-list"' in page
            assert 'id="system-stats"' not in page
            assert "CHRONOS CLOCK" not in page
            status, state_body = request(base + "/api/state")
            state = json.loads(state_body)
            assert status == 200 and not state["alarms"]
            assert "system" not in state

            status, alarm_body = request(
                base + "/api/alarms",
                "POST",
                {"label": "Smoke test", "when": int(time.time()) + 120},
            )
            alarm = json.loads(alarm_body)
            assert status == 201 and alarm["label"] == "Smoke test"

            status, toggle_body = request(
                base + f"/api/alarms/{alarm['id']}/toggle", "POST"
            )
            assert status == 200 and json.loads(toggle_body)["enabled"] is False
            try:
                request(base + f"/api/alarms/{alarm['id']}/snooze", "POST")
            except urllib.error.HTTPError as error:
                assert error.code == 409
            else:
                raise AssertionError("A pending alarm must not be snoozable")

            status, _ = request(
                base + f"/api/alarms/{alarm['id']}", "DELETE"
            )
            assert status == 200

            timezone = "Asia/Kolkata"
            recurring_when = int(time.time()) + 3
            local_weekday = datetime.fromtimestamp(
                recurring_when, ZoneInfo(timezone)
            ).weekday()
            sunday_based_day = (local_weekday + 1) % 7
            recurring_days = 1 << sunday_based_day
            status, recurring_body = request(
                base + "/api/alarms",
                "POST",
                {
                    "label": "Weekly test",
                    "when": recurring_when,
                    "days": recurring_days,
                    "timezone": timezone,
                },
            )
            recurring_alarm = json.loads(recurring_body)
            assert status == 201 and recurring_alarm["repeatDays"] == recurring_days
            assert "repeatDays" in request(base + "/api/state")[1]
            for _ in range(40):
                _, state_body = request(base + "/api/state")
                state = json.loads(state_body)
                recurring_state = next(
                    item
                    for item in state["alarms"]
                    if item["id"] == recurring_alarm["id"]
                )
                if any(item["id"] == recurring_alarm["id"] for item in state["history"]):
                    break
                time.sleep(0.2)
            else:
                raise AssertionError("Recurring alarm did not fire")
            assert recurring_state["enabled"]
            assert recurring_state["repeatDays"] == recurring_days
            assert recurring_state["when"] > int(time.time()) + 6 * 24 * 60 * 60
            status, recurring_snooze_body = request(
                base + f"/api/alarms/{recurring_alarm['id']}/snooze", "POST"
            )
            recurring_snooze = json.loads(recurring_snooze_body)
            assert status == 200 and recurring_snooze["repeatDays"] == recurring_days
            assert recurring_snooze["when"] >= int(time.time()) + 295

            status, firing_body = request(
                base + "/api/alarms",
                "POST",
                {"label": "Firing test", "when": int(time.time()) + 2},
            )
            firing_alarm = json.loads(firing_body)
            assert status == 201
            for _ in range(40):
                _, state_body = request(base + "/api/state")
                state = json.loads(state_body)
                if any(item["id"] == firing_alarm["id"] for item in state["history"]):
                    break
                time.sleep(0.2)
            else:
                raise AssertionError("Forked scheduler did not trigger the alarm")
            fired = next(
                item for item in state["alarms"] if item["id"] == firing_alarm["id"]
            )
            assert not fired["enabled"]
            assert any(item["id"] == firing_alarm["id"] for item in state["history"])

            status, state_body = request(base + "/api/state")
            state = json.loads(state_body)
            assert len(state["alarms"]) == 2
            status, snooze_body = request(
                base + f"/api/alarms/{firing_alarm['id']}/snooze", "POST"
            )
            snoozed = json.loads(snooze_body)
            assert status == 200 and snoozed["enabled"] is True
            assert snoozed["when"] >= int(time.time()) + 295
            process.terminate()
            process.wait(timeout=5)
            process = start_server(executable, port, env)
            for _ in range(50):
                if process.poll() is not None:
                    raise RuntimeError(process.stderr.read().decode())
                try:
                    _, state_body = request(base + "/api/state")
                    state = json.loads(state_body)
                    break
                except (urllib.error.URLError, TimeoutError):
                    time.sleep(0.1)
            else:
                raise RuntimeError("Restarted server did not become ready")
            assert any(item["id"] == firing_alarm["id"] for item in state["history"])
            restored = next(
                item for item in state["alarms"] if item["id"] == firing_alarm["id"]
            )
            assert restored["enabled"]
            assert restored["when"] == snoozed["when"]
            recurring_restored = next(
                item
                for item in state["alarms"]
                if item["id"] == recurring_alarm["id"]
            )
            assert recurring_restored["enabled"]
            assert recurring_restored["repeatDays"] == recurring_days
            assert recurring_restored["when"] > int(time.time()) + 6 * 24 * 60 * 60
            assert recurring_restored["snoozeUntil"] >= int(time.time()) + 295
            status, _ = request(
                base + f"/api/history/{firing_alarm['id']}/{firing_alarm['when']}",
                "DELETE",
            )
            assert status == 200
            _, state_body = request(base + "/api/state")
            state = json.loads(state_body)
            assert not any(
                item["id"] == firing_alarm["id"]
                and item["when"] == firing_alarm["when"]
                for item in state["history"]
            )
            try:
                request(
                    base + f"/api/history/{firing_alarm['id']}/{firing_alarm['when']}",
                    "DELETE",
                )
            except urllib.error.HTTPError as error:
                assert error.code == 404
            else:
                raise AssertionError("Deleting a missing history entry should return 404")
            print("Smoke test passed: UI, map, alarm firing/snooze, restart persistence, and API.")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
