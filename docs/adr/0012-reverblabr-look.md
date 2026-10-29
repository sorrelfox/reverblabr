<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0012: The Reverblabr look: our own control style and title bar

- **Status:** Accepted
- **Date:** 2026-09-23

## Context

The interface was designed as an interactive prototype before any of it was built: an HTML page that draws the window, the session tabs, the library, the canvas, every block's tile and every block's settings panel, in a dark and a light version. It sets its own values for all of it: the colours as CSS variables in two palettes, Noto Sans for text and JetBrains Mono for numbers, font sizes from 9.5 to 17 px, corner radii of 3, 4, 6 and 8 px, a 40 px header, a 30 px status bar, and controls 24, 26 or 28 px high. Its top row is not under a title bar; it *is* the title bar. The tabs, the running status, the elapsed time and Run/Stop share the row with a theme button and the minimise, maximise and close buttons. Double-clicking the row maximises the window.

What was built follows the prototype's layout, but is drawn by the desktop, because the project began with the opposite rule ([ADR-0001](0001-cpp-qt-kirigami-cmake.md), [ADR-0004](0004-kde-plasma-wayland-platforms.md)):

- **The controls are Breeze's** (`org.kde.desktop`), so a button, a slider or a tab is drawn by the widget style with its own sizes, radii, focus ring and fonts.
- **Colours are roles.** `AppColors` maps each of the prototype's names onto a `Kirigami.Theme` role, so the window takes the user's colour scheme and accent colour. Only the "signal flowing" orange and the category tints are constants.
- **KWin draws the title bar**, and the prototype's header became a toolbar row under it.

The result is recognisably the design, and nowhere exactly it: the type is the desktop's font at the desktop's size, the controls are Breeze's shapes, the palette is the user's scheme, and there is a second bar above the header. Matching the prototype exactly, in the same styles, sizes, colours, window decoration and positions, cannot be done by adjusting values inside that rule. Breeze decides how a control looks, and a colour scheme has no roles for fonts, radii or row heights. The rule has to change.

## Decision

**The application has two appearances, chosen in its settings. The default draws the prototype exactly; the other keeps today's desktop look.**

- **Reverblabr dark** and **Reverblabr light** are the prototype's two palettes. Dark is the default, as it is in the prototype.
- **Follow system** is today's behaviour, unchanged: Breeze controls, the user's colour scheme, accent and font, and KWin's decoration.

In the two Reverblabr appearances:

- **Our own Qt Quick Controls style, "Reverblabr".** It is a QML style module built on Qt's Basic style, and it draws each control as the prototype's CSS does. That covers every state: normal, hover, pressed, checked, focus and disabled. The style is chosen with `QQuickStyle::setStyle()` before the QML engine loads, so switching between a Reverblabr appearance and *Follow system* takes effect at the next start. Switching dark and light is immediate.
- **The prototype's values, fixed.** They are its two palettes, its fonts and its pixel sizes:
  - Noto Sans (400, 500, 600, 700) and JetBrains Mono (500, 600) are **bundled** with the application and loaded with `QFontDatabase`, since both are under the SIL Open Font License. The window's font no longer depends on what is installed.
  - Sizes are in logical pixels, so they scale with the display scale like everything else, but not with the desktop's font size.
- **Our own title bar.** The window is frameless (`Qt.FramelessWindowHint`), and its top 40 px row is the prototype's header. We do everything a decoration does:
  - moving and resizing through `startSystemMove()` and `startSystemResize()`, so the compositor still does the snapping and tiling;
  - maximise on a double-click;
  - the window menu on a right-click;
  - the 8 px corners;
  - a shadow drawn by the compositor through `KWindowShadow`.

  A maximised window has neither corners nor a shadow, as in the prototype.

**The two modes share one set of tokens, so neither is a fork.** Every colour, font, size, radius and gap the QML uses has one name, in `AppColors`, `AppFonts` or `AppMetrics`:

- In a Reverblabr appearance, a name holds the prototype's value.
- In *Follow system*, the same name maps to a `Kirigami.Theme` role or a `Kirigami.Units` measure, as `AppColors` maps colours today.

Nothing in `app/qml` names a colour or a pixel size of its own. The style reads the same names. The title bar and the toolbar row are two arrangements of the same controls, one for each mode.

**Three things depart from the prototype on purpose.** Two are values, because the prototype's value cannot be read:

