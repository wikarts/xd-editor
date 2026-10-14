# XD Editor — edytor i librarian Korg minilogue xd (VST3 / AU / standalone)

Otwarte odtworzenie funkcjonalności panelu Ctrlr „Korg Minilogue XD” (Sunny Synths, v2.4) jako natywna
wtyczka JUCE. Działa jak edytory producentów (np. wtyczki Moog): **patch zapisuje się w projekcie DAW
i po otwarciu projektu jest automatycznie wysyłany do syntezatora**, a do tego masz pełny librarian.

## Funkcje

| Z oryginalnej instrukcji | W XD Editor |
|---|---|
| Wszystkie parametry programu, edycja dwukierunkowa (CC/NRPN) | ✔ 10‑bitowe CC (CC63 + CC), NRPN 7/8/10‑bit, parametry kontekstowe (Multi shape/select, Mod FX sub type) |
| Parametry bez CC („jasnozielone”) + małe przyciski Send | ✔ + opcjonalne **automatyczne wysyłanie zrzutu** 250 ms po zmianie |
| LCD: nazwa, opis, CC/NRPN, wartość bieżąca i zapisana | ✔ (+ przycisk LCD OFF) |
| Wykresy obwiedni ADSR / AD sterowane myszą | ✔ |
| Direct mode, Source (Disk 1 / Disk 2 / Minilogue), Bank, Program, pozycja 000 = plik | ✔ |
| Load / Save / Receive / Send / Rename / Init | ✔ (Init zachowuje autora) |
| Pliki .syx, .mnlgxdprog, .mnlgxdlib (odczyt), .prog_bin | ✔ — **.mnlgxdlib także zapis** |
| Opis, autor, kategoria, data zapisu (`_AddInfo`) | ✔ (plik `nazwa_AddInfo.xml` obok .syx; w .mnlgxdprog w prog_info) |
| Parametry globalne (Request / Send) | ✔ |
| Librarian: 3 banki × 500, load/save/init/receive/send/clear/eksport listy | ✔ |
| Operacje: load, init, wstaw plik, zapis do pliku, bufor → slot, move, copy, multi‑copy (as‑is / collated), swap, porównania | ✔ |
| Clean dump (`_CleanDump.syx`), BankAutoSave, gwiazdka przy niezapisanym banku | ✔ |
| Morphing z blokadą sekcji, Morph 0% / 100%, CC 100 | ✔ + **parametr automatyzacji „Morph”** |
| Podgląd Note / Motion sequencera | ✔ (tylko odczyt, jak w oryginale) |
| Nazwy modułów USER (osc / mod fx / delay / reverb) | ✔ |
| Wyświetlanie i eksport informacji o programie (7 trybów) | ✔ |
| Ustawienia potwierdzeń, tryb zapisu, szybkość transferu, zoom z pamięcią | ✔ |
| Device Inquiry + przycisk ponowienia | ✔ (kanał MIDI ustawia się sam z odpowiedzi syntezatora) |

## Pobranie gotowych wersji (Windows / macOS)

Repozytorium zawiera workflow GitHub Actions, który buduje wszystko automatycznie:

1. Utwórz repozytorium na GitHubie i wypchnij do niego ten katalog.
2. Zakładka **Actions** → ostatni przebieg „Build” → sekcja **Artifacts**:
   `XD-Editor-Windows` (VST3 + .exe) i `XD-Editor-macOS` (VST3 + AU + .app, universal arm64/x86_64).
3. Tag `v1.0.0` (`git tag v1.0.0 && git push --tags`) dodatkowo tworzy Release z plikami .zip.

## Instalacja

**Windows:** skopiuj folder `XD Editor.vst3` do `C:\Program Files\Common Files\VST3\`. `XD Editor.exe` to wersja samodzielna.

**macOS:** `XD Editor.vst3` → `~/Library/Audio/Plug-Ins/VST3/`, `XD Editor.component` → `~/Library/Audio/Plug-Ins/Components/`
(Logic używa AU). Ponieważ build nie jest notaryzowany, zdejmij kwarantannę:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"XD Editor.vst3" ~/Library/Audio/Plug-Ins/Components/"XD Editor.component"
```

## Konfiguracja MIDI (zakładka Settings & Info)

