# "Safe" astronaut installer
A basic homebrew with a single job, install and uninstall astronaut from a console. Forked from edo9300's unlaunch installer with the same purpose.

## Features
- Safety checks to install and uninstall astronaut with minimal risk of bricking
- Fully compatible with older unlaunch installs
- Works on every DSi, whether it's retail or development
- Keeps a recovery copy of unlaunch in NAND to protect against future bricks
  (only on retail consoles)

## Building
Blocksds 1.23.0 or later is required due to bugs in the `ftruncate` function,
once set up, just run `make` in the root folder.

## Notes
This installer only supports installing astronaut (which comes bundled with the nds), it cannot be used to install unlaunch or other unlaunch-likes.

Due to some unforunate version differences, the install method used by this
application won't be usable on consoles with firmware 1.4.2 (1.4.3 for china and korea).
So installing on consoles that ship that version won't be allowed

## Differences with official installer
The Nintendo DSi's stage2 has a failsafe where it will load a backup launcher
(HNAA) if it can't find the real one (eg. missing, corrupted), even on retail
consoles.

The original unlaunch installer will append itself to the tmd of the "real" launcher.
This leaves the console bricked in case of failed install or uninstall (since
the backup launcher doesn't exist, stage2 can't save you).
The safe unlaunch/astronaut installer takes advantage of this feature by installing
unlaunch to the backup launcher.
The official launcher's TMD is "broken" by changing a byte, making stage2
load astronaut from the backup launcher. 

This is safer than original unlaunch installs because as long as the main TMD
isn't touched, the system can't be bricked by those operations (apart from
total nand failure), and when the main TMD is tampered with, unlaunch is
already there as fallback in case of errors.
Uninstalling is also safer as this program only has to restore that
previously changed byte to restore the main TMD.
This allows to keep backup CFW "installed" as general brick
protection since it won't interfere with the system. As a bonus, if you
sell/trade you console in the future and the new owner uses the original unlaunch
installer, they'll be protected from bricks.

## Credits
- [edo9300](https://github.com/edo9300): for creating the unlaunch installer which this installer was created from
- [AntonioND](https://github.com/AntonioND/): for [blocksds](https://blocksds.skylyrac.net/)
- [Martin Korth (nocash)](https://problemkaputt.de):
  [GBATEK](https://problemkaputt.de/gbatek.htm) and [UNLAUNCH](https://problemkaputt.de/unlaunch.htm) which preceeded Astronaut.
- [JeffRuLz](https://github.com/JeffRuLz)/[Epicpkmn11](https://github.com/Epicpkmn11):
  [TMFH](https://github.com/JeffRuLz/TMFH)/[NTM](https://github.com/Epicpkmn11/NTM)
  (what this is project used as base for menus)
- [rvtr](https://github.com/rvtr):
   Adding support for installing to dev/proto consoles
