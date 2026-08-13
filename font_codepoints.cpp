#include "font_codepoints.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <set>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;

bool readU16(const Bytes& data, size_t offset, uint16_t& value) {
    if (offset > data.size() || data.size() - offset < 2) return false;
    value = static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
    return true;
}

bool readU32(const Bytes& data, size_t offset, uint32_t& value) {
    if (offset > data.size() || data.size() - offset < 4) return false;
    value = (static_cast<uint32_t>(data[offset]) << 24) |
            (static_cast<uint32_t>(data[offset + 1]) << 16) |
            (static_cast<uint32_t>(data[offset + 2]) << 8) |
            static_cast<uint32_t>(data[offset + 3]);
    return true;
}

void addCodepoint(std::set<int>& codepoints, uint32_t codepoint,
                  uint32_t glyphIndex) {
    if (glyphIndex == 0 || codepoint == 0 || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return;
    codepoints.insert(static_cast<int>(codepoint));
}

void collectGroups(const Bytes& data, size_t groupStart, uint32_t groupCount,
                   size_t tableEnd, bool constantGlyph,
                   std::set<int>& codepoints) {
    for (uint32_t group = 0; group < groupCount; ++group) {
        size_t offset = groupStart + static_cast<size_t>(group) * 12;
        if (offset > tableEnd || tableEnd - offset < 12) break;
        uint32_t start = 0, end = 0, startGlyph = 0;
        if (!readU32(data, offset, start) ||
            !readU32(data, offset + 4, end) ||
            !readU32(data, offset + 8, startGlyph) || start > end) continue;
        end = std::min(end, 0x10FFFFu);
        if (start > end || (constantGlyph && startGlyph == 0)) continue;
        for (uint32_t cp = start; cp <= end; ++cp) {
            uint32_t glyph = constantGlyph ? startGlyph : startGlyph + cp - start;
            addCodepoint(codepoints, cp, glyph);
        }
    }
}

void collectSubtable(const Bytes& data, size_t tableStart, size_t cmapEnd,
                     std::set<int>& codepoints) {
    uint16_t format = 0;
    if (!readU16(data, tableStart, format)) return;

    size_t tableLength = 0;
    if (format == 8 || format == 10 || format == 12 || format == 13) {
        uint32_t length = 0;
        if (!readU32(data, tableStart + 4, length)) return;
        tableLength = length;
    } else {
        uint16_t length = 0;
        if (!readU16(data, tableStart + 2, length)) return;
        tableLength = length;
    }
    if (tableLength == 0 || tableStart > cmapEnd ||
        tableLength > cmapEnd - tableStart) return;
    size_t tableEnd = tableStart + tableLength;

    if (format == 0) {
        if (tableLength < 262) return;
        for (uint32_t cp = 0; cp < 256; ++cp)
            addCodepoint(codepoints, cp, data[tableStart + 6 + cp]);
    } else if (format == 2) {
        if (tableLength < 518) return;
        uint16_t maxKey = 0;
        for (size_t high = 0; high < 256; ++high) {
            uint16_t key = 0;
            if (!readU16(data, tableStart + 6 + high * 2, key)) return;
            maxKey = std::max(maxKey, key);
        }
        size_t subHeaders = tableStart + 518;
        size_t subHeaderCount = static_cast<size_t>(maxKey / 8) + 1;
        if (subHeaders > tableEnd || subHeaderCount > (tableEnd - subHeaders) / 8)
            return;
        for (size_t high = 0; high < 256; ++high) {
            uint16_t key = 0;
            readU16(data, tableStart + 6 + high * 2, key);
            if (high != 0 && key == 0) continue;
            size_t subHeader = subHeaders + static_cast<size_t>(key / 8) * 8;
            uint16_t first = 0, count = 0, deltaRaw = 0, rangeOffset = 0;
            if (!readU16(data, subHeader, first) ||
                !readU16(data, subHeader + 2, count) ||
                !readU16(data, subHeader + 4, deltaRaw) ||
                !readU16(data, subHeader + 6, rangeOffset)) continue;
            int16_t delta = static_cast<int16_t>(deltaRaw);
            for (uint32_t low = first; low < static_cast<uint32_t>(first) + count;
                 ++low) {
                size_t glyphOffset = subHeader + 6 + rangeOffset +
                                     static_cast<size_t>(low - first) * 2;
                uint16_t glyph = 0;
                if (glyphOffset > tableEnd || tableEnd - glyphOffset < 2 ||
                    !readU16(data, glyphOffset, glyph)) break;
                if (glyph != 0) glyph = static_cast<uint16_t>(glyph + delta);
                uint32_t cp = high == 0 ? low :
                    (static_cast<uint32_t>(high) << 8) | low;
                addCodepoint(codepoints, cp, glyph);
            }
        }
    } else if (format == 4) {
        uint16_t segCountX2 = 0;
        if (tableLength < 16 || !readU16(data, tableStart + 6, segCountX2) ||
            segCountX2 == 0 || (segCountX2 & 1) != 0) return;
        size_t segCount = segCountX2 / 2;
        size_t endCodes = tableStart + 14;
        size_t startCodes = endCodes + segCount * 2 + 2;
        size_t deltas = startCodes + segCount * 2;
        size_t rangeOffsets = deltas + segCount * 2;
        if (rangeOffsets > tableEnd || segCount > (tableEnd - rangeOffsets) / 2)
            return;
        for (size_t segment = 0; segment < segCount; ++segment) {
            uint16_t start = 0, end = 0, deltaRaw = 0, rangeOffset = 0;
            if (!readU16(data, startCodes + segment * 2, start) ||
                !readU16(data, endCodes + segment * 2, end) ||
                !readU16(data, deltas + segment * 2, deltaRaw) ||
                !readU16(data, rangeOffsets + segment * 2, rangeOffset) ||
                start > end) continue;
            int16_t delta = static_cast<int16_t>(deltaRaw);
            for (uint32_t cp = start; cp <= end; ++cp) {
                uint16_t glyph = 0;
                if (rangeOffset == 0) {
                    glyph = static_cast<uint16_t>(cp + delta);
                } else {
                    size_t glyphOffset = rangeOffsets + segment * 2 + rangeOffset +
                                         static_cast<size_t>(cp - start) * 2;
                    if (glyphOffset > tableEnd || tableEnd - glyphOffset < 2 ||
                        !readU16(data, glyphOffset, glyph)) break;
                    if (glyph != 0) glyph = static_cast<uint16_t>(glyph + delta);
                }
                addCodepoint(codepoints, cp, glyph);
            }
        }
    } else if (format == 6) {
        uint16_t first = 0, count = 0;
        if (tableLength < 10 || !readU16(data, tableStart + 6, first) ||
            !readU16(data, tableStart + 8, count)) return;
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t glyph = 0;
            if (!readU16(data, tableStart + 10 + static_cast<size_t>(i) * 2,
                         glyph)) break;
            addCodepoint(codepoints, static_cast<uint32_t>(first) + i, glyph);
        }
    } else if (format == 8) {
        uint32_t groups = 0;
        if (tableLength < 8208 || !readU32(data, tableStart + 8204, groups)) return;
        collectGroups(data, tableStart + 8208, groups, tableEnd, false, codepoints);
    } else if (format == 10) {
        uint32_t first = 0, count = 0;
        if (tableLength < 20 || !readU32(data, tableStart + 12, first) ||
            !readU32(data, tableStart + 16, count)) return;
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t glyph = 0;
            if (!readU16(data, tableStart + 20 + static_cast<size_t>(i) * 2,
                         glyph)) break;
            addCodepoint(codepoints, first + i, glyph);
        }
    } else if (format == 12 || format == 13) {
        uint32_t groups = 0;
        if (tableLength < 16 || !readU32(data, tableStart + 12, groups)) return;
        collectGroups(data, tableStart + 16, groups, tableEnd,
                      format == 13, codepoints);
    }
}

