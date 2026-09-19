#pragma once

#include "construction.h"

inline const char* ConstructionCommandErrorText(
    ConstructionCommandError error) {
    switch (error) {
    case ConstructionCommandError::None: return "";
    case ConstructionCommandError::InvalidCount: return "数量无效";
    case ConstructionCommandError::InvalidType: return "建筑类型无效";
    case ConstructionCommandError::UnknownCountry: return "未找到国家";
    case ConstructionCommandError::UnknownProvince: return "未找到省份";
    case ConstructionCommandError::WrongCountry:
        return "省份属于其他国家";
    case ConstructionCommandError::FinancialBuilding:
        return "金融建筑不可用于此建设";
    case ConstructionCommandError::CapacityReached:
        return "已达到待建或资源容量上限";
    case ConstructionCommandError::InvalidFunding:
        return "资金账户与项目不匹配";
    case ConstructionCommandError::InvalidOwner:
        return "所有者与资金来源不匹配";
    case ConstructionCommandError::InvalidPrice:
        return "建设价格无效";
    case ConstructionCommandError::InvalidReservation:
        return "预留建设预算无效";
    case ConstructionCommandError::InsufficientFunds:
        return "可用投资资金不足";
    case ConstructionCommandError::InsufficientTreasury:
        return "可用国库资金不足";
    case ConstructionCommandError::UnknownProject:
        return "未找到建设项目";
    case ConstructionCommandError::InvalidProjectState:
        return "建设项目状态无效";
    case ConstructionCommandError::DuplicateRequest:
        return "建设请求已提交";
    case ConstructionCommandError::QueueRejected:
        return "建设服务拒绝了项目";
    }
    return "建设指令失败";
}

inline const char* ConstructionProjectStatusText(
    ConstructionProjectStatus status) {
    switch (status) {
    case ConstructionProjectStatus::Queued: return "排队中";
    case ConstructionProjectStatus::Active: return "进行中";
    case ConstructionProjectStatus::Paused: return "已暂停";
    case ConstructionProjectStatus::Completed: return "已完成";
    case ConstructionProjectStatus::Cancelled: return "已取消";
    case ConstructionProjectStatus::Invalidated: return "已失效";
    }
    return "未知";
}

inline const char* ConstructionBlockReasonText(
    ConstructionBlockReason reason) {
    switch (reason) {
    case ConstructionBlockReason::None: return "";
    case ConstructionBlockReason::NoSupply: return "无建设物资供应";
    case ConstructionBlockReason::NoProvider: return "无符合条件的供应方";
    case ConstructionBlockReason::PriceLimit: return "价格超过上限";
    case ConstructionBlockReason::BudgetLimit: return "预留预算已耗尽";
    case ConstructionBlockReason::FundingDepleted: return "资金账户已耗尽";
    }
    return "已阻塞";
}

inline const char* ConstructionFundingText(ConstructionFundingKind kind) {
    switch (kind) {
    case ConstructionFundingKind::CountryTreasury:
        return "国库";
    case ConstructionFundingKind::ProvinceInvestmentPool:
        return "投资池";
    case ConstructionFundingKind::SandboxTreasury:
        return "沙盒国库";
    }
    return "未知资金来源";
}

inline const char* ConstructionOwnerText(OwnerType owner) {
    switch (owner) {
    case OWNER_GOVERNMENT: return "政府";
    case OWNER_INITIAL: return "初始私人";
    case OWNER_FINANCE: return "金融";
    case OWNER_COUNT: break;
    }
    return "未知所有者";
}

inline bool ConstructionProjectIsLive(ConstructionProjectStatus status) {
    return status == ConstructionProjectStatus::Queued ||
           status == ConstructionProjectStatus::Active ||
           status == ConstructionProjectStatus::Paused;
}
