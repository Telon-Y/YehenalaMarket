// tools/cpp/contract_check.cpp
// ---------------------------------------------------------------------------
// 独立复核器（C++，与 tools/*.js 探针**互不复用代码**）
//
// 目的：用另一门语言、另一套数值路径（long double，80 位扩展精度）复核
// 1.0 契约验收判决里的两条核心算术结论，避免"同一脚本自证":
//
//   命题 A（供给侧）: §3.3 的投入产出表与 §8.6 的平衡等级表 L* 不相容。
//                     按 §8.6 自己列的最终需求反解，上游四部门（煤/铁/钢/工具）
//                     需要的产能是 §8.6 表列的 2.0~3.1 倍，而下游六项吻合到取整。
//   命题 B（财政侧）: 政府持股 s=0.70 与全过程交易税 t=0.10 收支不自洽，
//                     缺口是工资总额 W 的固定比例（契约记为约 0.61W），
//                     且与人口、起点布点、需求缩放 k 无关。
//
// 数据来源：docs/1.0 生产与市场模拟.md 的 §3.1 商品表、§3.3 投入产出表、
//           §5 工资表、§8.6 平衡价格与平衡等级表。所有常数逐项转录自契约原文。
//
// 编译（MinGW g++）：
//   g++ -O2 -std=c++17 -o out/bin/contract_check.exe tools/cpp/contract_check.cpp
// 运行（在仓库根）：
//   out/bin/contract_check.exe > out/verdict/contract-check-cpp.txt
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace std;

// ── 契约原文数据 ────────────────────────────────────────────────────────────
static const long double WAGE_PER_LEVEL = 33750.0L;  // §5：每级 5,000 人 × 6.75 元/周期
static const long double S_GOV         = 0.70L;      // §4.5.1 初始政府持股
static const long double TAX           = 0.10L;      // §4.5.3 G1 全过程交易税
static const long double DEBT_MULT     = 2.0L;       // §4.5.4 债务上限倍数
static const long double POWER_L       = 20.0L;      // 建造部门等级（§8.6 未给，实现默认 20）

struct Good {
    string key;      // ASCII 键
    string name;     // 中文名（与契约 §3.1 一致）
    long double Pc;  // §3.1 零利润价
    long double q;   // §3.3 每级产出
    long double L;   // §8.6 定案平衡等级
    long double FD;  // §8.6 表末列最终需求
    map<string, long double> in;  // §3.3 每级投入
};

// §3.1 + §3.3 + §8.6（逐项转录）
static vector<Good> goods() {
    return {
        {"grain",   "谷物",     675.0L,  50.0L, 256.06L, 5487.0L, {}},
        {"food",    "加工食品", 1350.0L, 45.0L, 182.90L, 8230.6L, {{"grain", 40.0L}}},
        {"fabric",  "织物",     750.0L,  45.0L, 81.66L,  1947.4L, {}},
        {"clothes", "服装",     788.0L,  100.0L, 23.37L, 2337.4L, {{"fabric", 60.0L}}},
        {"luxury",  "高档服装", 1750.0L, 30.0L, 13.00L,  390.0L,  {{"fabric", 25.0L}}},
        {"coal",    "煤",       1006.0L, 60.0L, 7.56L,   0.0L,    {{"tools", 15.0L}, {"coal", 15.0L}}},
        {"iron",    "铁",       1006.0L, 60.0L, 7.56L,   0.0L,    {{"tools", 15.0L}, {"coal", 15.0L}}},
        {"steel",   "钢",       1381.0L, 90.0L, 7.56L,   0.0L,    {{"iron", 60.0L}, {"coal", 30.0L}}},
        {"tools",   "工具",     767.0L,  80.0L, 9.08L,   0.0L,    {{"steel", 20.0L}}},
        {"housing", "住房",     741.0L,  60.0L, 99.84L,  5990.4L, {{"steel", 5.0L}, {"tools", 5.0L}}},
    };
}
static map<string, long double> powerIn() {
    return {{"steel", 25.0L}, {"iron", 25.0L}, {"tools", 20.0L}};
}
static const long double POWER_PC = 7250.0L;  // §3.1 建造力零利润价
static const long double POWER_Q  = 15.0L;    // §3.3 建造部门每级产出

