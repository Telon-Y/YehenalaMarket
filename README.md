# Yehenala Market Simulation

Yehenala is a C++17 economic simulation with a Raylib interface. Version 2.0 remains the active data, ownership, and simulation contract. The executable defaults to the standard country-market world: every province has one LocalMarket, every country has one NationalMarket, and each country's local markets receive the five-market specialization model. Use `--debug-five-markets` for the isolated five-market diagnostic fixture.

## Five-market Debug fixture

The explicit `--debug-five-markets` mode creates five specialized local markets in one country. Every pair of markets is connected in both directions for every storable commodity, giving 10 undirected market pairs, 20 directed market pairs per commodity, and 220 directed routes in total.

- Market A produces grain and processed food.
- Market B produces fabric, clothes, and luxury clothes.
- Market C produces coal and iron.
- Market D produces steel and tools.
- Market E produces housing, construction capacity, and precious metals.

The Debug UI keeps the 1.1 panels for Goods, Buildings, Construction, and Macro data, then adds market switching and warehouse-network diagnostics. The Goods panel exposes the complete weekly chain, 52-week demand, production commands, inventory-review cadence, route distance and price decomposition, active orders, in-transit cargo, and the conservation residual. The Buildings panel exposes each input buffer's on-hand, reserved, confirmed-inbound, and backlog quantities. The Macro panel uses 52-week GDP for the headline while retaining raw weekly GDP for batch-level diagnosis, railway revenue, and warehouse profit.

    out/build/mingw-release/YehenalaMarket.exe
    out/build/mingw-release/YehenalaMarket.exe --debug-five-markets --debug-panel macro --debug-market 4
    out/build/mingw-release/YehenalaMarket.exe --world-gui
    out/build/mingw-release/YehenalaMarket.exe --debug-five-markets --warmup 1000 --dump-debug-state out/debug-report.json
    out/build/mingw-release/YehenalaMarket.exe --warmup 1000 --dump-debug-state out/report.json

The headless report exits successfully only when topology, warehouse audit, inventory conservation, finite GDP, and positive 52-week GDP all pass.

## Production and logistics contract

1. Each warehouse performs at most one all-commodity inventory review per logistics week. Repeated calls in the same cycle are suppressed and counted by the debug probe.
2. Consumer demand and production-order arrivals feed exact rolling 52-week means. The production department receives that mean as its constant-demand command, bounded by physical capacity and outstanding committed demand.
3. A warehouse or building-buffer shortage creates one root replenishment demand. The order first allocates unreserved local stock.
4. Any remaining shortage selects a route only when both endpoints have railway buildings and the destination price exceeds the origin price plus the quoted railway capacity cost.
5. Railway capacity is priced as labor wage plus railway raw-material prices. Distance does not change that capacity-unit price; it changes the capacity consumed by cargo (`distance * coefficient * cargo / 100`). The default railway setup starts at 10 levels and each level supplies 20 capacity units.
6. The buyer funds escrow, the seller reserves exportable stock, and a producer creates a directed production child when stock alone is insufficient. Receipt settles the locked quote separately to the origin producer, source railway, and destination warehouse.
7. Production can run only against a pending production order and committed building inputs. Inputs move from the market warehouse into the producer buffer, are reserved for that order, and are consumed when output completes.
8. Completed output enters the producer market warehouse. Local demand moves it into a building buffer; remote demand dispatches it on the selected route, records physical in-transit stock, and settles escrow on receipt.
9. Consumer, construction, and intermediate use remove goods only from their final local inventory. Every weekly commodity statement must satisfy:

       opening + production + receipts
       - dispatches - transfers to producers - consumer use - construction use
       = closing

GDP uses the production approach: current-cycle output value minus current-cycle intermediate input value. `getWeeklyGDP()` remains the raw weekly statement; `getGDP()` and world snapshots expose the rolling 52-week total so an idle batch week cannot erase a market's displayed GDP.

## Frozen ownership model

| Object | Owns | Does not own |
| --- | --- | --- |
| Country | Treasury, reserved construction budget, national construction queue, member provinces, aggregated population and GDP | Nothing in the player command path is outside the country |
| Province | Local population, buildings, employment, local market, and construction placement | Player cash, an independent construction budget, or an independent player build decision |
| Construction project | Payer country, target province, building type, quantity, budget, reservation, progress, and lifecycle status | A direct relationship to a player cash pool |
| Player | National commands issued on behalf of the selected country | Independent cash or construction ownership |

