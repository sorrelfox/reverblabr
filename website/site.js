// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The website's only script, and every page works without it. It adds a
// Copy button to each command, and plays the demonstration clip while it is
// on the screen, unless the reader asked for less motion or to save data.

(function () {
    "use strict";

    // One place a screen reader hears what a button did.
    let status = null;
    function say(words) {
        if (!status) {
            status = document.createElement("p");
            status.className = "sr-only";
            status.setAttribute("role", "status");
            document.body.append(status);
        }
        status.textContent = "";
        window.setTimeout(function () {
            status.textContent = words;
        }, 50);
    }

    // **Copy buttons.** Only where the clipboard can be written, which a
    // page served over https always can.
    if (navigator.clipboard && window.isSecureContext) {
        document.querySelectorAll("pre.cmd").forEach(function (pre) {
            const wrap = document.createElement("div");
            wrap.className = "cmd-wrap";
            pre.replaceWith(wrap);
            wrap.append(pre);

            const button = document.createElement("button");
            button.type = "button";
            button.className = "copy";
            button.textContent = "Copy";
            const what = pre.getAttribute("data-what");
            button.setAttribute("aria-label", what ? "Copy: " + what : "Copy the command");
            let reset = 0;
            button.addEventListener("click", function () {
                const text = pre.textContent.replace(/\s+$/, "");
                navigator.clipboard.writeText(text).then(function () {
                    button.textContent = "Copied";
                    say("Copied to the clipboard");
                }, function () {
                    button.textContent = "Not copied";
                    say("The clipboard refused the text; select it and copy it by hand");
                });
                window.clearTimeout(reset);
                reset = window.setTimeout(function () {
                    button.textContent = "Copy";
                }, 2000);
            });
            wrap.append(button);
        });
    }

    // **The clip plays while it is in view**, from the moment it first
    // scrolls in, and pauses when it leaves. Not with reduced motion or
    // data saving: then it waits for its own play button. A pause the
    // reader made is kept.
    const calm = window.matchMedia("(prefers-reduced-motion: reduce)");
    const saving = navigator.connection && navigator.connection.saveData;
    if ("IntersectionObserver" in window && !saving) {
        document.querySelectorAll("video[data-play-in-view]").forEach(function (video) {
            let pausedByUs = false;
            let pausedByReader = false;
            video.addEventListener("pause", function () {
                if (!pausedByUs) {
                    pausedByReader = true;
                }
                pausedByUs = false;
            });
            video.addEventListener("play", function () {
                pausedByReader = false;
            });
            const watcher = new IntersectionObserver(function (entries) {
                entries.forEach(function (entry) {
                    if (entry.isIntersecting && entry.intersectionRatio >= 0.5) {
                        if (video.paused && !pausedByReader && !calm.matches) {
                            video.play().catch(function () {});
                        }
                    } else if (!video.paused) {
                        pausedByUs = true;
                        video.pause();
                    }
                });
            }, { threshold: [0, 0.5] });
            watcher.observe(video);
        });
    }
})();
