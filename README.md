# Yehenala Market Simulation

Yehenala is a C++17 economic simulation with a Raylib interface. Version 2.0 remains the active data, ownership, and simulation contract. The executable defaults to the standard country-market world: every province has one LocalMarket, every country has one NationalMarket, and each country's local markets receive the five-market specialization model. Use `--debug-five-markets` for the isolated five-market diagnostic fixture.

## Five-market Debug fixture

The explicit `--debug-five-markets` mode creates five specialized local markets in one country. Every pair of markets is connected in both directions for every storable commodity, giving 10 undirected market pairs, 20 directed market pairs per commodity, and 220 directed routes in total.

- Market A produces grain and processed food.
- Market B produces fabric, clothes, and luxury clothes.
- Market C produces coal and iron.
- Market D produces steel and tools.
- Market E produces housing, construction capacity, and precious metals.

The Debug UI keeps the 1.1 panels for Goods, Buildings, Construction, and Macro data, then adds market switching and warehouse-network diagnostics. The Goods panel exposes the complete weekly chain, 52-week demand, production commands, inventory-review cadence, route distance and price decomposition, active orders, in-transit cargo, and the conservation residual. The Buildings panel exposes each input buffer's on-hand, reserved, confirmed-inbound, and backlog quantities. The Macro panel uses 52-week GDP for the headline while retaining raw weekly GDP for batch-level diagnosis and railway revenue. It does not display warehouse profit: the warehouse margin share is fixed at zero by design, so no warehouse margin channel exists to report.

    out/build/mingw-release/YehenalaMarket.exe
    out/build/mingw-release/YehenalaMarket.exe --debug-five-markets --debug-panel macro --debug-market 4
    out/build/mingw-release/YehenalaMarket.exe --world-gui
    out/build/mingw-release/YehenalaMarket.exe --debug-five-markets --warmup 1000 --dump-debug-state out/debug-report.json
    out/build/mingw-release/YehenalaMarket.exe --warmup 1000 --dump-debug-state out/report.json

The headless report exits successfully only when topology, warehouse audit, inventory conservation, finite GDP, *unfloored* positive 52-week GDP, GDP stability, and money conservation all pass. Note that the published per-market GDP is floored at a small positive value for markets that own productive buildings; the gate judges the unfloored series (`rawWeeklyGdp` / `rawAnnualizedGdp`), so a market whose value added falls to or below zero now fails instead of being masked.

