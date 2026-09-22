#!/usr/bin/env python3
"""
generate_changelog.py - Generates categorized, clean release notes from Git commits.

Features:
- Dynamically finds previous release tag if not provided.
- Extracts commit range (PREV_TAG..CURR_TAG or PREV_TAG..HEAD).
- Categorizes commits into Features, Bug Fixes, Upstream Sync, Maintenance, etc.
- Filters out CI skips, bot cache commits, and trivial noise.
- Auto-links issue/PR numbers and commit SHAs to GitHub.
- Generates a full compare URL and standard release highlights.
"""

import argparse
import os
import re
import subprocess
import sys

# Ensure UTF-8 output on all systems (especially Windows cp1252)
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")



CATEGORY_CONFIG = [
    {
        "id": "features",
        "title": "🚀 Features & Improvements",
        "patterns": [
            r"^feat(?:ure)?(?:\(.*\))?[:\s]",
            r"^add(?:\(.*\))?[:\s]",
        ]
    },
    {
        "id": "fixes",
        "title": "🐛 Bug Fixes",
        "patterns": [
            r"^fix(?:\(.*\))?[:\s]",
            r"^bug(?:\(.*\))?[:\s]",
            r"^resolved?[:\s]",
        ]
    },
    {
        "id": "sync",
        "title": "🔄 Upstream & Fork Sync",
        "patterns": [
            r"^merge(?:\(.*\))?[:\s]",
            r"^sync(?:\(.*\))?[:\s]",
            r".*sync with upstream.*",
            r".*sync with nixpkgs.*",
        ]
    },
    {
        "id": "maintenance",
        "title": "🛠️ Maintenance & Packaging",
        "patterns": [
            r"^chore(?:\(.*\))?[:\s]",
            r"^ci(?:\(.*\))?[:\s]",
            r"^build(?:\(.*\))?[:\s]",
            r"^refactor(?:\(.*\))?[:\s]",
            r"^perf(?:\(.*\))?[:\s]",
            r"^style(?:\(.*\))?[:\s]",
            r"^test(?:\(.*\))?[:\s]",
            r"^dco\b",
        ]
    },
    {
        "id": "docs",
        "title": "📚 Documentation",
        "patterns": [
            r"^docs?(?:\(.*\))?[:\s]",
        ]
    }
]

IGNORE_PATTERNS = [
    r"\[skip ci\]",
    r"\[ci skip\]",
    r"update releases\.json cache",
    r"^merge branch '.*' into .*",
    r"^merge remote-tracking branch .*",
    r"^(?:bullshit|test|wip|temp|asdf)$",
]


def run_cmd(cmd, cwd=None):
    """Run a shell command and return stdout string."""
    try:
        res = subprocess.run(cmd, shell=True, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=cwd)
        return res.stdout.strip()
    except subprocess.CalledProcessError:
        return ""


def find_previous_tag(curr_tag, cwd=None):
    """Find the latest previous tag before curr_tag using version sort."""
    raw = run_cmd("git tag -l --sort=-v:refname \"v*\"", cwd=cwd)
    if not raw:
        return ""
    tags = [t.strip() for t in raw.splitlines() if t.strip()]
    for t in tags:
        if t != curr_tag:
            return t
    return ""


def is_ignored(subject):
    """Check if the commit subject should be filtered out."""
    sub_lower = subject.lower().strip()
    if len(sub_lower) < 3:
        return True
    for pat in IGNORE_PATTERNS:
        if re.search(pat, sub_lower):
            return True
    return False


def link_prs_and_issues(subject, repo):
    """Convert (#1234) or #1234 to clickable GitHub links."""
    def replace_pr(match):
        prefix = match.group(1) or ""
        pr_num = match.group(2)
        suffix = match.group(3) or ""
        if int(pr_num) >= 4000:
            target_url = f"https://github.com/PrismLauncher/PrismLauncher/pull/{pr_num}"
        else:
            target_url = f"https://github.com/{repo}/issues/{pr_num}"
        return f"{prefix}[#{pr_num}]({target_url}){suffix}"

    subject = re.sub(r"(\()#(\d+)(\))", replace_pr, subject)
    subject = re.sub(r"(^|\s)#(\d+)($|\s)", replace_pr, subject)
    return subject


def categorize_commit(subject):
    """Match subject to a category id."""
    sub_lower = subject.lower().strip()
    for cat in CATEGORY_CONFIG:
        for pat in cat["patterns"]:
            if re.search(pat, sub_lower):
                return cat["id"]
    return "other"