Private capitalist expansion remains a private-economic action. Player-issued construction always enters the payer country's national queue.

## 2.0 world UI

The standard runtime draws the world map first and keeps it visible behind a detail overlay. `--world-gui` remains a compatibility alias for this mode.

- Clicking a province opens its country panel with that province selected.
- The country panel occupies the left two-fifths (40 percent) of the viewport and fills the usable height.
- The upper-right Build list button opens an independent national construction overlay occupying the right two-fifths; its clicks and wheel input never reach the map.
- The country header shows a 3:2 flag, name/country code, national population, GDP, treasury, reserved budget, and available treasury.
- The country pages are Overview and Construction. The overview lists member provinces; the construction page is the only player build entry point and exposes cancellation for queued or active projects.
- Clicking a province row opens the embedded local-market desk in the same left two-fifths overlay. It reuses the four Debug panels (Goods, Buildings, Construction, and Macro) with a compact layout; the selected province's LocalMarket is bound directly without changing the world's current market.
- The embedded desk's building controls remain command-only: `+` queues exactly one national project through `World`, and `-` cancels the newest queued or active project for that province/building type.
- Returning from a province preserves the country, selected province, selected page, and independent scroll offsets. Closing the panel returns to the map.
- The panel consumes clicks and wheel input inside its rectangle. The map continues to hover, select, edge-scroll, and zoom outside that rectangle.
- Map wheel zoom is clamped to a readable range and keeps the cursor's longitude as the zoom anchor. Horizontal map scrolling wraps at the Pacific seam.
- The world map uses the embedded Natural Earth land layer as a neutral visual base. Reviewed province geometry is drawn above it and is the only interactive layer; land that has no province binding remains visible but cannot be selected or navigated.

Every province owns one LocalMarket. Every country owns one NationalMarket containing the IDs of its member local markets. Country population and GDP are calculated from member province snapshots for the same simulation cycle; no second writable aggregate is maintained.

## Version and map scenario

The repository has no 1.1 or 1.2 branch/tag. The checked-in runtime contract is 2.0 and the map scenario is frozen in `scenario_config.h`:

- `scenarioYear = 1880`
- `alternateHistory = true`
- `scenarioKey = 1880_alternate_colonial_relations`

This is an explicit alternate-history scenario. It combines post-1867 Austria-Hungary, later colonial subject relations, and the alternate Prussian central-African relation; it must not be presented as one real-world historical year.

The neutral land base is Natural Earth 1:50m (`data/ne_50m_admin_0_countries.geojson`). Reviewed province geometry is generated offline from Natural Earth 10m admin-1 (`data/ne_10m_admin_1_states_provinces.geojson`) plus the frozen water-hole dataset (`data/ne_10m_review_water.geojson`) by `tools/build_reviewed_geometry.py` using Shapely/GEOS dissolve, precision snapping, validity repair, source assignment, water-hole, and cross-feature positive-area overlap gates. Rendering, labels, and hit testing consume the same final `ProvinceShape.parts` and `ProvinceShape.holes` geometry. No post-draw historical overlay or rectangular longitude/latitude envelope is authoritative. `geometryReviewed` controls the strict ring-topology and land-coverage gates; basemap binding repairs reviewed label anchors from the same final geometry.

Map colors are a tested palette: CHI `#F2E6A7` (China), FRA `#4777B8` (France), GBR `#C84B4B` (Britain), PRU `#8A6A4A` (Prussia), RUS `#8EBE6F` (Russia), AUS `#E6E4DD` (Austria), USA `#9DCBE7`, MEX `#4E9662`, BRA `#3E9B57`, JAP `#B83A42`, SWE `#79C6D5`, and LCO `#E58A32`. Supplementary modeled countries have explicit entries in `country_palette.cpp`. Subject countries resolve recursively to their final `overlordKey` color; every target must exist and the relation graph must be acyclic. Austria's light gray requires a dark boundary stroke.
To regenerate the checked-in reviewed geometry, run `python tools/build_reviewed_geometry.py` from the repository root (or `tools/build_reviewed_geometry.ps1` on Windows). The CMake target `reviewed_geometry` runs the same command and is a dependency of `YehenalaCore`; it uses the checked-in 10m admin-1 and water inputs, so a normal build cannot embed a stale reviewed map. The generator fails on invalid/empty regions, any positive-area overlap between reviewed features, a missing or duplicated Russian source district, a Russian union mismatch, or a filled Caspian/1880 Aral water point.

