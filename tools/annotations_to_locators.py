#!/usr/bin/env python3
"""One-off conversion: a song's per-section <annotation row="r"> elements
become song-level <locator row="absolute row"> elements in a <locators>
list before <sections>. A section's absolute start is the sum of the
lengths of the sections before it (length in bars, default 4, times the
song's rowsPerBar, default 16). Commented-out annotations are left alone.
Edits the text in place, so comments and layout survive.

Usage: annotations_to_locators.py song.xml...
"""
import re
import sys


def mask_comments(text):
    """`text` with every comment's characters replaced by spaces, so
    positions stay the same."""
    return re.sub(r"<!--.*?-->", lambda m: " " * len(m.group(0)), text, flags=re.S)


def attribute(tag, name, default):
    m = re.search(r'\b%s="([^"]*)"' % name, tag)
    return int(m.group(1)) if m else default


def convert(text):
    masked = mask_comments(text)
    song = re.search(r"<song\b[^>]*>", masked)
    rows_per_bar = attribute(song.group(0), "rowsPerBar", 16)
    list_match = re.search(r"<(sections|scenes)\b[^>]*>", masked)
    if not list_match:
        return text
    tag = "section" if list_match.group(1) == "sections" else "scene"

    locators = {}
    removals = []
    start = 0
    for section in re.finditer(r"<%s\b([^>]*)>" % tag, masked):
        length = attribute(section.group(1), "length", 4) * rows_per_bar
        if "/" not in re.sub(r'"[^"]*"', "", section.group(1)):
            end = masked.index("</%s>" % tag, section.end())
            for a in re.finditer(r'[ \t]*<annotation\b([^>]*?)(?:/>|>(.*?)</annotation>)[ \t]*\n?',
                                 masked[section.end():end], flags=re.S):
                row = attribute(a.group(1), "row", 0)
                name = text[section.end() + a.start(2):section.end() + a.end(2)] if a.group(2) else ""
                if name:
                    locators[start + row] = name
                removals.append((section.end() + a.start(), section.end() + a.end()))
        start += length

    for lo, hi in reversed(removals):
        text = text[:lo] + text[hi:]
    if not locators:
        return text

    line_start = text.rfind("\n", 0, list_match.start()) + 1
    indent = text[line_start:list_match.start()]
    child = indent + ("  " if len(indent) <= 2 else "    ")
    block = indent + "<locators>\n"
    for row in sorted(locators):
        # Names are copied as they appear in the file - already escaped.
        block += '%s<locator row="%d">%s</locator>\n' % (child, row, locators[row])
    block += indent + "</locators>\n"
    return text[:line_start] + block + text[line_start:]


def main():
    for path in sys.argv[1:]:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        converted = convert(text)
        if converted != text:
            with open(path, "w", encoding="utf-8") as f:
                f.write(converted)
            print("converted", path)


if __name__ == "__main__":
    main()
