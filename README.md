# Hangszőnyeg – Windows-program

Egyablakos Windows-alkalmazás, amely a Hangszőnyeg teljes felületét futtatja, és mellette egy
**VST3-gazdát**: a gépre telepített effekt-pluginok bármelyik csatornára (A, B, C, Mester) láncba fűzhetők,
a felületük saját ablakban nyílik, a beállításaikat a preset elmenti.

## Letöltés

A telepítő és a hordozható változat minden módosítás után automatikusan elkészül:
**Actions → Windows-program → a legutóbbi futás → Artifacts → Hangszonyeg-Windows**.
A `v…` címkéjű változatok a **Releases** oldalon is megjelennek.

## Felépítés

- `Source/Main.cpp` – az alkalmazás és a főablak (JUCE + WebView2).
- `Source/LocalServer.*` – kis helyi kiszolgáló a 127.0.0.1-en: kiszolgálja a felületet, és WebSocketen
  kapcsolja össze a felületet a plugin-gazdával (vezérlés: JSON, hang: 512 mintás bináris blokkok).
- `Source/PluginHost.*` – VST3-keresés, csatornaláncok, plugin-ablakok, állapotmentés.
- `web/index.html` – a Hangszőnyeg felülete (ugyanaz, mint a böngészős változat; a gazdát magától felismeri).
- `cmake/PatchJuce.cmake` – a nézetek külön ablakai (window.open) a WebView2-ben is megnyílnak.
- `tests/testplugin` – próba-VST3 a Linuxos körúttesztekhez.

## Fordítás kézzel (Windows)

Visual Studio 2022 (C++ asztali fejlesztés), CMake 3.22+, és a Microsoft.Web.WebView2 NuGet-csomag
kibontva egy mappába:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_WEBVIEW2_PACKAGE_LOCATION=C:/utvonal/wv2
cmake --build build --config Release
```

A JUCE a fordításkor automatikusan letöltődik (8.0.10). Licenc: a JUCE ingyenes Starter licence vagy AGPLv3;
a VST3 SDK MIT-licences.
