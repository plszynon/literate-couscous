# literate-couscous – PS3 File Manager

Prosty menedżer plików na PS3 (homebrew, PSL1GHT). Wymaga konsoli z CFW/HEN.

## Sterowanie
| Przycisk | Akcja |
|---|---|
| D-pad góra/dół | ruch po liście (przytrzymaj = szybciej) |
| D-pad lewo/prawo | strona wyżej / niżej |
| X | otwórz folder / info o pliku |
| O | folder wyżej |
| Kwadrat | kopiuj |
| Trójkąt | wytnij (przenieś) |
| Start | wklej tutaj |
| Select | usuń (potwierdzenie X / O) |
| R1 | nowy folder |

Napędy: `dev_hdd0`, `dev_hdd1`, `dev_usb000-007`, `dev_bdvd`, `dev_flash*` (flash tylko do odczytu).

## Budowanie
Lokalnie (ps3toolchain + PSL1GHT): `make pkg` → `ps3-filemanager.pkg`.
Albo przez GitHub Actions (`.github/workflows/build.yml`): zakładka Actions → artefakt z `.pkg`.

Instalacja: skopiuj `.pkg` na USB → Package Manager → Install Package Files.

Usuwanie plików jest nieodwracalne – uważaj na `dev_hdd0/home` i `game`.