- **The label on the live orange stays near-black.** The prototype writes white on it, which is about 2:1 on either orange; ordinary text needs 4.5:1.
- **On a light canvas the category tints keep the lightness that clears 3:1.** Three of the prototype's six hues (sources, outputs, meters) fall to about 2:1 on its light canvas, where an icon needs 3:1 to be told apart. The dark appearance uses the prototype's six unchanged.

The third is a pair of glyphs: **maximise is a chevron pointing up and restore a diamond**, as on every other window on the desktop. The prototype has the two the other way round, and people read a window's buttons by the desktop's convention, not by this window's.

**How close is "exactly" is measured, not judged by eye**. The prototype is rendered by a browser, and the app by `grabToImage`. Both are rendered in the same named states at the same size, and each state gets a score: the share of pixels that differ beyond a small colour tolerance. What cannot be pixel-identical is judged against that tolerance, and masked where it legitimately differs:

- **Text is set by Qt, not by a browser.** Both shape with HarfBuzz, but glyph rasterisation, hinting, sub-pixel positioning and line height differ. Text is compared by position and extent, not pixel for pixel.
- **Fractional display scaling.** At 125 % or 150 % a 1 px line or a 3 px radius falls between device pixels, and Qt and a browser round it differently. The comparison runs at a device pixel ratio of 1, with a check at 2. At fractional ratios the look is checked by eye.
- **Translations longer than the English** widen tabs, buttons and labels, and move what comes after them. The comparison is made in English. In other languages the rule is that nothing overlaps and nothing is cut off, not that the positions hold.

The following also cannot match the prototype pixel for pixel, and are masked or held to the tolerance:

- a live meter's level;
- the clock;
- the spinner;
- the shadow, which the compositor draws rather than CSS.

**This supersedes:**

- In [the architecture](../architecture.md), "Colours are roles, not values" and "KWin draws the title bar". Both now hold for *Follow system* only, and point here.
- In [ADR-0004](0004-kde-plasma-wayland-platforms.md), "windows use KWin's server-side decorations".
- In [ADR-0001](0001-cpp-qt-kirigami-cmake.md), "the native Breeze controls", which are now what *Follow system* uses.

Kirigami stays the application's shell in both modes. Everything else in those records stands.

## Consequences

- **The default is *Reverblabr dark*, now that the style draws the menus too.** While the control style drew only the window's controls, the default stayed *Follow system*: a menu or a tooltip drawn with the platform's palette can take the desktop's palette for menus and tooltips, and a dark window on a light desktop would have opened light menus. The style's last step draws every menu, tooltip, popup and dialog from the tokens, gives Kirigami the design's colours and corner, and gives every label the window's type, so nothing in the window is left to the desktop.
- **What the Reverblabr appearances give up:**
  - **Colour scheme and accent:** the user's scheme and accent colour no longer reach the window.
  - **Type:** the desktop's font and font size no longer reach it either. Text is 12.5 px Noto Sans whatever the desktop is set to, and a larger font needs the display scale or *Follow system*.
  - **Title bar:**
    - the decoration theme;
    - the button layout the user chose, including buttons on the left;
    - any extra title-bar buttons.

  **Why that is acceptable here:**
  - This is a studio tool whose window is mostly a canvas the application draws itself. Its identity (the live orange, the category tints, the type set for reading numbers) was already its own.
  - What the desktop contributed was mostly the chrome around that canvas, and the chrome is what made the window not look like the design.
  - Window management stays with the compositor: move, resize, snap, tile, rules, keyboard shortcuts and the window menu. Only the decoration's drawing moves to us.
  - Everyone for whom that is the wrong trade has *Follow system*.
