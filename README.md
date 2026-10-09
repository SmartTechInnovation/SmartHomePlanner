# Smart Home Planner

Aplicație desktop (Qt 6 Widgets, C++17) pentru proiectarea instalațiilor smart home:
plan de fundal la scară, pereți, camere cu suprafețe în m², elemente din bibliotecă XML,
simularea luminilor și a difuzoarelor (cu umbre de la pereți) și export PDF pe capitole pentru electricieni.

## Funcții

- **Proiect nou / deschide / salvează** – fișier `.shp` (XML, imaginea planului inclusă în fișier). Undo/redo nelimitat.
- **Planul de fundal** – import PNG/JPG sau **PDF** (dacă modulul Qt PDF e instalat), opacitate, mutare.
- **Scara** – unealta *Scară* (K): click pe două puncte cu distanță cunoscută și introduci distanța în metri.
  Toate unitățile din program sunt reale (1 unitate = 1 cm).
- **Pereți** (W) – polilinii cu grosime; blochează lumina și sunetul.
- **Camere** (M) – poligoane cu nume și suprafață în **m²** (plus perimetru); eticheta se poate trage cu mouse-ul.
- **Elemente** – drag & drop din bibliotecă (sau dublu-click). Fiecare primește ID automat pe capitol (L1, S3, M2 ...).
- **Proprietăți** (dreapta): ID, capitol, **notiță afișată pe plan** (ex. `3x1.5`, `Tree`), documentație (link / fișier),
  link de magazin, mărime, rotație. Editarea se aplică tuturor elementelor selectate.
- **Lumini** – unghi fascicul, înălțime de montaj (implicit tavanul proiectului), temperatură de culoare sau RGB.
  Pata de lumină = (înălțime − plan de calcul) × tan(unghi/2), tăiată de pereți ca în DIALux.
- **Difuzoare** – rază de acoperire, tot tăiată de pereți.
- **Benzi LED** – linie (L) sau cerc (C), culoare, lățime de strălucire, lungimea totală în metri.
- **Simulare** – dublu-click pe lumină / bandă / difuzor = pornit/oprit; *Simulare de noapte* (N); toate aprinse/stinse.
- **Filtru pe capitole** – Iluminat, Senzori, Muzică ... (dublu-click = doar acel capitol).
- **Export PDF** – pagină de titlu (date proiect, notițe, sumar pe capitole, camere cu suprafețe, previzualizare),
  plan cu toate elementele, apoi câte o pagină pe capitol cu legendă: simbol, ID-uri, element, notiță, cantitate
  (+ lungimea benzilor LED) și scară grafică.
- Interfață în engleză și română (*Vizualizare → Limba*).

## Biblioteca de elemente

`Libraries/<Nume>/<Nume>.xml` + `Libraries/<Nume>/Assets/` (SVG/PNG). Programul caută folderul `Libraries`
lângă executabil (pe macOS în `Contents/Resources`), apoi în folderul sursă (la dezvoltare – modificările din XML
se văd la repornire, fără recompilare). Se poate forța cu variabila `SHP_LIBRARY_PATH`.

```xml
<Library name="Electrical" version="2">
  <Sections>
    <Section id="lighting" name="Lighting" name_ro="Iluminat" prefix="L" color="#F5A623"/>
    ...
  </Sections>
  <Category name="Lighting" section="lighting">
    <Item name="LED Spot WW PWM" icon="LEDSpotWW.svg" type="light" note="2x0.75 (24V)"
          beam="36" cct="3000" size="30" doc="https://..." shop="https://..."/>
    <Item name="Difuzor Speaker 7" icon="speaker_mounted.png" type="speaker" radius="300" section="music" note="2x1.5 LS"/>
    <Item name="Banda Led WW" icon="BandaLedWW.png" type="strip" color="#FFD27F" glow="35" note="2x0.75 (24V)">
      <Variant name="Tree" icon="TreeBandaLedWW.svg" note="Tree"/>
    </Item>
  </Category>
</Library>
```

| Atribut | Semnificație |
|---|---|
| `section` | capitolul (moștenit de la `Category` dacă lipsește) |
| `type` | `light`, `speaker`, `strip` sau `generic` |
| `note` | notița implicită afișată lângă element |
| `beam`, `cct` | unghiul fasciculului [°] și temperatura de culoare [K] (lumini) |
| `radius` | raza de acoperire [cm] (difuzoare) |
| `color`, `glow` | culoarea și lățimea strălucirii [cm] (benzi LED) |
| `size` | mărimea simbolului pe plan [cm] |
| `doc`, `shop` | documentație și link magazin |

`tools/convert_stilib.py` convertește vechiul `Electrical.stilib` din Grid Planner în formatul nou.

## Compilare

Qt 6.8+ (testat cu **Qt 6.11.2**): Core, Gui, Widgets, Svg, Xml, PrintSupport, LinguistTools; opțional Pdf și Test.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/SmartHomePlanner            # macOS: open build/SmartHomePlanner.app
```

În Qt Creator: *Open Project* → `CMakeLists.txt` → kit Qt 6.11.

Teste (mouse simulat, undo/redo, salvare/încărcare, umbre, calibrare):
```bash
cmake --build build --target selftest && ctest --test-dir build
```

Linia de comandă:
```bash
SmartHomePlanner --make-demo demo.shp                 # proiect demo
SmartHomePlanner --export-pdf plan.pdf proiect.shp    # export PDF fără interfață
SmartHomePlanner --render-png plan.png [--night] proiect.shp
```

Traduceri: `cmake --build build --target update_translations`, apoi `python3 tools/fill_ro.py`.

## Structura

```
src/core      Library (XML), Geometry (arii, umbre/vizibilitate, CCT), IconCache
src/scene     PlanScene (unelte, snap, filtre, undo), PlanView, itemi: Wall, Room, Device, Effect, Strip, Note
src/ui        MainWindow, LibraryPanel, PropertiesPanel, SectionPanel, dialoguri
src/export    PdfExporter
tests/        selftest (QtTest)
```
