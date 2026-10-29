# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""A headless Firefox, driven through its own remote protocol.

**Marionette rather than WebDriver**: Firefox speaks it natively, so the
comparison needs neither geckodriver nor Selenium, only the browser Fedora
already ships. The protocol is length-prefixed JSON over a socket; the few
commands used here are the ones geckodriver itself forwards.
"""

import json
import os
import shutil
import socket
import subprocess
import tempfile
import time


class MarionetteError(RuntimeError):
    pass


def free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class Firefox:
    """A private headless Firefox with a throwaway profile, and its session."""

    def __init__(self, binary: str = "firefox", timeout: float = 60.0):
        self._profile = tempfile.mkdtemp(prefix="rb-design-firefox-")
        port = free_port()
        prefs = {
            "marionette.port": port,
            # **One CSS pixel is one image pixel**, whatever the desktop's
            # scale, so a 132 px tile is 132 pixels in the screenshot.
            "layout.css.devPixelsPerPx": "1.0",
            "browser.shell.checkDefaultBrowser": False,
            "browser.startup.homepage_override.mstone": "ignore",
            "datareporting.policy.dataSubmissionEnabled": False,
            "toolkit.telemetry.reportingpolicy.firstRun": False,
            "browser.aboutwelcome.enabled": False,
        }
        with open(os.path.join(self._profile, "user.js"), "w", encoding="utf-8") as out:
            for key, value in prefs.items():
                out.write(f"user_pref({json.dumps(key)}, {json.dumps(value)});\n")
        self._process = subprocess.Popen(
            [binary, "--headless", "--marionette", "--no-remote", "--profile", self._profile, "about:blank"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self._socket = None
        deadline = time.monotonic() + timeout
        while self._socket is None:
            try:
                self._socket = socket.create_connection(("127.0.0.1", port), timeout=timeout)
            except OSError:
                if self._process.poll() is not None or time.monotonic() > deadline:
                    self.close()
                    raise MarionetteError("Firefox did not start its remote protocol")
                time.sleep(0.2)
        self._id = 0
        self._receive()  # the greeting
        self.call("WebDriver:NewSession", {"capabilities": {"alwaysMatch": {"acceptInsecureCerts": True}}})

    def _receive(self):
        prefix = b""
        while not prefix.endswith(b":"):
            chunk = self._socket.recv(1)
            if not chunk:
                raise MarionetteError("Firefox closed the connection")
            prefix += chunk
        length = int(prefix[:-1])
        data = b""
        while len(data) < length:
            chunk = self._socket.recv(length - len(data))
            if not chunk:
                raise MarionetteError("Firefox closed the connection")
            data += chunk
        return json.loads(data)

    def call(self, command: str, params: dict | None = None):
        self._id += 1
        message = json.dumps([0, self._id, command, params or {}]).encode()
        self._socket.sendall(str(len(message)).encode() + b":" + message)
        while True:
            reply = self._receive()
            if reply[0] == 1 and reply[1] == self._id:
                break
        if reply[2]:
            raise MarionetteError(f"{command}: {reply[2].get('error')}: {reply[2].get('message')}")
        return reply[3]

    def navigate(self, url: str):
        self.call("WebDriver:Navigate", {"url": url})

    def script(self, body: str, *args):
        """Runs \\a body as a function body and answers what it returns."""
        return self.call("WebDriver:ExecuteScript", {"script": body, "args": list(args)}).get("value")

    def async_script(self, body: str, *args, timeout_ms: int = 30000):
        """Runs \\a body with a callback as its last argument, and answers what it is called with."""
        self.call("WebDriver:SetTimeouts", {"script": timeout_ms})
        return self.call("WebDriver:ExecuteAsyncScript", {"script": body, "args": list(args)}).get("value")

    def set_viewport(self, width: int, height: int):
        """Sizes the window so that the page, not the window, is \\a width by \\a height."""
        self.call("WebDriver:SetWindowRect", {"width": width, "height": height})
        for _ in range(4):
            inner = self.script("return [window.innerWidth, window.innerHeight];")
            if inner == [width, height]:
                return
            rect = self.call("WebDriver:GetWindowRect")
            self.call(
                "WebDriver:SetWindowRect",
                {"width": rect["width"] + width - inner[0], "height": rect["height"] + height - inner[1]},
            )
        raise MarionetteError(f"could not make the page {width}×{height}; it is {inner}")

    def screenshot(self) -> bytes:
        """The page as it is showing, as PNG bytes."""
        import base64

        value = self.call("WebDriver:TakeScreenshot", {"full": False, "hash": False, "scroll": False})["value"]
        return base64.b64decode(value)

    def close(self):
        try:
            if self._socket is not None:
                try:
                    self.call("Marionette:Quit", {"flags": ["eForceQuit"]})
                except (MarionetteError, OSError):
                    pass
                self._socket.close()
        finally:
            try:
                self._process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait()
            shutil.rmtree(self._profile, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
