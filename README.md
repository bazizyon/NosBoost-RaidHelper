# Raid Helper

NosBoost mod that marks raid levers and mini bosses on the minimap.

- Levers are the objects with vnum 1135 (1136 once pulled). Only levers it has learned as correct are marked: a lever is correct when the raid's lever counter (`raidmbf` 4th field, levers left) drops right before the lever flips to 1136. A flip without a drop is a trap. Learned levers are saved in `mods\RaidHelper\levers.txt` as `width height x y good bad`, keyed by the room's grid size because raid map ids change every run. The first run of a room shows no levers.
- Mini bosses are the monsters whose `in` packet ends with the minimap mark flag (`1`). Extra monster vnums can be listed in `mods\RaidHelper.ini` as `ExtraBossVnums=283,333`.
- Marks follow `mv`, and are removed on death (`su`), on `out` and on map change.
- F9 opens the settings: show levers, show bosses, the icon for each, and a count on the minimap.

Needs a NosBoost runtime with the `mv` packet, the raw `in` fields and the map size in `TickContext`.
