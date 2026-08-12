// decimal.h
#pragma once

#include <cmath>
#include <string>
#include <algorithm>
#include <cstdio>

// 高精度数值类型：有效数字 + 指数（科学计数法）
class Decimal {
public:
    // 构造函数
    Decimal() : mant(0.0L), exp(0) {}
    Decimal(double v) : mant((long double)v), exp(0) { normalize(); }
    Decimal(long double v) : mant(v), exp(0) { normalize(); }   // 新增：支持 long double
    Decimal(long double m, int e) : mant(m), exp(e) { normalize(); }
    Decimal(int v) : mant((long double)v), exp(0) { normalize(); }

    // 四则运算
    Decimal operator+(const Decimal& o) const {
        if (exp == o.exp) return Decimal(mant + o.mant, exp);
        int e = std::max(exp, o.exp);
        long double m1 = mant * pow10(exp - e);
        long double m2 = o.mant * pow10(o.exp - e);
        return Decimal(m1 + m2, e);
    }
    Decimal operator-(const Decimal& o) const {
        return *this + Decimal(-o.mant, o.exp);
    }
    Decimal operator*(const Decimal& o) const {
        return Decimal(mant * o.mant, exp + o.exp);
    }
    Decimal operator/(const Decimal& o) const {
        if (o.mant == 0.0L) return Decimal(0, 0); // 除零保护，返回0
        return Decimal(mant / o.mant, exp - o.exp);
    }

    // 复合赋值
    Decimal& operator+=(const Decimal& o) { *this = *this + o; return *this; }
    Decimal& operator-=(const Decimal& o) { *this = *this - o; return *this; }
    Decimal& operator*=(const Decimal& o) { *this = *this * o; return *this; }
    Decimal& operator/=(const Decimal& o) { *this = *this / o; return *this; }

    // 比较运算符
    bool operator==(const Decimal& o) const { return mant == o.mant && exp == o.exp; }
    bool operator!=(const Decimal& o) const { return !(*this == o); }
    bool operator<(const Decimal& o) const {
        if (mant == 0) return o.mant > 0;
        if (o.mant == 0) return mant < 0;
        if (mant < 0 && o.mant > 0) return true;
        if (mant > 0 && o.mant < 0) return false;
        // 同号时比较指数和尾数
        if (exp != o.exp) {
            return (mant > 0) ? (exp < o.exp) : (exp > o.exp);
        }
        return mant < o.mant;
    }
    bool operator<=(const Decimal& o) const { return (*this < o) || (*this == o); }
    bool operator>(const Decimal& o) const { return !(*this <= o); }
    bool operator>=(const Decimal& o) const { return !(*this < o); }

    // 一元负号
    Decimal operator-() const { return Decimal(-mant, exp); }

    // 数学辅助
    bool isFinite() const { return std::isfinite(mant); }
    Decimal abs() const { return Decimal(std::fabs(mant), exp); }

    // 转换
    double toDouble() const { return (double)mant * std::pow(10.0, (double)exp); }
    std::string toString(int precision = 10) const {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*Le", precision, mant * std::pow(10.0L, exp));
        return std::string(buf);
    }

private:
    long double mant;
    int exp;

    void normalize() {
        if (mant == 0.0L) { exp = 0; return; }
        while (std::fabs(mant) >= 10.0L) { mant /= 10.0L; exp++; }
        while (std::fabs(mant) < 1.0L)   { mant *= 10.0L; exp--; }
    }

    static long double pow10(int e) {
        return std::pow(10.0L, (long double)e);
    }
};

// 为兼容现有代码，提供全局 clamp 和 isfinite 重载
inline Decimal clamp(const Decimal& x, const Decimal& lo, const Decimal& hi) {
    if (x < lo) return lo;
    if (hi < x) return hi;
    return x;
}
inline bool isfinite(const Decimal& x) { return x.isFinite(); }