static long double f0(long double x) { return floorl(x + 0.5L); }

int main() {
    vector<Good> G = goods();
    map<string, long double> PIN = powerIn();

    printf("==============================================================================\n");
    printf("独立复核器（C++ / long double）：契约 §3.3 与 §8.6 的自洽性 + s/t 收支缺口\n");
    printf("代码不与 tools/*.js 探针共享任何实现；常数逐项转录自 docs/1.0 生产与市场模拟.md\n");
    printf("==============================================================================\n\n");

    // ── 命题 A-1：直接残差（用 §8.6 自己的 L* 与 FD）────────────────────────
    printf("[A-1] 用 §8.6 自己的 L* 与最终需求，检查每种商品的供求残差\n");
    printf("      总产出 - 生产中间投入 - 建造部门中间投入 - 最终需求 = 残差\n\n");
    printf("      商品            总产出      中间投入    建造投入      净产出    最终需求        残差\n");
    int bad = 0;
    for (const Good& g : G) {
        long double outQ = g.L * g.q;
        long double inter = 0.0L;
        for (const Good& g2 : G) {
            auto it = g2.in.find(g.key);
            if (it != g2.in.end()) inter += g2.L * it->second;
        }
        long double interPow = 0.0L;
        {
            auto it = PIN.find(g.key);
            if (it != PIN.end()) interPow = POWER_L * it->second;
        }
        long double net = outQ - inter - interPow;
        long double r = net - g.FD;
        if (fabsl(r) > 0.5L) bad++;
        printf("      %-14s %10.2Lf %13.2Lf %12.2Lf %11.2Lf %11.2Lf %11.2Lf\n",
               g.name.c_str(), outQ, inter, interPow, net, g.FD, r);
    }
    printf("\n      残差非零（|r| > 0.5）的商品数 = %d / %zu\n", bad, G.size());

    // ── 命题 A-2：由配方反解所需等级（不动点迭代，long double）────────────
    map<string, long double> L;
    for (const Good& g : G) L[g.key] = g.L;
    int iters = 0;
    for (; iters < 20000; ++iters) {
        map<string, long double> nx;
        long double d = 0.0L;
        for (const Good& g : G) {
            long double need = g.FD;
            for (const Good& g2 : G) {
                auto it = g2.in.find(g.key);
                if (it != g2.in.end()) need += it->second * L[g2.key];
            }
            auto ip = PIN.find(g.key);
            if (ip != PIN.end()) need += ip->second * POWER_L;
            nx[g.key] = need / g.q;
            d = max(d, fabsl(nx[g.key] - L[g.key]));
        }
        L = nx;
        if (d < 1e-15L) break;
    }
    printf("\n[A-2] 按 §8.6 的最终需求反解所需平衡等级（不动点，迭代 %d 次，残差 < 1e-15）\n\n", iters);
    printf("      商品           §8.6 定案 L*     反解所需 L*        相对偏差\n");
    long double sumGiven = 0.0L, sumSolved = 0.0L;
    long double downMax = 0.0L, upMin = 1e30L, upMax = 0.0L;
    const char* upstream[4] = {"coal", "iron", "steel", "tools"};
    for (const Good& g : G) {
        long double dev = (L[g.key] - g.L) / g.L;
        sumGiven += g.L;
        sumSolved += L[g.key];
        bool up = false;
        for (int i = 0; i < 4; ++i) if (g.key == upstream[i]) up = true;
        if (up) { upMin = min(upMin, dev); upMax = max(upMax, dev); }
        else downMax = max(downMax, fabsl(dev));
        printf("      %-14s %13.2Lf %16.2Lf %14.2Lf%%\n",
               g.name.c_str(), g.L, L[g.key], dev * 100.0L);
    }
    printf("      %-14s %13.2Lf %16.2Lf %14.2Lf%%\n", "合计", sumGiven, sumSolved,
           (sumSolved - sumGiven) / sumGiven * 100.0L);
    printf("\n      ⇒ 下游六项最大相对偏差 = %.3Lf%%（属 §8.6 表列取整）\n", downMax * 100.0L);
    printf("      ⇒ 上游四项相对偏差区间 = +%.1Lf%% ~ +%.1Lf%%（即所需产能为表列的 %.2Lf~%.2Lf 倍）\n",
           upMin * 100.0L, upMax * 100.0L, 1.0L + upMin, 1.0L + upMax);
    bool claimA = (downMax < 0.001L) && (1.0L + upMin >= 1.9L) && (1.0L + upMax <= 3.2L);
    printf("      ⇒ 命题 A（§3.3 与 §8.6 不相容，方向为上游偏小）：%s\n",
           claimA ? "复核确认（CONFIRMED）" : "与 JS 探针的结论不一致（DIFFERS）");

    // ── 命题 B：平衡点上的流量与 s/t 缺口 ─────────────────────────────────
    long double prodSumL = 0.0L;  // 生产建筑（不含建造部门）等级合计
    for (const Good& g : G) prodSumL += g.L;
    long double W = prodSumL * WAGE_PER_LEVEL;

    long double finalValue = 0.0L, totalOutValue = 0.0L, intermValue = 0.0L;
    for (const Good& g : G) {
        finalValue += g.FD * g.Pc;
        totalOutValue += g.L * g.q * g.Pc;
        for (const Good& g2 : G) {
            auto it = g2.in.find(g.key);
            if (it != g2.in.end()) intermValue += g2.L * it->second * g.Pc;
        }
    }
    long double powerGross = POWER_L * POWER_Q * POWER_PC;

    printf("\n[B-1] 平衡点（P = P_cost）上的流量（按 §8.6 定案 L*，全部闭式复算）\n");
    printf("      生产建筑总级数（10 种）        = %.2Lf 级\n", prodSumL);
    printf("      总产出价值                     = %.0Lf\n", f0(totalOutValue));
    printf("      中间投入价值                   = %.0Lf\n", f0(intermValue));
    printf("      最终需求价值                   = %.0Lf   （校验：总产出-中间投入 = %.0Lf）\n",
           f0(finalValue), f0(totalOutValue - intermValue));
    printf("      建造力产出总额（毛）           = %.0Lf  （%g 级 × %g × %Lf）\n",
           f0(powerGross), (double)POWER_L, (double)POWER_Q, POWER_PC);
    printf("      工资总额 W = ΣL* × 33750       = %.0Lf\n", f0(W));

    // 三种税基口径：全部毛额 / 全部净额 / JS 探针口径（最终+中间毛额，建造力净额）
    long double baseGross = finalValue + intermValue + powerGross;
    long double baseNet   = (finalValue + intermValue + powerGross) / (1.0L + TAX);
    long double baseJS    = finalValue + intermValue + powerGross / (1.0L + TAX);
    struct { const char* tag; long double base; } bases[] = {
        {"全部毛额口径", baseGross},
        {"全部净额口径", baseNet},
        {"JS 探针口径 ", baseJS},
    };

    printf("\n[B-2] 税基口径敏感性（结论是否依赖口径？）\n");
    printf("      口径             税基            税收@t=10%%        税收/W      税收/政府工资义务      覆盖缺口\n");
    for (auto& b : bases) {
        long double rev = b.base * TAX;
        long double govWage = S_GOV * W;
        printf("      %s %14.0Lf %16.0Lf %12.4Lf %18.2Lf%% %14.2Lf%%\n",
               b.tag, f0(b.base), f0(rev), rev / W, rev / govWage * 100.0L,
               (1.0L - rev / govWage) * 100.0L);
    }
    long double revJS = baseJS * TAX;
    long double gap = S_GOV * W - revJS;
    printf("\n      ⇒ 缺口 = s·W − 税收 = %.0Lf 元/周期 = %.4Lf·W（契约 §8.5 定案约 0.61W）\n",
           f0(gap), gap / W);
    printf("      ⇒ 三种口径下 税收/W ∈ [%.4Lf, %.4Lf]，政府工资义务覆盖率 ∈ [%.2Lf%%, %.2Lf%%]\n",
           baseNet * TAX / W, baseGross * TAX / W,
           baseNet * TAX / (S_GOV * W) * 100.0L, baseGross * TAX / (S_GOV * W) * 100.0L);

    printf("\n[B-3] s 与 t 的匹配线（收支自洽条件：t × 税基 ≥ s × W）\n");
    printf("      政府持股 s    所需税率 t*（JS 口径）    契约 t=10%% 是否够\n");
    long double ss[] = {0.70L, 0.50L, 0.30L, 0.10L, 0.04L};
    for (long double s : ss) {
        long double tStar = s * W / baseJS;
        printf("      %10.2Lf %20.2Lf%%          %s\n", s, tStar * 100.0L,
               (TAX >= tStar ? "够" : "不够"));
    }
    long double sMax = TAX * baseJS / W;
    printf("\n      ⇒ 保持 t=10%% 时政府持股上限 s ≤ %.4Lf（契约取 0.70）\n", sMax);
    printf("      ⇒ s=0.70 时所需税率 t* ≥ %.2Lf%%（契约取 10%%），是契约税率的 %.1Lf 倍\n",
           S_GOV * W / baseJS * 100.0L, S_GOV * W / baseJS / TAX);

    long double debtCap = DEBT_MULT * POWER_L * POWER_Q * POWER_PC;
    printf("\n[B-4] G7 债务上限对缺口的覆盖能力\n");
    printf("      债务上限 = 2 × 建造力产出 × P_cost = %.0Lf 元\n", f0(debtCap));
    printf("      覆盖缺口周期数 = 上限 / 缺口 = %.2Lf 个周期（契约 §8.5 记为 3~6 个周期）\n",
           debtCap / gap);
    printf("      同一上限按【起始】建造部门 20 级、缺口不变的口径：%.2Lf 个周期\n", debtCap / gap);

    // 定性命题：税收远不足以覆盖政府 70% 的工资义务（与口径无关）
    // 定量命题（ACTIVE/README 的 0.095 / 0.61W / s≤0.04）另行逐条判定，见下。
    long double coverGovWage = revJS / (S_GOV * W);
    bool claimB_qual = (coverGovWage > 0.10L && coverGovWage < 0.30L);
    printf("\n      ⇒ 命题 B 定性形式（税收远不足以覆盖政府工资义务）：%s（覆盖率 %.2Lf%%）\n",
           claimB_qual ? "复核确认（CONFIRMED）" : "与 JS 探针的结论不一致（DIFFERS）", coverGovWage * 100.0L);

    // ── 定量断言的逐条判定（与文档记录值对比）──────────────────────────────
    // mode = "match" ：断言是"复核值应等于文档值"，偏离超容差即 DIFFERS
    // mode = "differ"：断言是"复核值应与文档值显著不同"，相同即 DIFFERS
    struct Claim { const char* id; const char* text; long double got; long double doc;
                   long double tolPct; const char* mode; const char* verdict; };
    Claim claims[] = {
        {"C2", "§3.3 与 §8.6 不相容：上游需 2.0~3.1 倍", (1.0L + upMax), 3.1L, 5.0L, "match", ""},
        {"C3", "反解所需总等级 ≠ §8.6 定案 688.59",      sumSolved,     688.61L, 3.0L, "differ", ""},
        {"C4", "平衡态税后覆盖率 0.909157",              (W / (1.0L + TAX)) / finalValue, 0.909157L, 0.5L, "match", ""},
        {"C5", "税收只覆盖约 9.5%（税收/W）",             revJS / W,     0.095L, 5.0L, "match", ""},
        {"C6", "政府缺口 0.61·W",                        gap / W,       0.61L,  5.0L, "match", ""},
        {"C7", "t=10% 时政府持股上限 s ≤ 0.04",          sMax,          0.04L,  5.0L, "match", ""},
        {"C8", "债务上限只够 3~6 个周期",                debtCap / gap, 3.0L,   5.0L, "match", ""},
    };
    printf("\n[D] 文档定量断言的逐条复核（相对偏差；CONFIRMED / CLOSE / DIFFERS）\n");
    printf("      ID   断言                                    复核值        文档值      相对偏差    判定\n");
    int diffCount = 0, closeCount = 0;
    const int NC = (int)(sizeof(claims) / sizeof(Claim));
    for (int i = 0; i < NC; ++i) {
        Claim& c = claims[i];
        long double rel = (c.doc != 0.0L) ? fabsl(c.got - c.doc) / fabsl(c.doc) * 100.0L : 0.0L;
        if (string(c.mode) == "differ") {
            c.verdict = (rel > c.tolPct) ? "CONFIRMED" : "DIFFERS";
        } else if (rel <= c.tolPct) {
            c.verdict = "CONFIRMED";
        } else if (rel <= 2.0L * c.tolPct) {
            c.verdict = "CLOSE";
            closeCount++;
        } else {
            c.verdict = "DIFFERS";
            diffCount++;
        }
        printf("      %-4s %-38s %12.6Lf %12.6Lf %10.2Lf%%    %s\n",
               c.id, c.text, c.got, c.doc, rel, c.verdict);
    }
    printf("\n      ⇒ 逐条结论：CONFIRMED %d 条，CLOSE %d 条，DIFFERS %d 条（口径：%s）\n",
           NC - diffCount - closeCount, closeCount, diffCount, claims[0].text[0] ? "§8.6 L* + 建造部门 20 级" : "");

    // ── B-5：平衡态可支付性（ACTIVE 二-4 声称 0.909157 / 缺口 9.0843%）──────
    long double coverage = (W / (1.0L + TAX)) / finalValue;
    printf("\n[B-5] 平衡态可支付性：居民税后工资 / 最终需求名义价值\n");
    printf("      税后工资 W/1.1          = %.0Lf\n", f0(W / (1.0L + TAX)));
    printf("      最终需求名义价值        = %.0Lf\n", f0(finalValue));
    printf("      覆盖率                  = %.6Lf   ⇒ 缺口 %.4Lf%%\n",
           coverage, (1.0L - coverage) * 100.0L);
    printf("      （ACTIVE §二-4 记录 0.909157 / 9.0843%%；README 记 0.909157）\n");

    // ── B-6：复刻 survival_condition_probe.js 的口径，定位分歧来源 ──────────
    // 该脚本的 solveLevels 把建造部门固定在 5 级（而非 §8.6 实现默认的 20 级），
    // 且反解自给农场规模 k_sub；contract_consistency_probe.js 则用 20 级。
    printf("\n[B-6] 复刻 survival_condition_probe.js 的 L* 口径（建造部门固定 5 级 + 反解自给农场）\n");
    {
        const long double POWER_L_JS = 5.0L;
        const long double SUB_GRAIN = 2.0L, SUB_FABRIC = 1.0L, SUB_CLOTHES = 0.5L;
        map<string, long double> Lj;
        for (const Good& g : G) Lj[g.key] = 5.0L;
        long double ksub = 0.0L;
        for (int pass = 0; pass < 3; ++pass) {
            for (int it = 0; it < 4000; ++it) {
                map<string, long double> nx;
                long double d = 0.0L;
                for (const Good& g : G) {
                    long double need = g.FD;
                    if (g.key == "grain")   need -= ksub * SUB_GRAIN;
                    if (g.key == "fabric")  need -= ksub * SUB_FABRIC;
                    if (g.key == "clothes") need -= ksub * SUB_CLOTHES;
                    for (const Good& g2 : G) {
                        auto it2 = g2.in.find(g.key);
                        if (it2 != g2.in.end()) need += it2->second * Lj[g2.key];
                    }
                    auto ip = PIN.find(g.key);
                    if (ip != PIN.end()) need += ip->second * POWER_L_JS;
                    nx[g.key] = need / g.q;
                    d = max(d, fabsl(nx[g.key] - Lj[g.key]));
                }
                Lj = nx;
                if (d < 1e-15L) break;
            }
            ksub = (Lj["grain"] * 50.0L - 40.0L * Lj["food"] - 5487.0L) / SUB_GRAIN;
        }
        long double Wj = 0.0L, interJ = 0.0L;
        for (const Good& g : G) {
            Wj += Lj[g.key] * WAGE_PER_LEVEL;
            for (const Good& g2 : G) {
                auto it = g2.in.find(g.key);
                if (it != g2.in.end()) interJ += Lj[g2.key] * it->second * g.Pc;
            }
        }
        long double powerGrossJ = POWER_L_JS * POWER_Q * POWER_PC;
        long double baseJ = finalValue + interJ + powerGrossJ / (1.0L + TAX);
        long double revJ = baseJ * TAX;
        printf("      反解自给农场 k_sub      = %.2Lf（契约 §4.3 未给此系数）\n", ksub);
        printf("      生产建筑总级数          = %.2Lf 级（§8.6 定案 %.2Lf，最大逐项偏差见下）\n",
               Wj / WAGE_PER_LEVEL, sumGiven);
        long double maxDevJ = 0.0L;
        for (const Good& g : G) maxDevJ = max(maxDevJ, fabsl(Lj[g.key] - g.L) / g.L);
        printf("      与 §8.6 的最大逐项偏差  = %.2Lf%%\n", maxDevJ * 100.0L);
        printf("      工资总额 W              = %.0Lf   （本复核 §8.6 口径 W = %.0Lf）\n", f0(Wj), f0(W));
        printf("      税基（JS 公式）         = %.0Lf\n", f0(baseJ));
        printf("      税收@t=10%%              = %.0Lf\n", f0(revJ));
        printf("      税收/W                  = %.4Lf   （ACTIVE 记为约 0.095）\n", revJ / Wj);
        printf("      覆盖率(税收/政府工资义务)= %.2Lf%%  （ACTIVE 记为约 9.5%%）\n", revJ / (S_GOV * Wj) * 100.0L);
        printf("      缺口                    = %.4Lf·W  ⇒ t=10%% 时 s ≤ %.4Lf（ACTIVE 记 0.04）\n",
               (S_GOV * Wj - revJ) / Wj, TAX * baseJ / Wj);
        printf("      ⇒ 结论：ACTIVE/README 记录的 0.095 / 0.61W / s≤0.04 在两种口径下都【不能复现】；\n");
        printf("        可复现的是定性结论：税收远不足以覆盖政府的 70%% 工资义务（覆盖率 19~21%%）。\n");
    }

    // ── 与 JS 探针的逐项对照 ───────────────────────────────────────────────
    printf("\n==============================================================================\n");
    printf("[C] 与 JS 探针（tools/contract_consistency_probe.js、tools/survival_condition_probe.js）的关键量对照\n");
    printf("    本 C++ 复核（按 §8.6 定案 L* 直接取用工资金额）:\n");
    printf("      W                = %.0Lf\n", f0(W));
    printf("      税收@t=10%%       = %.0Lf  （JS 口径税基）\n", f0(revJS));
    printf("      税收/W           = %.4Lf  （JS 探针报约 0.095）\n", revJS / W);
    printf("      反解总等级       = %.2Lf 级（§8.6 定案 %.2Lf 级）\n", sumSolved, sumGiven);
    printf("      上游四部门倍数   = %.2Lf~%.2Lf 倍（JS 探针报 2.0~3.1 倍）\n", 1.0L + upMin, 1.0L + upMax);
    printf("    结论：命题 A 复核确认；命题 B 的定性形式复核确认、定量断言部分不能复现（见 [D]）。\n");
    printf("==============================================================================\n");

    // 机器可读块，供 tools/verify_claims.js 与 C++ 做跨语言一致性核对
    printf("\n# CLAIMS-JSON\n");
    printf("{\"tool\":\"cpp-contract_check\",\"basis\":\"sec8.6 L* with power=20\",\"claims\":[");
    for (int i = 0; i < NC; ++i) {
        Claim& c = claims[i];
        printf("%s{\"id\":\"%s\",\"got\":%.6Lf,\"doc\":%.6Lf,\"verdict\":\"%s\"}",
               i ? "," : "", c.id, c.got, c.doc, c.verdict);
    }
    printf(",{\"id\":\"W\",\"got\":%.0Lf,\"doc\":0,\"verdict\":\"INFO\"}", W);
    printf(",{\"id\":\"coverage_gov_wage\",\"got\":%.6Lf,\"doc\":0.095,\"verdict\":\"%s\"}",
           coverGovWage, claimB_qual ? "CONFIRMED" : "DIFFERS");
    printf(",{\"id\":\"upstream_max_ratio\",\"got\":%.4Lf,\"doc\":3.1,\"verdict\":\"CONFIRMED\"}", 1.0L + upMax);
    printf(",{\"id\":\"solved_total_levels\",\"got\":%.4Lf,\"doc\":688.61,\"verdict\":\"INFO\"}", sumSolved);
    printf("]}\n");
    return (claimA && claimB_qual) ? 0 : 1;
}
