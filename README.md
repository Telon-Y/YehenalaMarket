# Yehenala Market Simulation

C++17 economic simulation with a Raylib UI. The current 1.2 model includes
production chains, labor allocation, construction, money, banking, investment,
securities, and the first multi-market trade interfaces.

## Build on Windows

The repository includes Raylib headers and static libraries. The supplied CMake
presets use the MinGW and Ninja installations under `D:/Code/mingw64`.

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

The GUI executable is `out/build/mingw-debug/YehenalaMarket.exe`.

## Headless diagnostics

`YehenalaProbe` runs the same core model without opening a window:

```powershell
out/build/mingw-debug/YehenalaProbe.exe 6000 520
```

Arguments are the number of simulated weeks and the reporting interval.

## Tests

CTest covers:

- Decimal division-by-zero behavior in all build modes.
- Pool-derived bank levels, exact upgrade thresholds, minimum availability,
  and the rule that banks never enter the construction queue.
- Development building classification: construction and financial buildings
  remain fully employed despite labor shortages or operating-fund constraints.
- Development wages are paid by each building owner's pool. Financial levels
  employ 1,000 people: 500 laborers, 250 engineers, and 250 capitalists.
- Construction departments are government-owned development buildings. Their
  wages and inputs use the player/government fund, while private construction
  spending transfers the matching amount from the investment pool to that fund.
- Construction output remains available at full employment even when there are
  no current construction orders or no separate construction-building cash.
- 6000-week numerical stability and the system credit ceiling.
- Invalid market lookup behavior.
- Cross-market goods and money conservation.
- Smoke simulations at 52, 700, 2600, and 6000 weeks.

## Source layout

- `local_market*.cpp`: weekly market pipeline, labor, supply, consumption,
  finance, settlement, and history.
- `building_manager*.cpp`: buildings, construction, AI expansion, employment,
  decay, and ownership.
- `price_engine.*`: commodity price dynamics.
- `world.*`: market collection and trade paths.
- `ui_*.cpp`, `main.cpp`: Raylib presentation and input.
- `tools/`: headless diagnostic executables.
- `tests/`: deterministic integration tests.

All source files are UTF-8. The application reads the selected font's `cmap`
table and passes every mapped Unicode codepoint to Raylib, including extended
CJK, compatibility, symbol, and private-use glyphs supported by the font.
