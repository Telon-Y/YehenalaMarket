# Version 2.0 GUI Framework

This document freezes the 2.0 ownership and interface contract.

## 1. Terminology

- The world map is the permanent primary view. It is not a page that is replaced by a full-screen detail view.
- The 2.0 province UI is a snapshot-driven secondary menu inside the country overlay. Its body is the embedded LocalMarket Debug-derived desk with Goods, Buildings, Construction, and Macro panels; the full-screen five-market Debug fixture remains a separate diagnostic mode.
- The country layer is 2.0. There is no separate later management layer in this contract.
- Country and province detail desks are left-side overlays with width equal to two-fifths (40 percent) of the viewport and full usable height.
- The Build list control lives in the upper-right toolbar. When open, the national construction list is a right-side overlay with width equal to two-fifths (40 percent) and full usable height.

## 2. Entity responsibilities

| Entity | Authoritative state and permissions |
| --- | --- |
| Country | Treasury, reserved construction budget, national construction queue, member province IDs, national market membership, and read-only population/GDP aggregation |
| Province | Local population, buildings, employment, local market, local indicators, and the placement location for construction |
| Construction project | Payer country ID/country code, target province, type, quantity, locked unit price, total budget, reserved and paid budget, progress, cycle metadata, and status |
| Player | Commands issued for the currently selected country |
| Private economy | Optional capitalist expansion funded by private pools and kept separate from national projects |

A province never exposes a player cash pool, an independent build budget, or a player-owned build decision. Its building cards are read-only snapshots plus command controls: `+` issues one `QueueNationalConstruction` command and `-` demolishes one government-owned unit of that type. Because `-` is a demolition, not a cancellation, `QueueNationalConstruction` is not the only manual command that reaches the simulation; cancelling a queued or active project is a separate `CancelNationalConstruction` action exposed only in the national construction list. A country aggregate is computed from member province snapshots for one common simulation cycle rather than stored as another writable population or GDP total.

## 3. National construction contract

Two manual commands reach the simulation: `QueueNationalConstruction(country, province, type, count)` and `PlayerDemolish(province, type, count)`. QueueNationalConstruction is the only manual *construction* command; demolition removes existing government-owned units and never creates work.

- The target province must exist and its current country ID must equal the payer country ID.
- Financial buildings and invalid type IDs are rejected.
- Available treasury is treasury minus all active reservations. The full requested budget must be reserved atomically before a project is appended.
- The world cycle purchases current-cycle construction power from national providers, pays from the reservation, updates paid and remaining amounts, and installs completed units at the target province. Installation retries independently of new purchases when startup cash becomes available; terminal projects archive exactly once.
- CancelNationalConstruction releases the project's unpaid reservation only. PaidBudget is retained and never refunded.
- A province transfer invalidates affected projects and releases remaining reservations/capacity. Foreign targets cannot keep receiving construction work.
- Country removal is refused while it still owns provinces; orphaned active projects are blocked and their unpaid reservations are released before an empty country is removed.
- Country-owned local markets use country treasury for government wages, dividends, transaction tax, and construction. Standalone markets retain the LocalMarket playerCash compatibility path.
- Private expansion shares the national construction queue and supply, with a separate province investment funding reference and private ownership. Player commands bypass AI profitability policy and new manual work receives highest queue priority.
- Every live queue row has small round up/down buttons. Click swaps with the adjacent task; left or right Shift moves to first/last. The UI and scheduler use the same priority/sequence/ID comparator. Manual ordering preserves work, funding, selection and scrolling, and subsequent AI tasks append. Boundary and historical controls are disabled.
- Province building `-` immediately demolishes government/player-owned units without a cooldown; private ownership is protected. Demolition and construction cancellation are separate actions. Stale foreign-province UI state is rejected before dispatching player input.

The conservation rule is:

    treasury_after = treasury_before - paid_national_cost + credits_and_taxes
    reserved_after = sum(unpaid_reservation for active projects)

## 4. Read-only snapshots

Simulation code exposes immutable query snapshots:

- CountrySnapshot: country identity and country code, member province IDs, cycle, treasury fields, aggregated population/GDP, and construction project snapshots. The legacy `tag` field mirrors `countryCode` for compatibility.
- ProvinceSnapshot: identity, country, cycle, local population/GDP, employment, market indicators, dependent population, money supply, investment pool, loan capacity, debt, GDP/population history, buildings with queued counts, and population classes.
- ConstructionProjectSnapshot: payer identity, target, type, quantity, locked unit price, budget fields, total/remaining construction, progress, created/settled cycles, and status.