- **Where the prototype cannot be read, the Reverblabr appearance departs from it**, as it already does for the writing on the live colour. WCAG AA is the bar: 4.5:1 for words, 3:1 for a mark that has to be seen to be understood.
  - **The muted grey** moves toward the text colour just far enough to read at 4.5:1 on every surface: a step in the dark palette, #6b7378 to #5e6569 in the light one.
  - **The label on the accent** is near-black: white on either blue is 2.5:1 and 3.3:1.
  - **Words in a status colour** (a readout in the accent, a warning, an error) use `accentText`, `warningText` and the like, and **marks** (a state dot, a live or idle wire, a meter's bar, a focus ring) use `accentMark`, `liveMark` and the like. Each is its fill moved toward the text colour just far enough to clear its ratio, and is the fill itself where that already does: in the dark palette most are unchanged, in the light one the live orange, the ok green and the warning orange darken for marks, and more for words. The fills themselves (the Run button, the category tints) stay the prototype's.
  - `tst_contrast.qml` measures every one of them, and `tools.readable-colours` keeps a label from being given a fill again.
  - **None of this applies to *Follow system*,** whose colours stay the scheme's, per the next point, with one change of role.
  - **Secondary text in *Follow system* is the scheme's text colour, not its disabled-text colour**. It is the text colour taken toward the scheme's background only as far as it still reads at 4.5:1 on every surface: #575c61 under Breeze Light, where the disabled-text colour read at about 2.4:1, and #a0a6ab under Breeze Dark. Every colour still comes from the scheme; a hint or a unit is not a disabled control, so the disabled role was the wrong one for it.
- **Follow system is a supported mode, not a fallback.** It is what users of large fonts, high contrast or another theme need, and it must stay exactly what the application is today. Every change to the Reverblabr look keeps it unchanged, and the QML suite runs under both styles. A control that works in only one of them is a bug.
- **An upgrade changes the look.** Existing users open the application in *Reverblabr dark*, not in their scheme. The setting is one click away, and *Follow system* brings back what they had.
- **We now maintain a control style**, one QML file per control the application uses. It is built in three steps: buttons and choices; sliders, lists and text fields; then menus, tooltips, dialogs and messages. Kirigami's own components read our palette through `Kirigami.Theme`, so they do not need their own copies.
- **We now maintain window chrome.** That covers:
  - hit-testing the header's empty space for moving;
  - resize grips on every edge and corner, with the right cursors;
  - the window menu, and keyboard access to the header's buttons;
  - announcing the window buttons to accessibility as window buttons.

  With our own title bar the menus also have to go somewhere: the prototype defines File, Edit and Session menus but draws no menu bar. **Decided: a menu button in the header**, KDE's pattern for a window whose menu bar is hidden, holding every menu; Ctrl+M (Show Menubar) brings the classic bar back, under the title bar; every shortcut works either way. The window menu is the compositor's own, asked for through Qt's Wayland client internals (Qt has no public call for it, and only the compositor can keep a window above the others or move it to another desktop), with a smaller menu of ours where it cannot be asked for. On Plasma, Alt+Space belongs to KRunner, and KWin's own Alt+F3 opens the window menu on this window as on any other.
- **Fonts become part of the package.** Two font families are added to `LICENSES/` (OFL-1.1) and to REUSE, and are loaded at start in the Reverblabr appearances only.
- **Icons stay the icon theme's** for now. The prototype draws its own line icons. Where the comparison shows one differing enough to matter, it is dealt with along with the sizes and positions rather than by shipping a second icon set up front.
- **"Matches the design" becomes a number.** Every step toward the design (the control style, the sizes and positions, the title bar) quotes the comparison's score before and after it. Once the style is in, a regression beyond a threshold fails the comparison's test.

## Alternatives considered

- **Keep the native look and tune the values.** This is where we were. `KColorSchemeManager` can give the application a colour scheme of its own without touching the desktop's, which would fix the palette. But Breeze still draws the controls, and a scheme has no say over fonts, radii, row heights or the title bar. It would get closer, and never exact.
- **Only the Reverblabr look, with no Follow system.** It is less to keep working. But it would take away the one answer the application has for high contrast, large fonts and people who want their desktop's theme everywhere, and those are not preferences we should overrule.
- **A decoration of our own inside KWin** (an Aurorae theme or a decoration plugin) instead of a frameless window. It can colour and shape a title bar, but it cannot hold the session tabs, the clock and Run/Stop, which are the application's own controls. It would also change every window on the desktop, or need a window rule the user has to install.
- **Client-side decorations from Qt's Wayland plugin.** Those draw a generic title bar, not ours, and the same objection about the header's contents applies.
- **A `QStyle` instead of a Qt Quick Controls style.** The Breeze controls reach QML through `qqc2-desktop-style`, which paints through a `QStyle`. Writing a `QStyle` would keep that route, but it paints widgets in C++ from style options. That is further from the prototype's boxes, borders and radii than QML items that read the same tokens as the rest of the window.
- **Showing the prototype itself in a web view.** It is the prototype and not the application: there is no engine behind it, and it would bring a browser engine into a real-time audio application's process for the sake of its chrome.
