#include "ui.h"

void NavigateToCountry(UIState* state, int countryId, int provinceId) {
    if (state == nullptr) return;
    if (state->playerCountryId < 0)
        state->playerCountryId = countryId;
    if (countryId != state->playerCountryId) return;
    state->selectedCountryId = state->playerCountryId;
    state->countrySelectedProvinceId = provinceId;
    state->selectedProvinceId = provinceId;
    state->countryTab = 0;
    state->panelConsumesInput = true;
    state->countryProvinceScroll = 0;
    state->provinceReturnView = UIView::CountryOverview;
    state->selectedTransportRouteId = -1;
    state->selectedTransportOrderId = NO_WAREHOUSE_ORDER;
    state->transportShipmentScroll = 0;
    state->transportOrderScroll = 0;
    state->view = UIView::CountryOverview;
}

void NavigateToProvince(UIState* state, int provinceId) {
    if (state == nullptr) return;
    state->provinceReturnView = state->view;
    state->selectedProvinceId = provinceId;
    state->countrySelectedProvinceId = provinceId;
    state->provinceTab = 0;
    state->provincePageScroll.fill(0);
    state->localMarketUI.currentPanel = 0;
    state->localMarketUI.goodsScroll = 0;
    state->localMarketUI.buildingScroll = 0;
    state->localMarketUI.constructionScroll = 0;
    state->localMarketUI.orderScroll = 0;
    state->view = UIView::ProvinceDetail;
}

void NavigateBack(UIState* state) {
    if (state == nullptr) return;
    if (state->view == UIView::CommodityMarket) {
        if (state->selectedGood >= 0) {
            state->selectedGood = -1;
            return;
        }
        state->view = UIView::WorldMap;
        state->panelConsumesInput = false;
        return;
    }
    if (state->view == UIView::ProvinceDetail) {
        state->view = state->provinceReturnView;
        state->hoveredProvinceId = -1;
        state->panelConsumesInput = state->provinceReturnView != UIView::WorldMap;
        return;
    }
    if (state->view == UIView::CountryOverview ||
        state->view == UIView::NationalMarket ||
        state->view == UIView::TransportLogistics) {
        state->view = UIView::WorldMap;
        state->hoveredProvinceId = -1;
    }
        state->panelConsumesInput = false;
}