Money conservation is judged by `moneyConserved`: the report sums every pool (each market's class pools, its investment pool and building cash, every country treasury, and the warehouse escrow) plus the seigniorage recorded as created, and compares that against the opening total. A residual is money that moved with no receiver. The standard world closes that identity to within Decimal rounding over 208 weeks (residual 3.6e-7 on a total of 7.9e10). The four black holes it exposed - bank retention debited into a capital counter, national construction revenue credited twice, consumer purchases credited above what households could pay, and construction-department inputs credited above what was actually paid - are all closed. See FUNCTIONAL_AUDIT.md section 17 item 14.

## Production and logistics contract

1. Each warehouse performs at most one all-commodity inventory review per logistics week. Repeated calls in the same cycle are suppressed and counted by the debug probe.
2. Consumer demand and production-order arrivals feed exact rolling 52-week means. The production department receives that mean as its constant-demand command, bounded by physical capacity and outstanding committed demand.
3. A warehouse or building-buffer shortage creates one root replenishment demand. The order first allocates unreserved local stock.
4. Any remaining shortage selects a route only when both endpoints have railway buildings and the destination price exceeds the origin price plus the quoted railway capacity cost.
5. Railway capacity is priced as labor wage plus railway raw-material prices. Distance does not change that capacity-unit price; it changes the capacity consumed by cargo (`distance * coefficient * cargo / 100`). The default railway setup starts at 10 levels and each level supplies 20 capacity units. Route distance is currently a placeholder derived from the ordering of a country's provinces rather than from geography, and every rail route takes exactly one cycle; see FUNCTIONAL_AUDIT.md section 9.3.
6. The buyer funds escrow, the seller reserves exportable stock, and a producer creates a directed production child when stock alone is insufficient. Receipt settles the locked quote separately to the origin producer, source railway, and destination warehouse.
7. Production can run only against a pending production order and committed building inputs. Inputs move from the market warehouse into the producer buffer, are reserved for that order, and are consumed when output completes.
8. Completed output enters the producer market warehouse. Local demand moves it into a building buffer; remote demand dispatches it on the selected route, records physical in-transit stock, and settles escrow on receipt.
9. Consumer, construction, and intermediate use remove goods only from their final local inventory. Every weekly commodity statement must satisfy:
       opening + production + receipts
       - dispatches - transfers to producers - consumer use - construction use
       = closing

GDP uses the production approach: current-cycle output value minus current-cycle intermediate input value. `getWeeklyGDP()` remains the raw weekly statement; `getGDP()` and world snapshots expose the rolling 52-week total so an idle batch week cannot erase a market's displayed GDP.

## Money loop and the terminal household pool

1. Seigniorage is the only path that creates money; every other movement between pools has a payer and a receiver. Money that is debited from one pool and credited to none is a black hole, and the audited identity (`total(t) - total(0) == created - residual`) makes it visible instead of silent.
2. Revenue is only ever credited up to what the buyer actually paid. Consumer purchases are scaled back to the cash the households hold, and a construction department's input plan reaches its suppliers only in proportion to what the department and the treasury actually settled.
3. Settlement that happens before the revenue statement - construction power bought by the state - is posted to the statement for GDP and profit reporting only, never routed a second time into merchant revenue.
4. Bank retention raises the institution's capital counter; it is not a second payment, so it must not debit the bank's cash.
5. The investment pool is working capital for construction, not a store of value. Each cycle the part of the pool above the market's own construction requirement (`INVESTMENT_POOL_WORKING_WEEKS` of its investment-funded construction demand, floored at `INVESTMENT_POOL_MIN_WORKING_MONEY`) is paid out to households at `INVESTMENT_POOL_RETURN_SHARE`. The class pools are the terminal destination, distributed by population, so the labor pool - the overwhelming majority of the population - absorbs the surplus instead of the pool accumulating it. Without this valve the standard world drained every class pool (-30.5e9 in total over 208 weeks) straight into the investment pool (+40.0e9), and the lost household purchasing power is what dragged consumer demand down.

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
- The country construction page and embedded province building/construction panels issue player build commands immediately. Live queues expose pause, resume, cancellation and round up/down controls; ordinary clicks swap neighbors and either Shift key moves to the first/last position.
- Clicking a province row opens a wider responsive local-market desk while leaving map context visible where the viewport permits. It reuses the four Debug panels (Goods, Buildings, Construction, and Macro); the selected province's LocalMarket is bound directly without changing the world's current market.
- The province Buildings panel presents a limited set of two-line operating summaries at once, a visible vertical position indicator, and a fixed input-buffer detail section. Mouse wheel, Up/Down, and Page Up/Page Down move through the full building list.
- The embedded desk's building controls are command-only: `+` queues exactly one national project through `World`, and `-` immediately demolishes one government-owned building of that type, including construction departments. Private buildings are protected, demolition has no cooldown, and cancelling a queued or active project is a separate action available only in the construction list (see the building-page notes below).
- Returning from a province preserves the country, selected province, selected page, and independent scroll offsets. Closing the panel returns to the map.
- The panel consumes clicks and wheel input inside its rectangle. The map continues to hover, select, edge-scroll, and zoom outside that rectangle.
- The map pans in eight directions from mouse edges/corners or combined arrow/WASD keys. Horizontal movement wraps at the Pacific seam; vertical movement clamps at the projected world limits.
- Hold the middle mouse button over the map and drag to grab the map directly. The drag keeps pointer ownership until release, follows the cursor, wraps horizontally, and clamps vertically.
- Map wheel zoom is clamped to a readable range and keeps both the cursor's longitude and latitude as the zoom anchor.
- The world map uses the embedded Natural Earth land layer as a neutral visual base. Reviewed province geometry is drawn above it and is the only interactive layer; land that has no province binding remains visible but cannot be selected or navigated.

Every province owns one LocalMarket. Every country owns one NationalMarket containing the IDs of its member local markets. Country population and GDP are calculated from member province snapshots for the same simulation cycle; no second writable aggregate is maintained.

## Version and map scenario

The repository has no 1.1 or 1.2 branch/tag. The checked-in runtime contract is 2.0 and the map scenario is frozen in `scenario_config.h`:

- `scenarioYear = 1880`
- `alternateHistory = true`
- `scenarioKey = 1880_alternate_colonial_relations`

This is an explicit alternate-history scenario. It combines post-1867 Austria-Hungary, later colonial subject relations, and the alternate Prussian central-African relation; it must not be presented as one real-world historical year.

The neutral land base is Natural Earth 1:50m (`data/ne_50m_admin_0_countries.geojson`). Reviewed province geometry is generated offline from Natural Earth 10m admin-1 (`data/ne_10m_admin_1_states_provinces.geojson`) plus the frozen water-hole dataset (`data/ne_10m_review_water.geojson`) by `tools/build_reviewed_geometry.py` using Shapely/GEOS dissolve, precision snapping, validity repair, source assignment, water-hole, and cross-feature positive-area overlap gates. China is a complete 32-district admin-1 partition across its five mainland macro-provinces, with Taiwan and its islands appended to east China; Mongolia is rebuilt from the same 10m source. Belarus belongs to Central Russia, while Volhynia and Podolia belong to the Russian Ukraine region and Austrian Galicia/Bukovina remain Austrian. Rendering, labels, and hit testing consume the same final `ProvinceShape.parts` and `ProvinceShape.holes` geometry. No post-draw historical overlay or rectangular longitude/latitude envelope is authoritative. `geometryReviewed` controls the strict ring-topology and land-coverage gates; basemap binding repairs reviewed label anchors from the same final geometry.

Map colors are a tested palette: CHI `#F2E6A7` (China), FRA `#4777B8` (France), GBR `#C84B4B` (Britain), PRU `#8A6A4A` (Prussia), RUS `#8EBE6F` (Russia), AUS `#E6E4DD` (Austria), USA `#9DCBE7`, MEX `#4E9662`, BRA `#3E9B57`, JAP `#B83A42`, SWE `#79C6D5`, and LCO `#E58A32`. Supplementary modeled countries have explicit entries in `country_palette.cpp`. Subject countries resolve recursively to their final `overlordKey` color; every target must exist and the relation graph must be acyclic. Austria's light gray requires a dark boundary stroke.
To regenerate the checked-in reviewed geometry, run `python tools/build_reviewed_geometry.py` from the repository root (or `tools/build_reviewed_geometry.ps1` on Windows). The CMake target `reviewed_geometry` runs the same command and is a dependency of `YehenalaCore`; it uses the checked-in 10m admin-1 and water inputs, so a normal build cannot embed a stale reviewed map. The generator fails on invalid/empty regions, any positive-area overlap between reviewed features, incomplete or duplicated Chinese, metropolitan French, Russian, Belarusian, Ukrainian, British Indian, or United States source assignments, a source-union mismatch, or a filled Caspian/1880 Aral water point.

The frozen political relation set is: Britain -> South Africa, India, Canada, Australia; France -> Algeria, Indochina, West Africa; Turkey -> Egypt; Russia -> Finland; Sweden -> Norway; Prussia -> Central Africa; Low Countries -> East Indies. These relations affect political ownership and color only, not fiscal, military, or AI behavior.

Country-overview flags follow scenario-period designs: the 1862-1889 triangular Qing Yellow Dragon Flag, British Raj Red Ensign, 1803-1892 Kingdom of Prussia flag, Russian Empire black-yellow-white tricolor, Habsburg black-gold bicolor, and the green-white-blue design commonly attributed to the Confederation of the Rhine. The last design is explicitly an attributed or alleged flag because no securely attested official national flag of the Confederation is known.

### Geography acceptance descriptions

The following descriptions are the source contract for map editing and review:

- **Taiwan / east China:** treat Taiwan at approximately 120-122.1E / 21.9-25.3N and Penghu as an independent `MultiPolygon` part of `east_china`; never connect it to the mainland with a straight segment. High-resolution data may add Kinmen and Matsu. Acceptance points are Taipei, Taichung, and Kaohsiung.
- **China mainland:** build Northeast, North, East, South, and Northwest China by dissolving complete Natural Earth 10m admin-1 districts. Every CHN source district must appear exactly once; macro-province borders follow the source administrative seams, with no hand-cut longitude/latitude lines.
- **France:** build North and South France from all 96 Natural Earth 10m metropolitan departments. North France contains Bretagne, Normandie, Pays de la Loire, Hauts-de-France, Grand Est, Ile-de-France, Centre-Val de Loire, and Bourgogne-Franche-Comte; South France contains Nouvelle-Aquitaine, Occitanie, Auvergne-Rhone-Alpes, Provence-Alpes-Cote-d'Azur, and Corse. The two regions meet only on source administrative seams, and the five overseas departments are excluded.
- **England:** if the name remains England, use only southern Great Britain: the northern edge follows the Scotland border, Wales is excluded, and the Isle of Wight and Isles of Scilly remain parts. Scotland and Wales are separate provinces. Ireland contains no British-island geometry. If the province is not split, rename it Great Britain.
- **Punjab:** model the complete northwestern British India block by dissolving every modern Pakistani admin-1 district plus Indian Punjab, Chandigarh, Haryana, Himachal Pradesh, Jammu and Kashmir, Ladakh, and Rajasthan. It includes Lahore, Amritsar, Karachi, Islamabad, Peshawar, Quetta, Jaipur, and Srinagar. Delhi, Uttarakhand, Uttar Pradesh, and Gujarat remain outside Punjab.
- **British India:** the three Indian macro-provinces together cover every Natural Earth admin-1 district in modern India and Pakistan exactly once. Punjab owns the complete northwestern block above; South India contains the remaining Indian South/West regions and Andaman and Nicobar; North India contains every remaining northern, central, eastern, and northeastern Indian district. Bangladesh is excluded.
- **United States:** dissolve all 50 states and the District of Columbia into the five scenario macro-provinces. The historical Thirteen States group follows the original-state footprint and also includes Maine, Vermont, and Washington, D.C.; every other state is assigned exactly once to the Southern States, Great Lakes, Great Plains, or West Coast group. Alaska and Hawaii remain discontinuous West Coast parts.
- **Central Asian steppe:** cover the selected-year Russian Kazakh steppe and Turkestan. The west follows the eastern Caspian shore, the south the Persian and Afghan borders, the east the Tien Shan and Chinese border, and the north the Siberian forest-steppe line. The Caspian and Aral Seas are explicit water holes; Persia, Afghanistan, and Xinjiang are excluded.
- **Prussia:** draw discontinuous territory: Rhineland/Westphalia and western enclaves, then Brandenburg, Pomerania, Silesia, Posen, and East/West Prussia. Follow Dutch, Belgian, Danish, Baltic, German-state, and Russian-Polish borders. Do not replace these components with one belt across northern Germany and Poland.
- **Congress Poland:** because the province is Russian-owned, draw Congress Poland rather than modern Poland: Warsaw, Lodz, Lublin, and the Vistula basin; north-west touches Prussia, south touches Austrian Galicia, and east touches Russia proper. Exclude Posen, East/West Prussia, Galicia, and Vilnius.
- **Austria-Hungary and Italy:** use the post-1867 name only. Lombardy and Venetia belong to Italy. Austria retains South Tyrol/Trentino, Trieste, Istria, Dalmatia, Bohemia, Moravia, Galicia, Bukovina, Hungary, Slovakia, Transylvania, and Croatia. Italy is the boot-shaped peninsula plus Sardinia and Sicily, with Austrian territory excluded and no rectangular islands or Adriatic bridge polygons.
- **Ottoman Turkey:** use a date-consistent name. The homeland consists of Anatolia, East Thrace, and selected directly ruled Balkan, Levantine, and Mesopotamian parts as separate coastal/border-following `MultiPolygon` components. Exclude Greece, Persia, Russian Caucasus, and Austria-Hungary; never fill the Black, Aegean, or Mediterranean seas.
- **Egypt:** include the Nile Delta, Nile Valley, and Sinai. If Khedivate Sudan is modeled, keep Sudan as an independent component/province along the Nile and Red Sea; do not cover the Ethiopian highlands, Somalia, Kenya, or Libya. A puppet keeps its own boundary but resolves to its overlord color.
- **Russia:** the ten macro-regions are North Russia (Kola-White Sea-Arkhangelsk), Central Russia (Moscow, Oka-Volga, and Belarus), Baltic (Gulf of Finland/Riga shore), Ukraine (Dnieper, north Black Sea, Volhynia, and Podolia), Caucasus (between Black and Caspian), Central Asian Steppe, West Siberia, Central Siberia, East Siberia, and Far East (Amur, Ussuri, Primorye, Kamchatka, Chukotka, and selected-year Sakhalin). Austrian Galicia, Bukovina, and Transcarpathia remain outside the Russian Ukraine region. Parts must meet without overlap, holes must remain water, and the Pacific seam must never close across ocean.

The province page body, labels, and building summaries use a shared minimum 14px text contract. Building `+` queues exactly one national project through `World`; `-` immediately demolishes one government/player-owned building, including construction departments. Private buildings cannot be demolished by government. Demolition has no cooldown and does not cancel queued work; cancellation is a separate queue action.

## National construction accounting

1. A project must target a province currently owned by the payer country.
2. Before enqueueing, the model checks treasury minus already reserved construction budget.
3. A successful order reserves the complete requested budget atomically, so separate provinces cannot spend the same funds.
4. Each simulation cycle buys current-cycle construction power and settles actual spending from the reservation. Ready units are installed separately from new purchases. Full-progress projects awaiting private startup cash retry installation even with no supply, exhausted work budget, or a changed price; completed projects release capacity/funds and move to history exactly once. Payment comparisons tolerate only an absolute arithmetic residue of at most 1e-8 currency units.
5. Cancellation releases only the unpaid reservation. Paid construction spending is never refunded.
6. Province building cards are snapshot-driven. `+` queues one unit through the payer country's national queue; `-` demolishes a government-owned unit through the market command API. Both take effect immediately without AI profitability approval. Normal player input is restricted to the controlled country's provinces.
7. Country transfers invalidate affected projects and release their unpaid reservations and capacity. Foreign/orphaned projects cannot continue construction.
8. Government money flows for country-owned provinces use the country treasury. The legacy LocalMarket playerCash path remains only for standalone markets and private compatibility paths.
9. Country deletion is refused while member provinces remain attached; orphaned active projects are blocked and unpaid reservations are released before an empty country is removed.
10. Projects reserve a quoted budget and price ceiling. Each country shares current-cycle industrial construction output and its national base supplement across runnable projects; payments go to the actual provider. Genuine funding or price-limit shortfalls remain visible queue blocks.
11. Bad-debt write-off removes the matching lender-side cash/capital entry instead of recapitalizing a borrower from nowhere. Fiscal-country treasury is included once per member province in local money-supply and price-level accounting.
12. New player orders outrank autonomous expansion. Manual up/down ordering is authoritative for both snapshots and construction allocation; subsequent AI tasks append after that order. Paused projects can be reordered but receive no work, and historical projects are read-only. Invalid identity/type, resource capacity and unavailable funding remain command errors, rather than AI approval decisions.

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
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/united_states.png --map-zoom 2 --map-scroll 250 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/british_india.png --map-zoom 2 --map-scroll 2350 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/pacific_seam.png --map-zoom 2 --map-scroll 3700 --size 1440x900 --frames 3

The generated files are under the ignored `out/` directory. A review must check Taiwan as a separate island part, England/Scotland/Wales separation, Congress Poland and Prussia adjacency, Balkan/Near-East coast and sea holes, the ten Russian macro-regions, all five United States macro-provinces including Alaska/Hawaii, the full modern India + Pakistan British India extent, and no polygon closure across the Pacific seam.

## Source layout

- world*.cpp, country.*, province.*, world_data.*: registries, snapshots, national queues, province markets, and simulation orchestration.
- warehouse*.cpp and local_market*.cpp: logistics lifecycles, weekly local simulation, and standalone compatibility finance.
- building_manager*.cpp: local buildings, private orders, and construction settlement primitives.
- map_model.*, map_layout.*, world_basemap.*: projection, looping, zoom, geometry, and hit testing.
- ui_map.cpp, ui_country.cpp, ui_draw.cpp, ui_input.cpp, ui_navigation.cpp, main.cpp: map shell, country overlay, province submenu, input, navigation, and screenshots.
- ui_construction.cpp: right-side national construction list, reservation metrics, scrolling, and cancellation input.
- debug_ui.cpp, debug_ui.h, debug_report.cpp: full-screen five-market diagnostic UI plus the embedded four-panel LocalMarket desk and headless audit report.

All source files are UTF-8. The selected font is checked through its cmap table before the UI starts.
