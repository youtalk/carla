#!/usr/bin/env python3
"""
Analyze variance benchmark results and generate a Markdown report.

Usage:
    python3 analyze_variance.py <pre.json> <post.json> [--output report.md]
"""

import argparse
import json
import math
import sys
from datetime import datetime


FOCUS_METRICS = ["vram_used_mb", "server_rss_mb", "server_vms_mb", "server_threads"]
EXTRA_METRICS = ["gpu_util_pct", "cpu_util_pct", "server_cpu_pct", "gpu_temp_c", "power_draw_w", "server_fps"]

METRIC_LABELS = {
    "vram_used_mb": "VRAM (MB)",
    "server_rss_mb": "Server RSS (MB)",
    "server_vms_mb": "Server VMS (MB)",
    "server_threads": "Server Threads",
    "gpu_util_pct": "GPU Util (%)",
    "cpu_util_pct": "CPU Util (%)",
    "server_cpu_pct": "Server CPU (%)",
    "gpu_temp_c": "GPU Temp (C)",
    "power_draw_w": "GPU Power (W)",
    "server_fps": "Server FPS",
}

SCENARIO_LABELS = {
    "idle": "Idle (最軽量)",
    "traffic_50v_30w": "Traffic 50v+30w (交通シミュ)",
    "sensors_ego": "Sensors Ego (知覚開発)",
    "combined_30v_20w_sensors": "Combined 30v+20w+sensors (AD パイプライン)",
    "max_stress": "Max Stress (最重量: 夜雨霧+50v+30w+多カメラ)",
}


def stats(values):
    n = len(values)
    if n == 0:
        return None
    mean = sum(values) / n
    var = sum((x - mean) ** 2 for x in values) / (n - 1) if n > 1 else 0
    sd = math.sqrt(var)
    se = sd / math.sqrt(n) if n > 0 else 0
    # 95% CI (t approx for n>=20: ~2.09, use 2.0 as approximation)
    t_crit = 2.093 if n >= 20 else 2.262  # df=19 or df=9
    ci95 = t_crit * se
    values_sorted = sorted(values)
    return {
        "n": n, "mean": mean, "sd": sd, "se": se, "ci95": ci95,
        "min": values_sorted[0], "max": values_sorted[-1],
        "range": values_sorted[-1] - values_sorted[0],
    }


def welch_t_test(s1, s2):
    """Welch's t-test. Returns t-statistic and approximate p-value."""
    n1, n2 = s1["n"], s2["n"]
    m1, m2 = s1["mean"], s2["mean"]
    v1, v2 = s1["sd"] ** 2, s2["sd"] ** 2

    if v1 == 0 and v2 == 0:
        # Both have zero variance
        if m1 == m2:
            return 0.0, 1.0
        else:
            return float('inf'), 0.0

    se = math.sqrt(v1 / n1 + v2 / n2)
    if se == 0:
        return 0.0, 1.0

    t = (m2 - m1) / se

    # Welch-Satterthwaite degrees of freedom
    num = (v1 / n1 + v2 / n2) ** 2
    d1 = (v1 / n1) ** 2 / (n1 - 1) if n1 > 1 and v1 > 0 else 0
    d2 = (v2 / n2) ** 2 / (n2 - 1) if n2 > 1 and v2 > 0 else 0
    denom = d1 + d2
    df = num / denom if denom > 0 else 1

    # Approximate p-value using normal distribution for large df
    # For more accurate: use scipy if available, else approximate
    p = _approx_p_value(abs(t), df)
    return t, p


def _approx_p_value(t_abs, df):
    """Approximate two-tailed p-value for t-distribution."""
    try:
        from scipy.stats import t as t_dist
        return 2 * t_dist.sf(t_abs, df)
    except ImportError:
        pass
    # Fallback: normal approximation (good for df > 30)
    # Using Abramowitz & Stegun approximation for normal CDF
    z = t_abs
    if df < 30:
        # Adjust for t-distribution with small df
        z = t_abs * (1 - 1 / (4 * df))
    # Normal survival function approximation
    p = 2 * _norm_sf(z)
    return p


def _norm_sf(z):
    """Survival function of standard normal (1 - CDF)."""
    # Abramowitz & Stegun 26.2.17
    if z < 0:
        return 1.0 - _norm_sf(-z)
    b0 = 0.2316419
    b1 = 0.319381530
    b2 = -0.356563782
    b3 = 1.781477937
    b4 = -1.821255978
    b5 = 1.330274429
    t = 1.0 / (1.0 + b0 * z)
    phi = math.exp(-z * z / 2) / math.sqrt(2 * math.pi)
    return phi * (b1 * t + b2 * t**2 + b3 * t**3 + b4 * t**4 + b5 * t**5)