* **Wejście:** `minilogue xd KBD/KNOB` (USB) lub port MIDI DIN. **Wyjście:** `minilogue xd SOUND`.
* **Kanał** ustawia się sam po udanym Device Inquiry (kanał globalny syntezatora).
* **Trasa wyjścia:**
  * *port MIDI urządzenia* (domyślnie) — wtyczka sama rozmawia z syntezatorem; SysEx działa w każdym DAW
    (także w Ableton, który nie przepuszcza SysEx z wtyczek). Nuty ze ścieżki DAW przechodzą do syntezatora
    („Thru: host notes → synth”). W DAW wyłącz wtedy ten sam port dla ścieżek, żeby nie dublować nut
    (na Windows port może być otwarty tylko przez jeden program).
  * *przez DAW* — wtyczka wysyła wszystko na swoje wyjście MIDI, a DAW kieruje je do syntezatora.
  * *oba*.
* Jeśli syntezator wysyła CC do DAW, włącz „Host MIDI in = synth feedback” — ruchy gałek syntezatora
  zaktualizują panel i mogą być nagrywane jako automatyka.

## Praca w DAW (jak edytor Moog)

* Każda instancja zapisuje w projekcie: bieżący patch (wszystkie 1024 bajty, łącznie z sekwencją),
  wersję „zapisaną” (do porównań), opis/autora, ścieżki do plików banków, stan panelu i blokady morphingu.
* Po otwarciu projektu patch jest wysyłany do bufora syntezatora (~0,7 s po wczytaniu) — opcja
  **Send patch on project load**. Opcja **Send patch when editor opens** wysyła patch danej ścieżki,
  gdy otworzysz jej okno (przydatne przy kilku ścieżkach z jednym syntezatorem).
* Presety DAW (Cubase .vstpreset, Reaper, Studio One, Ableton, Logic .aupreset, track presety) działają,
  bo zapisują ten sam stan.
* Wszystkie ~120 parametrów + „Morph” są dostępne do automatyzacji, ze stabilnymi ID (nazwa parametru,
  nie numer), więc dodanie parametrów w przyszłości nie rozsypie automatyki.
* Przy wielu instancjach dane przychodzące z syntezatora trafiają do instancji, z którą ostatnio pracowałeś.

## Budowanie lokalnie

Wymagania: CMake ≥ 3.22, kompilator C++17 (Visual Studio 2022 / Xcode 15 / GCC 11+). JUCE 8.0.4 pobiera się
automatycznie (albo wskaż własny: `-DJUCE_DIR=...`).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Wyniki: `build/XDEditor_artefacts/Release/{VST3,AU,Standalone}`.
Testy: `XDCoreTests` (format danych, pliki, morphing) i `XDProcessorTests` (MIDI, stan DAW, transfer banku z symulowanym syntezatorem).
Na Linuksie potrzebne pakiety: `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev`.

## Co zostało sprawdzone, a czego nie

Sprawdzone: kompilacja (GCC i Clang, bez ostrzeżeń), testy jednostkowe i integracyjne, walidacja VST3
narzędziem **pluginval (strictness 7, z testami GUI): SUCCESS**.

Mapa CC/NRPN, listy wartości (cele joysticka/CV, mikrostroje, podziały arpeggiatora, wzory arp/akordy,
parametry motion sequencera) i format odpowiedzi USER SLOT STATUS są zgodne z oficjalnym dokumentem
**KORG minilogue xd MIDI Implementation rev. 1.01**. Format danych sprawdzony na prawdziwym banku 500 programów
z Korg Sound Librarian (wczytanie, nazwy, tryby, efekty, motion sequence, wartości spoza zakresu zachowane bez zmian).

**Jeszcze nie testowane na żywym syntezatorze** (komunikacja w czasie rzeczywistym). Warto sprawdzić:

* reakcję syntezatora na gałki panelu (CC / NRPN) i odwrotnie,
* odbiór i wysyłanie banku oraz zapis do slotów (Program Data Dump 0x4C),
* nazwy modułów USER (pusty slot syntezator zgłasza błędem → nazwa `EMPTYn`).

Jeśli coś zachowuje się inaczej, poprawka to zwykle jedna linia w `Source/core/Params.cpp` (tabela parametrów).

## Struktura

```
Source/core/      format programu (1024 B), tabela parametrów, SysEx, pliki, raporty, morphing  (bez GUI)
Source/MidiEngine współdzielone porty MIDI, parser CC/NRPN
Source/PluginProcessor  logika: parametry/automatyka, wysyłanie/odbiór, transfery, stan projektu DAW
Source/Librarian  operacje na bankach
Source/gui/       wygląd, kontrolki, 5 zakładek
Tests/            testy
```

## Licencja

Kod używa frameworka JUCE 8 (licencja AGPLv3 lub komercyjna). Do własnego użytku nie ma ograniczeń;
jeśli będziesz dystrybuować binarki, rób to na zasadach AGPLv3 (z kodem źródłowym) albo z licencją JUCE.
„Korg” i „minilogue” są znakami towarowymi Korg Inc.; projekt nie jest z nią związany.