The frozen political relation set is: Britain -> South Africa, India, Canada, Australia; France -> Algeria, Indochina, West Africa; Turkey -> Egypt; Russia -> Finland; Sweden -> Norway; Prussia -> Central Africa; Low Countries -> East Indies. These relations affect political ownership and color only, not fiscal, military, or AI behavior.

### Geography acceptance descriptions

The following descriptions are the source contract for map editing and review:

- **Taiwan / east China:** treat Taiwan at approximately 120-122.1E / 21.9-25.3N and Penghu as an independent `MultiPolygon` part of `east_china`; never connect it to the mainland with a straight segment. High-resolution data may add Kinmen and Matsu. Acceptance points are Taipei, Taichung, and Kaohsiung.
- **England:** if the name remains England, use only southern Great Britain: the northern edge follows the Scotland border, Wales is excluded, and the Isle of Wight and Isles of Scilly remain parts. Scotland and Wales are separate provinces. Ireland contains no British-island geometry. If the province is not split, rename it Great Britain.
- **Punjab:** use the upper Indus basin and the Jhelum, Chenab, Ravi, Beas, and Sutlej river basins, including Lahore, Amritsar, Multan, and Rawalpindi. The west ends at the Sulaiman foothills, the north at the Himalayan front, the east at the Yamuna watershed, and the south at the northern Thar Desert. Exclude Kabul, Balochistan, Karachi, the high Kashmir ranges, and Gujarat.
- **Central Asian steppe:** cover the selected-year Russian Kazakh steppe and Turkestan. The west follows the eastern Caspian shore, the south the Persian and Afghan borders, the east the Tien Shan and Chinese border, and the north the Siberian forest-steppe line. The Caspian and Aral Seas are explicit water holes; Persia, Afghanistan, and Xinjiang are excluded.
- **Prussia:** draw discontinuous territory: Rhineland/Westphalia and western enclaves, then Brandenburg, Pomerania, Silesia, Posen, and East/West Prussia. Follow Dutch, Belgian, Danish, Baltic, German-state, and Russian-Polish borders. Do not replace these components with one belt across northern Germany and Poland.
- **Congress Poland:** because the province is Russian-owned, draw Congress Poland rather than modern Poland: Warsaw, Lodz, Lublin, and the Vistula basin; north-west touches Prussia, south touches Austrian Galicia, and east touches Russia proper. Exclude Posen, East/West Prussia, Galicia, and Vilnius.
- **Austria-Hungary and Italy:** use the post-1867 name only. Lombardy and Venetia belong to Italy. Austria retains South Tyrol/Trentino, Trieste, Istria, Dalmatia, Bohemia, Moravia, Galicia, Bukovina, Hungary, Slovakia, Transylvania, and Croatia. Italy is the boot-shaped peninsula plus Sardinia and Sicily, with Austrian territory excluded and no rectangular islands or Adriatic bridge polygons.
- **Ottoman Turkey:** use a date-consistent name. The homeland consists of Anatolia, East Thrace, and selected directly ruled Balkan, Levantine, and Mesopotamian parts as separate coastal/border-following `MultiPolygon` components. Exclude Greece, Persia, Russian Caucasus, and Austria-Hungary; never fill the Black, Aegean, or Mediterranean seas.
- **Egypt:** include the Nile Delta, Nile Valley, and Sinai. If Khedivate Sudan is modeled, keep Sudan as an independent component/province along the Nile and Red Sea; do not cover the Ethiopian highlands, Somalia, Kenya, or Libya. A puppet keeps its own boundary but resolves to its overlord color.
- **Russia:** the ten macro-regions are North Russia (Kola-White Sea-Arkhangelsk), Central Russia (Moscow and Oka-Volga core), Baltic (Gulf of Finland/Riga shore), Ukraine (Dnieper and north Black Sea), Caucasus (between Black and Caspian), Central Asian Steppe, West Siberia, Central Siberia, East Siberia, and Far East (Amur, Ussuri, Primorye, Kamchatka, Chukotka, and selected-year Sakhalin). Parts must meet without overlap, holes must remain water, and the Pacific seam must never close across ocean.

The province page body, labels, and building cards use a shared minimum 14px text contract. Building `+` queues exactly one national project through `World`; `-` cancels the newest matching queued/active project and does not write `LocalMarket` directly.

