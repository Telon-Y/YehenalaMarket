#pragma once

#include "construction.h"
#include "local_market.h"

// Installation is a separate phase from purchasing construction power. A
// finished unit may be waiting for startup cash; retry it even in cycles with
// no provider, no remaining work, or an exhausted construction budget.
template <typename OnStartup, typename OnCompleted>
bool InstallReadyConstructionUnits(ConstructionProject& project,
                                   LocalMarket& market,
                                   OnStartup onStartup,
                                   OnCompleted onCompleted) {
    const Money unitPoints(buildingCost[project.typeIndex]);
    const Money tolerance = Money(1e-9);
    while (project.completedUnits < project.quantity &&
           project.currentUnitProgress + tolerance >= unitPoints) {
        const Money startup = project.startupCapitalPerUnit;
        if (project.funding.kind ==
                ConstructionFundingKind::ProvinceInvestmentPool &&
            !market.capitalizePrivateConstruction(project.typeIndex, startup)) {
            project.blockReason = ConstructionBlockReason::FundingDepleted;
            return false;
        }
        project.reservedStartupCapital = std::max(
            Money(0), project.reservedStartupCapital - startup);
        project.paidStartupCapital += startup;
        if (startup > Money(0)) onStartup(startup);
        market.completeNationalConstruction(
            project.typeIndex, 1, project.owner.type);
        ++project.completedUnits;
        project.currentUnitProgress = std::max(
            Money(0), project.currentUnitProgress - unitPoints);
        onCompleted();
    }
    return true;
}
