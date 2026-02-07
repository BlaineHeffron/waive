#pragma once

namespace waive {
namespace Spacing {
    // ── General padding / gap scale ─────────────────────────────
    constexpr int xxs = 2;
    constexpr int xs  = 4;
    constexpr int sm  = 8;
    constexpr int md  = 12;
    constexpr int lg  = 16;
    constexpr int xl  = 24;

    // ── Standard UI dimension constants ─────────────────────────
    // Button / control heights
    constexpr int controlHeightSmall  = 20;   // solo/mute, tiny toggles
    constexpr int controlHeightMedium = 24;   // combo boxes, small buttons
    constexpr int controlHeightDefault = 28;  // standard buttons, rows
    constexpr int controlHeightLarge  = 32;   // tall buttons, input rows

    // Label / status heights
    constexpr int labelHeight = 18;           // name labels, status text
    constexpr int captionHeight = 14;         // small captions

    // Toolbar / header heights
    constexpr int toolbarRowHeight = 36;      // single toolbar row
    constexpr int menuBarHeight = 24;         // top menu bar

    // Fixed panel dimensions
    constexpr int rulerHeight = 30;           // timeline ruler
    constexpr int scrollbarThickness = 14;    // scrollbar size
    constexpr int resizerBarThickness = 4;    // drag-to-resize bars
    constexpr int trackHeaderWidth = 120;     // timeline track headers
    constexpr int mixerStripWidth = 80;       // mixer channel strip
    constexpr int sidebarWidth = 280;         // tool sidebar default
}
}