bool collectCmap(const char* fontPath, std::set<int>& codepoints) {
    std::ifstream input(fontPath, std::ios::binary | std::ios::ate);
    if (!input) return false;
    std::streamsize size = input.tellg();
    if (size <= 0) return false;
    input.seekg(0, std::ios::beg);
    Bytes data(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(data.data()), size)) return false;

    size_t fontOffset = 0;
    uint32_t signature = 0;
    if (!readU32(data, 0, signature)) return false;
    if (signature == 0x74746366) {
        uint32_t fontCount = 0, firstFontOffset = 0;
        if (!readU32(data, 8, fontCount) || fontCount == 0 ||
            !readU32(data, 12, firstFontOffset)) return false;
        fontOffset = firstFontOffset;
    }

    uint16_t tableCount = 0;
    if (!readU16(data, fontOffset + 4, tableCount)) return false;
    size_t cmapOffset = 0, cmapLength = 0;
    for (uint16_t table = 0; table < tableCount; ++table) {
        size_t record = fontOffset + 12 + static_cast<size_t>(table) * 16;
        uint32_t tag = 0, offset = 0, length = 0;
        if (!readU32(data, record, tag) || !readU32(data, record + 8, offset) ||
            !readU32(data, record + 12, length)) return false;
        if (tag == 0x636D6170) {
            cmapOffset = offset;
            cmapLength = length;
            break;
        }
    }
    if (cmapLength < 4 || cmapOffset > data.size() ||
        cmapLength > data.size() - cmapOffset) return false;

    uint16_t encodingCount = 0;
    if (!readU16(data, cmapOffset + 2, encodingCount)) return false;
    size_t cmapEnd = cmapOffset + cmapLength;
    std::set<size_t> parsedSubtables;
    size_t before = codepoints.size();
    for (uint16_t encoding = 0; encoding < encodingCount; ++encoding) {
        size_t record = cmapOffset + 4 + static_cast<size_t>(encoding) * 8;
        uint16_t platform = 0, encodingId = 0;
        uint32_t subtableOffset = 0;
        if (!readU16(data, record, platform) ||
            !readU16(data, record + 2, encodingId) ||
            !readU32(data, record + 4, subtableOffset)) break;
        bool unicodeMapping = platform == 0 ||
            (platform == 3 && (encodingId == 0 || encodingId == 1 ||
                               encodingId == 10));
        if (!unicodeMapping || subtableOffset >= cmapLength) continue;
        size_t subtable = cmapOffset + subtableOffset;
        if (!parsedSubtables.insert(subtable).second) continue;
        collectSubtable(data, subtable, cmapEnd, codepoints);
    }
    return codepoints.size() > before;
}

