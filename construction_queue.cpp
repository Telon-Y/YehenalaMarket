#include "construction_queue.h"

#include "construction_service.h"
#include "country.h"
#include "local_market.h"
#include "world.h"

#include <algorithm>

bool MoveConstructionQueueProject(CountryConstructionState& state,
                                  ConstructionProjectId projectId,
                                  bool up, bool toEdge) {
    std::vector<ConstructionProject*> ordered;
    ordered.reserve(state.projects.size());
    for (ConstructionProject& project : state.projects)
        if (project.live()) ordered.push_back(&project);
    std::sort(ordered.begin(), ordered.end(),
              [](const ConstructionProject* left,
                 const ConstructionProject* right) {
                  return ConstructionQueueOrder{}(*left, *right);
              });
    const auto found = std::find_if(
        ordered.begin(), ordered.end(),
        [projectId](const ConstructionProject* project) {
            return project->id == projectId;
        });
    if (found == ordered.end()) return false;
    const std::size_t index = static_cast<std::size_t>(found - ordered.begin());
    if ((up && index == 0) || (!up && index + 1 == ordered.size()))
        return false;
    const std::size_t destination = toEdge
        ? (up ? 0 : ordered.size() - 1)
        : (up ? index - 1 : index + 1);
    if (up)
        std::rotate(ordered.begin() + destination, found, found + 1);
    else
        std::rotate(found, found + 1, ordered.begin() + destination + 1);

    // A manual position overrides previous numeric priorities. Rebasing all
    // live entries also handles tied/extreme priorities and exhausted sequence
    // values without changing the relative order of any other projects.
    std::uint64_t sequence = 1;
    for (ConstructionProject* project : ordered) {
        project->priority = 0;
        project->sequence = sequence++;
    }
    state.nextSequence = sequence;
    state.manuallyOrdered = true;
    return true;
}

bool ConstructionService::move(int countryId, ConstructionProjectId projectId,
                               bool up, bool toEdge) {
    if (world == nullptr) return false;
    const auto countryIt = world->countryIndexById.find(countryId);
    if (countryIt == world->countryIndexById.end()) return false;
    Country& country = *world->countries[static_cast<std::size_t>(
        countryIt->second)];
    CountryConstructionState& state = country.constructionState;
    const auto project = std::find_if(
        state.projects.begin(), state.projects.end(),
        [projectId, countryId](const ConstructionProject& candidate) {
            return candidate.id == projectId &&
                   candidate.payerCountryId == countryId && candidate.live();
        });
    if (project == state.projects.end()) return false;
    if (!MoveConstructionQueueProject(state, projectId, up, toEdge))
        return false;
    const int cycle = world->getProvinceById(
        project->targetProvinceId).getLocalMarket().getStepCount();
    state.ledger.push_back({cycle, projectId,
        ConstructionLedgerKind::PriorityChanged, project->targetProvinceId,
        Money(project->priority), Money(static_cast<long double>(project->sequence))});
    return true;
}

bool StandaloneConstructionService::move(
    LocalMarket& market, CountryConstructionState& state,
    ConstructionProjectId projectId, bool up, bool toEdge) {
    const auto project = std::find_if(
        state.projects.begin(), state.projects.end(),
        [projectId](const ConstructionProject& candidate) {
            return candidate.id == projectId && candidate.live();
        });
    if (project == state.projects.end()) return false;
    if (!MoveConstructionQueueProject(state, projectId, up, toEdge))
        return false;
    state.ledger.push_back({market.getStepCount(), projectId,
        ConstructionLedgerKind::PriorityChanged, project->targetProvinceId,
        Money(project->priority), Money(static_cast<long double>(project->sequence))});
    return true;
}