## National construction accounting

1. A project must target a province currently owned by the payer country.
2. Before enqueueing, the model checks treasury minus already reserved construction budget.
3. A successful order reserves the complete requested budget atomically, so separate provinces cannot spend the same funds.
4. Each simulation cycle consumes construction goods and settles actual spending from the reservation. Completion adds the building to the target province.
5. Cancellation releases only the unpaid reservation. Paid construction spending is never refunded.
6. Province building cards are snapshot-driven. `+` and `-` are command controls only: `+` queues one unit through the payer country's national queue, while `-` releases the newest matching unpaid reservation through `World`; no UI drawing function writes a province or LocalMarket directly.
7. A country transfer is rejected while active national or legacy government orders target the province. Invalid foreign or orphaned projects are blocked and their remaining reservation is released.
8. Government money flows for country-owned provinces use the country treasury. The legacy LocalMarket playerCash path remains only for standalone markets and private compatibility paths.
9. Country deletion is refused while member provinces remain attached; orphaned active projects are blocked and unpaid reservations are released before an empty country is removed.
10. National project prices are locked at enqueue time. Each country shares one staffed construction-capacity pool across its active projects, and each successful payment is posted to the target province's construction department and the next market GDP statement.
11. Bad-debt write-off removes the matching lender-side cash/capital entry instead of recapitalizing a borrower from nowhere. Fiscal-country treasury is included once per member province in local money-supply and price-level accounting.

## Build on Windows

The repository includes Raylib headers and static libraries. The supplied CMake presets use MinGW and Ninja under D:/Code/mingw64.

    cmake --preset mingw-debug
    cmake --build --preset mingw-debug --target reviewed_geometry
    cmake --build --preset mingw-debug

The GUI executable is out/build/mingw-debug/YehenalaMarket.exe. A release package can be built with the mingw-release preset and installed with cmake --install.

## Deterministic screenshots

    out/build/mingw-debug/YehenalaMarket.exe --screenshot map.png --size 1920x1080 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot map_zoom.png --map-zoom 2 --map-scroll 1200 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot country.png --view country --tab overview --province north_china --size 1024x720 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot construction.png --view country --tab construction --province north_china --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot construction_panel.png --construction-panel --size 1024x720 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot province.png --view province --province north_china --size 1920x1080 --frames 3

Supported country tab aliases are overview and construction. The older local, national, transport, and numeric aliases remain accepted by the launcher for compatibility, but they do not grant province-level construction rights.

### Geography regression screenshots

The map viewport is a wrapped 3840px world. These deterministic zoomed captures place the reviewed regions in the center and keep the Pacific seam visible for visual review:

    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/taiwan.png --map-zoom 2 --map-scroll 2250 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/england.png --map-zoom 2 --map-scroll 960 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/poland_balkans.png --map-zoom 2 --map-scroll 1250 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/near_east.png --map-zoom 2 --map-scroll 1450 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/russia.png --map-zoom 2 --map-scroll 2050 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/pacific_seam.png --map-zoom 2 --map-scroll 3700 --size 1440x900 --frames 3

The generated files are under the ignored `out/` directory. A review must check Taiwan as a separate island part, England/Scotland/Wales separation, Congress Poland and Prussia adjacency, Balkan/Near-East coast and sea holes, the ten Russian macro-regions, and no polygon closure across the Pacific seam.

## Source layout

- world*.cpp, country.*, province.*, world_data.*: registries, snapshots, national queues, province markets, and simulation orchestration.
- warehouse*.cpp and local_market*.cpp: logistics lifecycles, weekly local simulation, and standalone compatibility finance.
- building_manager*.cpp: local buildings, private orders, and construction settlement primitives.
- map_model.*, map_layout.*, world_basemap.*: projection, looping, zoom, geometry, and hit testing.
- ui_map.cpp, ui_country.cpp, ui_draw.cpp, ui_input.cpp, ui_navigation.cpp, main.cpp: map shell, country overlay, province submenu, input, navigation, and screenshots.
- ui_construction.cpp: right-side national construction list, reservation metrics, scrolling, and cancellation input.
- debug_ui.cpp, debug_ui.h, debug_report.cpp: full-screen five-market diagnostic UI plus the embedded four-panel LocalMarket desk and headless audit report.

All source files are UTF-8. The selected font is checked through its cmap table before the UI starts.
