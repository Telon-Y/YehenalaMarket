// Package num 提供定点数、矩阵与求解器等数值基础。
//
// 设计依据（docs/ACTIVE.md §6.2 实现架构与工程约定）：
//   - 钱 / 数量 / 价格一律用 int64 定点，刻度 1e-3，保证跨平台逐位可复现；
//   - float64 仅用于求解器内部（线性代数、幂运算），进入状态前统一取整回定点；
//   - 禁止把 math.Pow/Exp/Log 放进每个子步的状态更新（跨平台 libm 有 ULP 差异）。
package num

import (
	"errors"
	"fmt"
	"math"
)

// Scale 是定点数的刻度：1 单位 = 1000 个最小刻度。
const Scale int64 = 1000

// Fixed 是定点数（int64，刻度 1e-3）。
type Fixed int64

// FromFloat 把 float64 转为定点数（四舍五入到最近刻度）。
func FromFloat(v float64) Fixed {
	if math.IsNaN(v) || math.IsInf(v, 0) {
		return 0
	}
	return Fixed(math.Round(v * float64(Scale)))
}

// Float 返回 float64 近似值。
func (f Fixed) Float() float64 { return float64(f) / float64(Scale) }

// Int 返回四舍五入后的整数。
func (f Fixed) Int() int64 { return int64(math.Round(float64(f) / float64(Scale))) }

func (f Fixed) String() string { return fmt.Sprintf("%.3f", f.Float()) }

// ErrOverflow 表示定点乘法溢出。
var ErrOverflow = errors.New("num: fixed-point overflow")

// MulDiv 计算 a*b/c，并在运算前检查溢出。
// 参照 ARCHITECTURE §10.1 R6：所有连乘必须走这里，不能直接 a*b*c。
func MulDiv(a, b, c Fixed) (Fixed, error) {
	if c == 0 {
		return 0, errors.New("num: MulDiv division by zero")
	}
	// a*b 用 float64 中间量以避免 int64 溢出，但先做量级检查。
	const maxSafe = int64(1) << 52
	af, bf := float64(a), float64(b)
	if math.Abs(af) > float64(maxSafe) || math.Abs(bf) > float64(maxSafe) {
		return 0, ErrOverflow
	}
	prod := af * bf / float64(Scale) // 保持刻度
	res := prod / float64(c) * float64(Scale)
	if math.IsNaN(res) || math.IsInf(res, 0) || math.Abs(res) > float64(maxSafe) {
		return 0, ErrOverflow
	}
	return FromFloat(res), nil
}

// Matrix 是稠密方阵（行优先）。
type Matrix [][]float64

// NewMatrix 创建 n×n 零矩阵。
func NewMatrix(n int) Matrix {
	m := make(Matrix, n)
	for i := range m {
		m[i] = make([]float64, n)
	}
	return m
}

// MulVec 返回 M·v。
func (m Matrix) MulVec(v []float64) []float64 {
	n := len(m)
	out := make([]float64, n)
	for i := 0; i < n; i++ {
		var s float64
		for j := 0; j < n; j++ {
			s += m[i][j] * v[j]
		}
		out[i] = s
	}
	return out
}

// Invert 用带部分主元的高斯-约当消元求逆矩阵。
// 参照 ARCHITECTURE D10：系统是 11×11 的稀疏小矩阵，手写消元即可，不引入 BLAS。
func Invert(m Matrix) (Matrix, error) {
	n := len(m)
	if n == 0 {
		return nil, errors.New("num: empty matrix")
	}
	aug := make(Matrix, n)
	for i := 0; i < n; i++ {
		if len(m[i]) != n {
			return nil, fmt.Errorf("num: matrix is not square at row %d", i)
		}
		aug[i] = make([]float64, 2*n)
		copy(aug[i], m[i])
		aug[i][n+i] = 1
	}
	for c := 0; c < n; c++ {
		// 选主元
		p := c
		for r := c + 1; r < n; r++ {
			if math.Abs(aug[r][c]) > math.Abs(aug[p][c]) {
				p = r
			}
		}
		if math.Abs(aug[p][c]) < 1e-14 {
			return nil, fmt.Errorf("num: singular matrix at column %d (A 的谱半径 >= 1，零利润价方程无解)", c)
		}
		aug[c], aug[p] = aug[p], aug[c]
		pv := aug[c][c]
		for k := 0; k < 2*n; k++ {
			aug[c][k] /= pv
		}
		for r := 0; r < n; r++ {
			if r == c {
				continue
			}
			f := aug[r][c]
			if f == 0 {
				continue
			}
			for k := 0; k < 2*n; k++ {
				aug[r][k] -= f * aug[c][k]
			}
		}
	}
	out := make(Matrix, n)
	for i := 0; i < n; i++ {
		out[i] = make([]float64, n)
		copy(out[i], aug[i][n:])
	}
	return out, nil
}

// SpectralRadius 用幂法求矩阵的谱半径。
// 契约 §3.4 的可行性判据：谱半径 < 1 才存在正的零利润价。
func SpectralRadius(m Matrix, iters int) float64 {
	n := len(m)
	if n == 0 {
		return 0
	}
	v := make([]float64, n)
	for i := range v {
		v[i] = 1 / math.Sqrt(float64(n))
	}
	var r float64
	for it := 0; it < iters; it++ {
		w := m.MulVec(v)
		var norm float64
		for _, x := range w {
			norm += x * x
		}
		norm = math.Sqrt(norm)
		if norm == 0 {
			return 0
		}
		r = norm
		for i := range w {
			v[i] = w[i] / norm
		}
	}
	return r
}

// Solve 解线性方程组 M·x = b。
func Solve(m Matrix, b []float64) ([]float64, error) {
	inv, err := Invert(m)
	if err != nil {
		return nil, err
	}
	return inv.MulVec(b), nil
}

// Clamp 把 v 限制在 [lo, hi]。
func Clamp(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

// LinInterp 在 (x0,y0)-(x1,y1) 上线性插值；x0==x1 时返回 y0。
func LinInterp(x, x0, y0, x1, y1 float64) float64 {
	if x1 == x0 {
		return y0
	}
	t := (x - x0) / (x1 - x0)
	return y0 + (y1-y0)*t
}
