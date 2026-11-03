[한국어 안내 바로가기](README.ko.md)

# L4D2 Outbreak Experience HUD (Built with Neko)

**[Download now — 0.1.15 Public Test ZIP](https://github.com/Hu-Myo/L4D2-Outbreak-Experience-HUD-Neko/releases/download/v0.1.15-test/L4D2-Outbreak-Experience-HUD-Neko_0.1.15-public-test.zip)**

Extra teammate HUD cards for Left 4 Dead 2 with Neko. Public test version: **0.1.15**.

Requires a supported Neko installation for the Windows version of Left 4 Dead 2. The in-game menu and DLL use the name **NekoExtraHUD / Extra Survivor HUD**.

> **Account and server restriction notice:** This DLL modifies the game at runtime. The author cannot guarantee that using it will not result in game/server restrictions or account bans. To the extent permitted by law, the author accepts no responsibility for such penalties. Use it only if you accept these risks of restrictions or bans.

## Installation

### 1. Set up Neko

Following the [NekoShaders & L4N author's Workshop instructions](https://steamcommunity.com/sharedfiles/filedetails/?id=3558049615):

1. Subscribe, start L4D2, let the Workshop download finish at the main menu, then close the game completely.
2. In the game folder, open `left4dead2/addons/workshop` and change `3558049615.vpk` to `3558049615.zip`. Enable file extensions in Explorer if they are hidden.
3. Unpack it beside `left4dead2.exe`, replacing files as directed by the author. See the included `readme_l4n.txt` for details and updates.

This HUD's 0.1.15 test build targets **Neko 2.51.0**. Other Neko or game versions may not show the extra HUD.

### 2. Add this HUD

1. Get a packaged test build from [Releases](https://github.com/Hu-Myo/L4D2-Outbreak-Experience-HUD-Neko/releases) when one is available. GitHub's **Source code** archives are not ready-to-install builds.
2. With the game fully closed, place `NekoExtraHUD.dll` at:

   ```text
   <Left 4 Dead 2>/bin/neko/plugins/NekoExtraHUD.dll
   ```

3. If the package includes an example `extra_hud.ini`, new users can put it at `<Left 4 Dead 2>/left4dead2/neko/extra_hud.ini`. Keep your existing file when updating so your settings are preserved.
4. Start the game and load a map. Enter `l4n_menu` in the console, open **Extra Survivor HUD**, and set **Extension: ON**. To preview it in a normal four-survivor game, also enable **Single-player test**.

## What you see in a game

When you join a game with more teammates than your usual HUD can show, additional teammate cards appear from the **lower-left corner**. They continue across the available space and form more rows upward when needed. Their positions adjust to the card size and the space occupied by your existing HUD.

If your custom HUD shows three teammates, the extra cards cover the teammates it leaves out. If it shows four, those four do not need an additional copy. Your own player HUD remains visible.

Each extra card shows the teammate's name, portrait, health, and status. The health number includes temporary health. Downed and hanging teammates show their remaining health; dead teammates have a death display. The exact artwork and arrangement depend on the selected theme and your HUD.

With **nine or more survivors**, or **repeated survivor characters**, the plugin can show the complete teammate HUD in place of the usual teammate cards. Matching player nicknames alone do not activate this behavior.

In a standard four-survivor game, seeing **no extra cards is normal**. Use **Single-player test** to preview the extra HUD with your three existing teammates.

## Options in the game

After a map has loaded, open the console, enter `l4n_menu`, and select **Extra Survivor HUD**. Menu toggles are saved automatically.

| Option | Default | What it does |
| --- | --- | --- |
| **Extension: ON / OFF** | ON | Shows or hides NekoExtraHUD. The usual teammate HUD follows your Neko settings. |
| **8th+ teammate cards: ON / OFF** | OFF | ON allows teammate cards beyond the first seven teammates, excluding you, when there is room. OFF normally limits the combined teammate HUD to seven teammates. This limit does not apply while the complete teammate HUD is being replaced. |
| **Theme: Current design / In-game HUD** | Current design | **Current design** uses the plugin's simple card appearance. **In-game HUD** follows the HUD appearance currently used in your game, including available portraits, health bars, item icons, and downed/death artwork. Some custom HUD elements may look different. |
| **Single-player test: ON / OFF** | OFF | In a standard four-survivor game with three visible teammate cards, shows an extra copy of those three teammates for testing. It does not add players or bots. Turn it OFF for normal play. |
| **Diagnostic logging: ON / OFF** | ON | Enables or disables detailed diagnostic logging. OFF does not remove basic startup or error messages. It does not change the HUD's appearance. |
| **Reload extra_hud.ini** | Action | Applies settings you have saved in `extra_hud.ini`. |
| **Write diagnostic snapshot** | Action | Writes a diagnostic report to `extra_hud.log`. It does not take a screenshot or change the display. |

### Portraits and item icons

With **In-game HUD**, available item icons include healing items, pills or adrenaline, and throwables. Their appearance depends on your active HUD and the teammate's inventory.

If a teammate is using a Neko model with suitable portrait artwork, the extra card can use that artwork automatically. This follows your existing Neko model selections; there is no separate portrait switch. If suitable artwork is unavailable, the survivor portrait is used.

## Size and position settings

These settings are in `left4dead2/neko/extra_hud.ini`, under `[hud]`. Save your changes, then select **Reload extra_hud.ini** in the menu.

| Setting | Default | What it changes |
| --- | --- | --- |
| `scale_percent` | `85` | Extra card size as a percentage of the detected teammate card size. Effective range: 50–120%. |
| `gap` | `6` | Horizontal spacing between extra cards, up to 40 pixels. In-game HUD may follow your current HUD's spacing instead. |
| `row_gap` | `8` | Space between rows, up to 60 pixels. |
| `offset_x` | `0` | Moves the starting position to the right. Negative values do not move it left of the starting edge. |
| `offset_y` | `0` | Negative values move the starting position upward. Placement stays within the screen. |

The extra HUD starts at the lower left even if your custom teammate HUD is located elsewhere. The number of cards that fit depends on your resolution, HUD size, and available space.

## Public test notes

- Check both normal play and downed, hanging, and dead states with your HUD.
- Some L4D1 survivor configurations may show mismatched survivor portraits or downed artwork, or may fail to switch to the complete teammate display for certain repeated characters.
- If something looks wrong, describe your HUD/theme, survivor count, and the state in which it happened. Check a log before sharing it publicly.

Redistribution terms: [Korean license](LICENSE) · [English translation](LICENSE.en.txt).