The UI draws only these snapshots and issues commands through World. Drawing functions do not mutate the simulation model.

GDP and population use the same cycle rule as the member province snapshots. If provinces are at different cycles, the country snapshot reports the minimum member cycle so the panel never labels mixed data as one period.

## 5. Navigation and overlay state

The state machine is:

    WorldMap
      -> CountryOverview(country, selectedProvince)
           -> ProvinceDetail(country, selectedProvince, tab)
           -> CountryOverview
      -> WorldMap

CountryOverview has two fixed tabs: Overview and Construction. ProvinceDetail opens the embedded LocalMarket desk with four fixed panels: Goods, Buildings, Construction, and Macro. The desk owns its market, goods, building, construction, and order scroll offsets; its `+`/`-` controls issue national World commands. The building panel uses a paged vertical summary list with a visible position indicator and a fixed selected-building input section; wheel, Up/Down, and Page Up/Page Down navigate the list. Returning from a province preserves country, selected province, country context, and embedded desk state.

The country panel intercepts input inside its left two-fifths rectangle. The responsive province desk intercepts its complete, wider rectangle. Input outside the active rectangle is sent to the map, so hover, province selection, eight-direction mouse-edge scrolling, arrow/WASD panning, middle-button grab dragging, and wheel zoom remain active without penetrating the overlay. A middle-button drag must begin inside the map viewport; after it begins, it owns the pointer until release even if the cursor crosses an overlay. Diagonal pan velocity is normalized, horizontal movement and dragging wrap at the Pacific seam, vertical movement and dragging clamp at the projected world limits, and wheel zoom preserves the cursor's longitude and latitude. The right construction panel applies the same interception rule to its right two-fifths rectangle.

## 6. Layout contract

All overlay rectangles come from the current viewport. Country and national construction desks use viewport width times 0.4 (2/5). ProvinceDetail uses `min(viewportWidth, clamp(viewportWidth * 0.62, 900, 1120))`, giving dense market tables a readable surface while preserving map context on standard desktop widths. It passes those bounds to `DebugLayout::EmbeddedLocalMarket`, which uses a 58-pixel local-market header, compact panel tabs when needed, and shared rectangles for drawing, hit testing, and scrolling. The country flag remains a 3:2 rectangle, and embedded content is clipped to its desk scissor rectangle.

The required regression sizes are 1024x720, 1366x768, 1440x900, and 1920x1080. No label, control, cancellation button, or list row may exceed its parent panel or viewport. Province and country body text uses the shared 14px minimum; titles may be larger. Building summary rows use the same dimensions for drawing, scrolling, and hit testing, and their `+`/`-` controls never overlap the metrics text.

## 7. Historical map and version contract

The active contract is 2.0; no 1.1/1.2 branch or tag is supported. The frozen scenario is `scenarioYear=1880`, `alternateHistory=true`, `scenarioKey=1880_alternate_colonial_relations`. It deliberately combines post-1867 Austria-Hungary, later colonial subjects, and alternate Prussian Central Africa, so it is not a claim about a single real-world year.

The neutral land layer is Natural Earth 1:50m. Final province geometry is generated offline from Natural Earth 10m admin-1 and the checked-in water-hole dataset with `tools/build_reviewed_geometry.py` (Shapely/GEOS dissolve, precision snapping, and explicit source whitelists). It is then stored as `data/reviewed_province_geometry.geojson`; `parts` plus explicit `holes` are the only geometry consumed by drawing, labels, and hit testing. The generator rejects invalid/empty regions, positive-area cross-feature overlaps, incomplete or duplicated Chinese, metropolitan French, Russian, Belarusian, Ukrainian, British Indian, or United States assignments, source-union mismatches, and filled Caspian/1880 Aral water points. Runtime gates still require reviewed topology and basemap land coverage. China's five mainland macro-provinces form a complete 32-district admin-1 partition, Taiwan and its islands belong to east China, and Mongolia uses the same 10m source. Metropolitan France partitions all 96 mainland and Corsican departments between North and South France on regional seams, with overseas departments excluded. British India partitions all modern Indian and Pakistani districts between North India, Punjab, and South India; the United States partitions all 50 states plus Washington, D.C. between five macro-provinces. The central-Asian steppe retains Caspian and Aral holes; England excludes Wales and Scotland; Congress Poland is Russian; Prussia is disconnected; Austria excludes Lombardy/Venetia; Italy retains the boot, Sardinia, and Sicily; Ottoman and Egyptian components follow coastlines and borders. Russia is partitioned into North Russia, Central Russia including Belarus, Baltic, Ukraine including Volhynia and Podolia, Caucasus, Central Asian Steppe, West Siberia, Central Siberia, East Siberia, and Far East; Austrian Galicia and Bukovina remain Austrian. Their authored parts are disjoint and dedicated tests cover core points, water negatives, holes, and the Pacific seam.