def get_commits(prev_tag, curr_tag, cwd=None):
    """Get list of (full_sha, short_sha, subject) between tags."""
    if prev_tag:
        rev_range = f"{prev_tag}..HEAD"
    else:
        rev_range = "-n 30 HEAD"

    cmd = f'git log {rev_range} --pretty=format:"%H%x09%h%x09%s"'
    output = run_cmd(cmd, cwd=cwd)
    if not output:
        return []

    commits = []
    for line in output.splitlines():
        parts = line.split("\t", 2)
        if len(parts) == 3:
            commits.append((parts[0].strip(), parts[1].strip(), parts[2].strip()))
    return commits


def format_changelog(curr_tag, prev_tag, repo, commits, title_override=None):
    """Build the final Markdown release body."""
    title = title_override or f"Prism Launcher {curr_tag}"
    lines = [f"### {title}", ""]

    if prev_tag:
        lines.append(f"#### Changes since {prev_tag}:")
        compare_link = f"**Full Changelog**: https://github.com/{repo}/compare/{prev_tag}...{curr_tag}"
    else:
        lines.append("#### Recent Changes:")
        compare_link = ""

    lines.append("")

    groups = {cat["id"]: [] for cat in CATEGORY_CONFIG}
    groups["other"] = []

    valid_commit_count = 0
    for full_sha, short_sha, subject in commits:
        if is_ignored(subject):
            continue

        cat_id = categorize_commit(subject)
        linked_subject = link_prs_and_issues(subject, repo)
        commit_link = f"[{short_sha}](https://github.com/{repo}/commit/{full_sha})"
        groups[cat_id].append(f"- {linked_subject} ({commit_link})")
        valid_commit_count += 1

    if valid_commit_count == 0:
        lines.append("- Maintenance updates and stability improvements.")
        lines.append("")
    else:
        for cat in CATEGORY_CONFIG:
            items = groups[cat["id"]]
            if items:
                lines.append(f"##### {cat['title']}")
                lines.extend(items)
                lines.append("")

        if groups["other"]:
            lines.append("##### 📝 Other Changes")
            lines.extend(groups["other"])
            lines.append("")

    if compare_link:
        lines.append(compare_link)
        lines.append("")

    lines.extend([
        "---",
        "#### Highlights & Features:",
        "- **Discord Rich Presence (RPC)**: Native Discord detection with official Minecraft Java game profile (`1402418491272986635`), elapsed game time, instance details, and player avatar status.",
        "- **Updater Bug Fixes**: Accurate version comparison, stripping channel suffixes, and multi-tier Fastly CDN fallback.",
        "- **Offline Account Skin Visibility**: Full skin visibility for offline accounts in singleplayer and LAN worlds with CustomSkinLoader / SkinsRestorer integration.",
        "- **Skin Sharing & Cloud Upload**: Built-in 1-click `/skin` copy and Mineskin/Catbox cloud texture upload.",
        "",
        "**Public Downloads:** Any user can download the installer or portable packages below without needing a GitHub account."
    ])

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate release changelog from git commits.")
    parser.add_argument("--curr-tag", required=True, help="Current version / tag (e.g. v12.0.0.11578)")
    parser.add_argument("--prev-tag", default="", help="Previous tag (auto-detected if empty)")
    parser.add_argument("--repo", default="Towartz/PrismLauncher", help="GitHub repository (owner/repo)")
    parser.add_argument("--title", default="", help="Release title override")
    parser.add_argument("--cwd", default=".", help="Directory to run git commands in")
    parser.add_argument("--output-file", default="", help="Write output to file")
    parser.add_argument("--github-output", default="", help="Append to $GITHUB_OUTPUT file")

    args = parser.parse_args()

    prev_tag = args.prev_tag
    if not prev_tag:
        prev_tag = find_previous_tag(args.curr_tag, cwd=args.cwd)

    commits = get_commits(prev_tag, args.curr_tag, cwd=args.cwd)
    changelog = format_changelog(
        curr_tag=args.curr_tag,
        prev_tag=prev_tag,
        repo=args.repo,
        commits=commits,
        title_override=args.title
    )

    if args.output_file:
        with open(args.output_file, "w", encoding="utf-8") as f:
            f.write(changelog)

    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as f:
            f.write("body<<EOF\n")
            f.write(changelog)
            f.write("\nEOF\n")

    if not args.output_file and not args.github_output:
        print(changelog)


if __name__ == "__main__":
    main()
