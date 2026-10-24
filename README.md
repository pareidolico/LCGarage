# LCGarage

A Mafia II style garage for GTA IV / EFLC. Drive up to a garage door and honk: the car goes in, with a plate of your choice. Walk up to the door later and take any of your cars out, repaired and clean, plate and all. Lose or wreck a car you took out, and it's back in the garage.

It's a small `.asi` plugin. It's made for Complete Edition 1.2.0.59 with [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) and plates from **[LibertyCityPlates](https://github.com/ImpossibleEchoes/LibertyCityPlates)**, and works alongside [PlateChanger](https://github.com/pareidolico/lcplates-changer). The door is set up for the Lost clubhouse in Acter (TLAD), and you can add others.

## Install

1. Download the zip from the [latest release](../../releases/latest) (or the newest build under **Actions**).
2. Put `LCGarage.asi` and `LCGarage.ini` in your game folder, next to `LibertyCityPlates.asi`.

You need an ASI loader (FusionFix has one). Without LibertyCityPlates the garage still works, it just can't set plates.

## Storing a car

Stop at the door facing it, and honk. The door doesn't open; the camera shows the car and the screen fades.

A car that's new to the garage gets its plate first. The camera shows the back of the car, and every change shows on the plate straight away:

| Key | Does |
|---|---|
| `A`–`Z`, `0`–`9`, Space | Plate text, up to 12 characters (numpad digits work too, numpad `*` is the state emblem) |
| Backspace | Delete the last character |
| Page Up / Page Down | Plate picture, with its text size, position, colour and font from your `vehicles.ide` |
| Home / End | Font |
| Enter | Store the car |
| Delete | Cancel |

**Leave the text blank** and the car gets a random plate made from your `vehicles.ide`, picked by the same rules LibertyCityPlates uses: `used_plates` for the model, then each plate's chance, parent and zones, at the garage's position. The prompt shows that plate before you press Enter.

From then on the plate is the car's: a car that came out of the garage goes back in with the plate it has, without asking.

## Taking a car out

On foot near the door, press **F6**:

| Key | Does |
|---|---|
| Up / Down (or Page Up / Page Down) | Choose a car |
| Enter | Take it out |
| Delete, then Delete again | Remove it from the garage for good |
| Backspace or F6 | Close |

The car comes out at the door facing away from it, repaired and clean, with its colours, extras, livery and plate.

A car that's out is marked `(out)` and can't be taken out twice. If it's wrecked, or lost (it despawned somewhere), it's back in the garage. A garage car that comes back with a saved game, e.g. parked at a safehouse, gets its own plate back when you get in (LibertyCityPlates gives every car a new random plate when a game loads).

## Settings (`LCGarage.ini`)

| Setting | Default | |
|---|---|---|
| `Garage1` … `Garage8` | the Lost clubhouse | Where you stop to honk, facing the door: `x y z heading`. All doors share the same cars |
| `Slots` | `20` | How many cars the garage holds (up to 64) |
| `TakeOutKey` | `F6` | Opens the list, on foot at a door |
| `StoreDistance` | `8` | Metres from a door where honking stores the car |
| `TakeOutDistance` | `12` | Metres from a door where the list opens |
| `ExitTurn` | `180` | Which way a car faces when it comes out, in degrees turned from facing the door |
| `PlatesIde` | blank | Your `vehicles.ide`. Blank = `update\common\data\vehicles.ide`, then `common\data\vehicles.ide` |

To find a door's position, stop where you'd honk and read your position with a trainer (or ask for a build that logs it).

## Files

- `LCGarage.cars`: your cars, one per line (model, colours, extras, livery, plate picture/font/layout and text). Plain text; it isn't tied to a savegame.
- `LCGarage.log`: what the plugin did. Send it with any bug report.

## Limits

- A plate you change with PlateChanger while a garage car is out isn't seen by the garage; the car goes back in with its garage plate.
- Damage isn't kept: cars always come out repaired (that's the point).
- The plugin calls the game's own script commands from outside a script. If one of them ever crashes, the plugin catches it, puts the screen and controls back, logs it, and carries on, instead of the game closing.

## Building

```sh
pip install ziglang
./build.sh          # build/LCGarage.asi (32-bit, no C runtime, kernel32 + user32 only)
./tests/run.sh      # plate rules and the cars file, on Linux, against tests/sample_vehicles.ide
```

## License

MIT