The exact tested palette includes CHI `#F2E6A7`, FRA `#4777B8`, GBR `#C84B4B`, PRU `#8A6A4A`, RUS `#8EBE6F`, AUS `#E6E4DD`, USA `#9DCBE7`, MEX `#4E9662`, BRA `#3E9B57`, JAP `#B83A42`, SWE `#79C6D5`, and LCO `#E58A32`. Recursive `overlordKey` resolution maps Britain -> South Africa/India/Canada/Australia, France -> Algeria/Indochina/West Africa, Turkey -> Egypt, Russia -> Finland, Sweden -> Norway, Prussia -> Central Africa, and Low Countries -> East Indies. The graph is validated for existing targets and cycles; these links affect political color/ownership only.

The six scenario-specific country flags are the Qing Yellow Dragon Flag, British Raj Red Ensign, Kingdom of Prussia crowned-eagle flag, Russian Empire black-yellow-white tricolor, Habsburg black-gold bicolor, and the green-white-blue design attributed to the Confederation of the Rhine. The Rhine design is documented as alleged rather than a securely attested official national flag.

The full editing descriptions are kept in the README's “Geography acceptance descriptions” section and are part of this contract: Taiwan (120-122.1E, 21.9-25.3N plus Penghu, no mainland bridge), England (southern Great Britain, Wales/Scotland excluded), Punjab (five-river upper Indus basin), Central Asian Steppe (Caspian/Aral holes and no Persia/Afghanistan/Xinjiang), discontinuous Prussia, Congress Poland, post-1867 Austria-Hungary versus boot-shaped Italy, date-consistent Ottoman components, Nile/Sinai Egypt, and the ten-region Russian partition with no Pacific seam closure.
The reproducible map-data flow is: edit the acceptance whitelist only in the Python generator, run `python tools/build_reviewed_geometry.py` (or `tools/build_reviewed_geometry.ps1`), inspect the GEOS gates, then run the CMake `reviewed_geometry` target before embedding. The target is an order-only dependency of `YehenalaCore`, and the embedded GeoJSON is shared by renderer, labels, and hit testing; no post-draw historical overlay may be introduced.

8. Delivery stages

1. Freeze terminology, the 1880 alternate-history scenario, private-construction policy, GDP cycle, and cancellation rule.
2. Keep country treasury and national project identity authoritative for all player orders.
3. Close the reservation, settlement, cancellation, completion, and transfer loop.
4. Expose country/province/project snapshots; keep displays read-only and route `+`/`-` through World commands.
5. Implement map, country overlay, province submenu, and return context.
6. Verify model invariants, input interception, responsive geometry, topology/water-hole tests, exact RGB colors, minimum font size, and screenshots.

## 9. Acceptance commands

    cmake --build --preset mingw-debug --target reviewed_geometry
    cmake --build --preset mingw-debug
    out/build/mingw-debug/YehenalaMarket.exe --screenshot country.png --view country --tab overview --province north_china --size 1024x720 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot construction.png --view country --tab construction --province north_china --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot construction_panel.png --construction-panel --size 1024x720 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot province.png --view province --province north_china --size 1920x1080 --frames 3

    # Geographic screenshot regression (wrapped map scroll positions)
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/taiwan.png --map-zoom 2 --map-scroll 2250 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/england.png --map-zoom 2 --map-scroll 960 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/poland_balkans.png --map-zoom 2 --map-scroll 1250 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/near_east.png --map-zoom 2 --map-scroll 1450 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/russia.png --map-zoom 2 --map-scroll 2050 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/united_states.png --map-zoom 2 --map-scroll 250 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/british_india.png --map-zoom 2 --map-scroll 2350 --size 1440x900 --frames 3
    out/build/mingw-debug/YehenalaMarket.exe --screenshot out/pacific_seam.png --map-zoom 2 --map-scroll 3700 --size 1440x900 --frames 3