void collectUtf8(const std::string& text, std::set<int>& codepoints) {
    for (size_t i = 0; i < text.size();) {
        unsigned char first = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        size_t trailing = 0;
        if (first < 0x80) {
            cp = first;
        } else if ((first & 0xE0) == 0xC0) {
            cp = first & 0x1F;
            trailing = 1;
        } else if ((first & 0xF0) == 0xE0) {
            cp = first & 0x0F;
            trailing = 2;
        } else if ((first & 0xF8) == 0xF0) {
            cp = first & 0x07;
            trailing = 3;
        } else {
            ++i;
            continue;
        }
        if (trailing > text.size() - i - 1) break;
        bool valid = true;
        for (size_t part = 1; part <= trailing; ++part) {
            unsigned char next = static_cast<unsigned char>(text[i + part]);
            if ((next & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3F);
        }
        if (valid && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF))
            codepoints.insert(static_cast<int>(cp));
        i += valid ? trailing + 1 : 1;
    }
}

} // namespace

std::vector<int> collectFontCodepoints(
    const char* fontPath, const std::vector<std::string>& requiredTexts) {
    std::set<int> codepoints;
    for (const std::string& text : requiredTexts) collectUtf8(text, codepoints);
    for (int cp = 32; cp <= 126; ++cp) codepoints.insert(cp);

    if (!collectCmap(fontPath, codepoints)) {
        for (int cp = 0x4E00; cp <= 0x9FFF; ++cp) codepoints.insert(cp);
        for (int cp = 0x3000; cp <= 0x303F; ++cp) codepoints.insert(cp);
        for (int cp = 0xFF00; cp <= 0xFFEF; ++cp) codepoints.insert(cp);
    }
    return std::vector<int>(codepoints.begin(), codepoints.end());
}
