#!/usr/bin/env python3
"""
Compare two CARLA benchmark result files and generate a comparison report.

Usage:
    python3 benchmark_compare.py pre_patch.json post_patch.json [--output report.md]
"""

import argparse
import json
import sys


def format_delta(pre, post):
    """Format a delta with percentage change."""
    if pre == 0:
        return f"{post:.1f} (N/A)"
    delta = post - pre
    pct = (delta / pre) * 100
    sign = "+" if delta >= 0 else ""
    return f"{post:.1f} ({sign}{delta:.1f}, {sign}{pct:.1f}%)"


def compare_metric(pre_data, post_data, metric_key, stat="mean"):
    """Compare a single metric between pre and post."""
    pre_val = pre_data.get(metric_key, {}).get(stat)
    post_val = post_data.get(metric_key, {}).get(stat)
    if pre_val is None or post_val is None:
        return None
    return {
        "pre": pre_val,
        "post": post_val,
        "delta": round(post_val - pre_val, 2),
        "delta_pct": round((post_val - pre_val) / pre_val * 100, 2) if pre_val != 0 else None,
    }


def main():
    parser = argparse.ArgumentParser(description="Compare CARLA benchmark results")
    parser.add_argument("pre", help="Pre-patch results JSON")
    parser.add_argument("post", help="Post-patch results JSON")
    parser.add_argument("--output", default="benchmark_comparison.md", help="Output markdown file")
    args = parser.parse_args()

    with open(args.pre) as f:
        pre = json.load(f)
    with open(args.post) as f:
        post = json.load(f)

    pre_label = pre["system"].get("label", "Pre-patch")
    post_label = post["system"].get("label", "Post-patch")

    metrics = [
        ("gpu_util_pct", "GPU Utilization (%)", False),
        ("vram_used_mb", "VRAM Used (MB)", False),
        ("cpu_util_pct", "CPU Utilization (%)", False),
        ("ram_used_mb", "System RAM Used (MB)", False),
        ("server_rss_mb", "Server RSS (MB)", False),
        ("server_vms_mb", "Server VMS (MB)", False),
        ("server_cpu_pct", "Server CPU (%)", False),
        ("server_threads", "Server Threads", False),
        ("gpu_temp_c", "GPU Temperature (C)", False),
        ("power_draw_w", "GPU Power Draw (W)", False),
        ("server_fps", "Server FPS", True),
    ]

    lines = []
    lines.append(f"# CARLA Performance Benchmark Comparison")
    lines.append("")
    lines.append(f"## System")
    lines.append("")
    lines.append(f"| Item | Value |")
    lines.append(f"|------|-------|")
    lines.append(f"| GPU | {pre['system'].get('gpu_name', 'N/A')} |")
    lines.append(f"| VRAM | {pre['system'].get('vram_total_mb', 'N/A')} MB |")
    lines.append(f"| RAM | {pre['system'].get('ram_total_mb', 'N/A')} MB |")
    lines.append(f"| CPU | {pre['system'].get('cpu_count_physical', 'N/A')} physical / {pre['system'].get('cpu_count', 'N/A')} logical |")
    lines.append(f"| Map | {pre['system'].get('map', 'N/A')} |")
    lines.append(f"| Pre-patch run | {pre['system'].get('timestamp', 'N/A')} ({pre_label}) |")
    lines.append(f"| Post-patch run | {post['system'].get('timestamp', 'N/A')} ({post_label}) |")
    lines.append("")

    # Per-scenario comparison
    all_scenarios = sorted(set(list(pre.get("scenarios", {}).keys()) + list(post.get("scenarios", {}).keys())))

    for scenario in all_scenarios:
        pre_s = pre.get("scenarios", {}).get(scenario, {})
        post_s = post.get("scenarios", {}).get(scenario, {})

        lines.append(f"## {scenario}")
        lines.append("")
        lines.append(f"| Metric | {pre_label} | {post_label} | Delta | Delta % |")
        lines.append(f"|--------|------------|-------------|-------|---------|")

        for key, label, higher_is_better in metrics:
            cmp = compare_metric(pre_s, post_s, key)
            if cmp is None:
                continue

            delta_str = f"{cmp['delta']:+.1f}"
            pct_str = f"{cmp['delta_pct']:+.1f}%" if cmp["delta_pct"] is not None else "N/A"

            # Indicator: green if improvement, red if regression
            if cmp["delta"] == 0:
                indicator = ""
            elif higher_is_better:
                indicator = " :arrow_up:" if cmp["delta"] > 0 else " :arrow_down:"
            else:
                indicator = " :arrow_down:" if cmp["delta"] < 0 else " :arrow_up:"

            lines.append(
                f"| {label} | {cmp['pre']:.1f} | {cmp['post']:.1f} | {delta_str}{indicator} | {pct_str} |"
            )

        lines.append("")

    # Summary table: key metrics across all scenarios
    lines.append("## Summary: Key Metrics Across All Scenarios")
    lines.append("")

    key_metrics = [
        ("vram_used_mb", "VRAM (MB)"),
        ("ram_used_mb", "RAM (MB)"),
        ("server_rss_mb", "Server RSS (MB)"),
        ("gpu_util_pct", "GPU %"),
        ("cpu_util_pct", "CPU %"),
        ("server_fps", "FPS"),
    ]

    header = "| Scenario |"
    sep = "|----------|"
    for _, label in key_metrics:
        header += f" {label} Pre | {label} Post | {label} Delta |"
        sep += "----:|-----:|------:|"
    lines.append(header)
    lines.append(sep)

    for scenario in all_scenarios:
        pre_s = pre.get("scenarios", {}).get(scenario, {})
        post_s = post.get("scenarios", {}).get(scenario, {})
        row = f"| {scenario} |"
        for key, _ in key_metrics:
            cmp = compare_metric(pre_s, post_s, key)
            if cmp:
                row += f" {cmp['pre']:.0f} | {cmp['post']:.0f} | {cmp['delta']:+.0f} |"
            else:
                row += " - | - | - |"
        lines.append(row)

    lines.append("")

    report = "\n".join(lines)

    with open(args.output, "w") as f:
        f.write(report)

    print(report)
    print(f"\nReport saved to: {args.output}")


if __name__ == "__main__":
    main()
