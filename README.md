# Raid Helper

A NosBoost mod that marks raid levers and mini bosses on the minimap.

## What it does

- Mini bosses get a mark on the minimap that follows them while they move.
- Levers get a mark too, but only the correct ones. The mod learns which lever is correct by watching the raid's lever counter. A lever that doesn't lower the counter is a trap and is never marked.
- What it learns is saved, so the next time you do the same room the levers show up. The first time you do a room, no levers are shown.
- F9 opens the settings: show levers, show bosses, pick the icon for each, and show a count on the minimap.

Learned levers are saved in `mods\RaidHelper\levers.txt`. Extra monsters to mark as bosses can be added in `mods\RaidHelper.ini` as `ExtraBossVnums=283,333`.

## Install

Download the DLL from this repo's Releases and put it in the game's `mods` folder. NosBoost loads it by itself. Open it from the NosBoost mods menu (F9).