def cohens_d(s1, s2):
    """Cohen's d effect size."""
    n1, n2 = s1["n"], s2["n"]
    v1, v2 = s1["sd"] ** 2, s2["sd"] ** 2
    pooled_sd = math.sqrt(((n1 - 1) * v1 + (n2 - 1) * v2) / (n1 + n2 - 2)) if (n1 + n2 > 2) else 1
    if pooled_sd == 0:
        return 0.0 if s1["mean"] == s2["mean"] else float('inf')
    return (s2["mean"] - s1["mean"]) / pooled_sd


def verdict(p, d):
    if p < 0.01:
        return "REAL"
    elif p < 0.05:
        return "BORDERLINE"
    else:
        return "NOISE"


def effect_label(d):
    ad = abs(d)
    if ad < 0.2:
        return "negligible"
    elif ad < 0.5:
        return "small"
    elif ad < 0.8:
        return "medium"
    else:
        return "large"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pre_json", help="Pre-patch results JSON")
    parser.add_argument("post_json", help="Post-patch results JSON")
    parser.add_argument("--output", default="benchmark_variance_report_v3.md")
    args = parser.parse_args()

    with open(args.pre_json) as f:
        pre = json.load(f)
    with open(args.post_json) as f:
        post = json.load(f)

    scenarios = list(pre["scenarios"].keys())
    lines = []

    def w(s=""):
        lines.append(s)

    w("# UE 5.5.4 パッチ性能分散テスト v3 レポート")
    w()
    w("## テスト概要")
    w()
    w("| 項目 | 値 |")
    w("|------|-----|")
    w(f"| マップ | {pre['map']} |")
    w(f"| 反復回数 | {pre['runs']} 回/シナリオ |")
    w(f"| 計測時間 | {pre['duration']} 秒/ラン |")
    w(f"| Pre-patch 実行日時 | {pre['timestamp'][:19]} |")
    w(f"| Post-patch 実行日時 | {post['timestamp'][:19]} |")
    w(f"| シナリオ数 | {len(scenarios)} |")
    w(f"| 手法 | 各ランを独立サブプロセスで実行 |")
    w()

    w("## シナリオ一覧")
    w()
    w("| シナリオ | 内容 |")
    w("|---------|------|")
    for sc in scenarios:
        w(f"| `{sc}` | {SCENARIO_LABELS.get(sc, sc)} |")
    w()

    # ── Stability / restart stats ──
    w("## サーバー安定性")
    w()
    w("| シナリオ | | 成功 | タイムアウト | エラー | 再起動 |")
    w("|---------|--|-----:|----------:|------:|------:|")
    for sc in scenarios:
        for label, data in [("Pre", pre), ("Post", post)]:
            sd = data["scenarios"].get(sc, {})
            success = sd.get("successful_runs", len(sd.get("runs", [])))
            to = sd.get("timeouts", 0)
            err = sd.get("errors", 0)
            rst = sd.get("restarts", 0)
            w(f"| `{sc}` | {label} | {success} | {to} | {err} | {rst} |")
    w()

    # ── Focus metrics detail ──
    w("## フォーカスメトリクス詳細分析")
    w()
    w("判定基準:")
    w("- **REAL**: Welch's t-test p < 0.01")
    w("- **BORDERLINE**: p < 0.05")
    w("- **NOISE**: p >= 0.05")
    w()

    summary_rows = []

    for sc in scenarios:
        pre_v = pre["scenarios"].get(sc, {}).get("variance", {})
        post_v = post["scenarios"].get(sc, {}).get("variance", {})
        pre_runs = pre["scenarios"].get(sc, {}).get("runs", [])
        post_runs = post["scenarios"].get(sc, {}).get("runs", [])

        w(f"### {SCENARIO_LABELS.get(sc, sc)}")
        w()
        w(f"| メトリクス | Pre Mean | Pre SD | Post Mean | Post SD | Delta | p値 | Cohen's d | 効果量 | 判定 |")
        w(f"|-----------|-------:|------:|--------:|------:|------:|----:|--------:|------:|------|")

        for m in FOCUS_METRICS:
            pre_means = [r[m]["mean"] for r in pre_runs if m in r]
            post_means = [r[m]["mean"] for r in post_runs if m in r]

            if not pre_means or not post_means:
                continue

            s1 = stats(pre_means)
            s2 = stats(post_means)
            t_val, p_val = welch_t_test(s1, s2)
            d_val = cohens_d(s1, s2)
            v = verdict(p_val, d_val)
            el = effect_label(d_val)
            delta = s2["mean"] - s1["mean"]
            sign = "+" if delta >= 0 else ""

            w(f"| {METRIC_LABELS.get(m, m)} | {s1['mean']:.1f} | {s1['sd']:.1f} | "
              f"{s2['mean']:.1f} | {s2['sd']:.1f} | {sign}{delta:.1f} | "
              f"{p_val:.4f} | {d_val:.2f} | {el} | **{v}** |")

            summary_rows.append((sc, m, s1, s2, delta, p_val, d_val, v, el))
        w()

    # ── Summary table ──
    w("## 総合判定サマリ")
    w()
    w(f"| シナリオ | メトリクス | Pre | Post | Delta | p値 | 効果量 | 判定 |")
    w(f"|---------|-----------|----:|-----:|------:|----:|------:|------|")
    for sc, m, s1, s2, delta, p_val, d_val, v, el in summary_rows:
        sign = "+" if delta >= 0 else ""
        sc_short = sc.replace("combined_30v_20w_sensors", "combined").replace("traffic_50v_30w", "traffic")
        w(f"| {sc_short} | {METRIC_LABELS.get(m, m)} | {s1['mean']:.0f} | {s2['mean']:.0f} | "
          f"{sign}{delta:.0f} | {p_val:.3f} | {el} | **{v}** |")
    w()

    # ── Extra metrics overview ──
    w("## 補助メトリクス概要")
    w()
    w(f"| シナリオ | メトリクス | Pre Mean | Post Mean | Delta | 判定 |")
    w(f"|---------|-----------|-------:|--------:|------:|------|")
    for sc in scenarios:
        pre_runs = pre["scenarios"].get(sc, {}).get("runs", [])
        post_runs = post["scenarios"].get(sc, {}).get("runs", [])
        for m in EXTRA_METRICS:
            pre_means = [r[m]["mean"] for r in pre_runs if m in r]
            post_means = [r[m]["mean"] for r in post_runs if m in r]
            if not pre_means or not post_means:
                continue
            s1 = stats(pre_means)
            s2 = stats(post_means)
            t_val, p_val = welch_t_test(s1, s2)
            d_val = cohens_d(s1, s2)
            v = verdict(p_val, d_val)
            delta = s2["mean"] - s1["mean"]
            sign = "+" if delta >= 0 else ""
            sc_short = sc.replace("combined_30v_20w_sensors", "combined").replace("traffic_50v_30w", "traffic")
            w(f"| {sc_short} | {METRIC_LABELS.get(m, m)} | {s1['mean']:.1f} | {s2['mean']:.1f} | {sign}{delta:.1f} | **{v}** |")
    w()

    # ── Conclusion placeholder ──
    w("## 定量分析")
    w()
    real_count = sum(1 for r in summary_rows if r[7] == "REAL")
    border_count = sum(1 for r in summary_rows if r[7] == "BORDERLINE")
    noise_count = sum(1 for r in summary_rows if r[7] == "NOISE")
    total = len(summary_rows)
    w(f"- フォーカスメトリクス {total} 件中: REAL={real_count}, BORDERLINE={border_count}, NOISE={noise_count}")
    w()

    # Find significant results
    real_results = [(sc, m, delta, p, d, el) for sc, m, s1, s2, delta, p, d, v, el in summary_rows if v == "REAL"]
    border_results = [(sc, m, delta, p, d, el) for sc, m, s1, s2, delta, p, d, v, el in summary_rows if v == "BORDERLINE"]

    if real_results:
        w("### 統計的に有意な差 (REAL)")
        w()
        for sc, m, delta, p, d, el in real_results:
            sign = "+" if delta >= 0 else ""
            w(f"- **{sc} / {METRIC_LABELS.get(m, m)}**: {sign}{delta:.1f} (p={p:.4f}, d={d:.2f}, {el})")
        w()

    if border_results:
        w("### ボーダーライン (BORDERLINE)")
        w()
        for sc, m, delta, p, d, el in border_results:
            sign = "+" if delta >= 0 else ""
            w(f"- **{sc} / {METRIC_LABELS.get(m, m)}**: {sign}{delta:.1f} (p={p:.4f}, d={d:.2f}, {el})")
        w()

    w("## バージョンアップ推奨の考察")
    w()
    w("*(実験結果を基に手動で追記)*")
    w()

    with open(args.output, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Report saved to: {args.output}")


if __name__ == "__main__":
    main()
