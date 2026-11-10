[한국어 변경 기록](CHANGELOG.ko.md)

# Changelog

## 0.1.25 — The Final Final Release

- L4D2 survivors keep their own portraits on maps that use the L4D1 survivor set, including mixed teams with characters from both games.
- If a player joins while a survivor bot is dead, the other extra HUD cards stay visible while the affected slot updates.

If you find a bug while playing, please include the steps to reproduce it, your HUD theme, and the number of survivors. [GitHub Issues](https://github.com/Hu-Myo/L4D2-Outbreak-Experience-HUD-Neko/issues)

## 0.1.23 — Final release

- After you die and spectate a teammate, that teammate stays eligible for an extra HUD card.
- Includes 0.1.22's improvement to how the **In-game HUD** theme prepares available downed and hanging styles, reducing cases where cards fall back to a simpler design.

This release wraps up planned development. If you find a bug while playing, please report it with steps to reproduce it. [GitHub Issues](https://github.com/Hu-Myo/L4D2-Outbreak-Experience-HUD-Neko/issues)

## 0.1.21 — Item icons and teammate card placement

- In the **In-game HUD** theme, missing item icons can appear once their artwork becomes available, including when the extra HUD replaces all teammate cards.
- Improved the size and placement of recovered item icons to better match the current HUD.
- Reduced overlap between teammate cards and the local player's HUD.

The build and automated checks passed. The tester confirmed in-game that the item icon size and position were corrected.

## 0.1.18 — L4D1 teammate replacement and Korean menus

### Added

- **Replace L4D1 team HUD**, enabled by default. While the game uses the L4D1 survivor set, the extra HUD displays all teammates in place of the existing teammate cards. Your own HUD stays visible. This works with either HUD theme.
- The option can be changed in **L4N Plugins → Extra Survivor HUD**. Turning it off restores the usual extra-card behavior; automatic replacement for nine or more survivors or repeated survivor characters still applies.
- Automatic Korean titles, option names, and ON/OFF labels when the game's language is Korean (`korean` or `koreana`). Other languages use English. The game's current language takes priority over its Steam language setting.

### Included improvements from 0.1.17

- More consistent downed, hanging, and dead appearances when teammate entries change or the current HUD briefly becomes unavailable.
- Better retention of item icons and state artwork during brief display interruptions.
- Reduced repeated HUD work to ease processing overhead and help with stuttering.
- Fixed duplicate diagnostic log entries with incorrect elapsed times.

### Testing status

The build and automated checks passed. The tester confirmed that missing icons were resolved with 0.1.17. The new 0.1.18 L4D1 replacement option and automatic Korean menu still need confirmation in live server play. No particular FPS improvement is guaranteed.

### Updating

Close Left 4 Dead 2 completely before replacing `bin/neko/plugins/NekoExtraHUD.dll`. Keep your existing `left4dead2/neko/extra_hud.ini`. If `replace_l4d1_team` is absent, the new option defaults to ON and is saved when the plugin starts. Set `replace_l4d1_team=0` to disable that option.

## 0.1.17 — HUD continuity and processing improvements

- Reduced cases where the In-game HUD theme temporarily fell back to a simpler appearance or lost downed/dead artwork as teammate cards changed.
- Improved recovery from brief missing or transparent HUD elements, including item icons.
- Reduced repeated processing during HUD drawing and fixed duplicate diagnostic timing entries.
- The tester subsequently confirmed that missing icons were resolved. FPS and behavior across other HUDs and server configurations remain subject to testing.

## 0.1.16 — L4D1 survivor portraits

- Corrected L4D1 survivor portraits and downed artwork in the extra teammate HUD.
- Corrected repeated-survivor-character detection for L4D1 survivor configurations.
- Preserved the existing themes, menu options, and settings.
- Normal and downed L4D1 displays were confirmed in-game by the tester.

[Installation, options, and reference images](README.md)
