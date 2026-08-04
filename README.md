# "Safe" astronaut installer
A basic homebrew with a single job, install and uninstall astronaut from a console. Forked from edo9300's unlaunch installer with the same purpose.

## Features
- Safety checks to install and uninstall astronaut with minimal risk of bricking
- Fully compatible with older unlaunch installs
- Works on every DSi, whether it's retail or development
- Keeps a recovery copy of unlaunch in NAND to protect against future bricks
  (only on retail consoles)

## Building
Blocksds 1.14.2 or later is required, once set up, just run `make` in the root folder.

## Notes
This installer only supports installing astronaut (which comes bundled with the nds), it cannot be used to install unlaunch or other unlaunch-likes.

Due to some unforunate version differences, the install method used by this
application won't be usable on consoles with firmware 1.4.2 (1.4.3 for china and korea).
So installing on consoles that ship that version won't be allowed and you will need to perform a system update.

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
