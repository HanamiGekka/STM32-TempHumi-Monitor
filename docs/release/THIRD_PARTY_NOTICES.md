# Third-party source and runtime notices

Project source/documentation is GPL-3.0-or-later. The repository owner is the primary developer; other team members are acknowledged and have agreed to public release. Existing upstream headers/licenses control their files.

| Component | Terms and corresponding source |
|---|---|
| QCustomPlot2.1.0 | Emanuel Eichhammer, GPL-3.0-or-later; complete source is in UIDesktop/plot and full GPL text in LICENSE |
| Qt5.14.2 runtime | Dynamically linked Core/Gui/Widgets/PrintSupport/SerialPort, qwindows and base image plugins. Original license notices are in binary packages; unmodified qtbase and qtserialport source tarballs accompany this Release |
| MinGW GCC7.3 runtime | Original GPL plus GCC Runtime Library Exception and winpthreads/Mingw notices are preserved in binary packages; exact source references and source access are in DEPENDENCY_SOURCES.md |
| ST HAL/device and Arm CMSIS | Original BSD/Apache notices retained under Drivers; complete texts also under LICENSES |
| Inno Setup7.1 | Original compiler license included in Setup; generated installer is unsigned |

Users may inspect, replace and relink the shared Qt libraries and rebuild the complete application. No restriction on reverse engineering for debugging such modifications is imposed. Source/binary manifests and the supplied upstream source archives provide access without an account or fee. Application builds do not use a custom modified Qt fork.

See [GPL](../../LICENSE), [LGPL](../../LICENSES/LGPL-3.0.txt), [BSD](../../LICENSES/BSD-3-Clause.txt), [Apache](../../LICENSES/Apache-2.0.txt), [corresponding sources](DEPENDENCY_SOURCES.md) and [Qt's official obligations](https://www.qt.io/development/open-source-lgpl-obligations).
