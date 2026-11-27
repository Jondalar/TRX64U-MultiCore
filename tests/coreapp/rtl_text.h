// T264 S2: a decode module names its offsets by the constants of its generated block (`localparam [11:0] R_<BLOCK>_<PROP>`,
// tools/toml_a_gen.py). rtl_literal returns the module's text with every use of such a constant replaced by the literal the block
// defines, so a test that reads a decode by its literals reads the converted module as it read the old one:
// `(R_X + k) >> 2` -> 10'hNNN of (X + k) >> 2, `R_X + k` or `R_X + 12'dk` -> 12'hNNN of X + k, `R_X >> 2` -> 10'hNNN (the word of a word-granular case), `R_X` -> 12'hNNN.
#pragma once
#include <cstdio>
#include <map>
#include <regex>
#include <string>

static inline std::string rtl_literal(const std::string &src)
{
    std::map<std::string, unsigned> val;
    const std::regex def("localparam \\[11:0\\] (R_[A-Z0-9_]+) = 12'h([0-9A-Fa-f]+);");
    for (std::sregex_iterator i(src.begin(), src.end(), def), e; i != e; ++i) val[(*i)[1].str()] = (unsigned)std::stoul((*i)[2].str(), 0, 16);
    if (val.empty()) return src;
    // `(R_X + k) >> 2` first: the word of a byte inside a register (a word-granular read case)
    std::string t2;
    {
        const std::regex word("\\((R_[A-Z0-9_]+) \\+ (?:12'd)?([0-9]+)\\) >> 2");
        size_t last = 0;
        for (std::sregex_iterator i(src.begin(), src.end(), word), e; i != e; ++i) {
            const std::smatch &m = *i;
            auto v = val.find(m[1].str());
            if (v == val.end()) continue;
            char lit[16];
            snprintf(lit, sizeof(lit), "10'h%03X", (v->second + (unsigned)std::stoul(m[2].str())) >> 2);
            t2.append(src, last, (size_t)m.position(0) - last);
            t2 += lit;
            last = (size_t)(m.position(0) + m.length(0));
        }
        t2.append(src, last, std::string::npos);
    }
    const std::string &t = t2;
    const std::regex use("\\b(R_[A-Z0-9_]+)( \\+ (?:12'd)?([0-9]+)| >> 2)?");
    std::string out;
    size_t last = 0;
    for (std::sregex_iterator i(t.begin(), t.end(), use), e; i != e; ++i) {
        const std::smatch &m = *i;
        auto v = val.find(m[1].str());
        if (v == val.end()) continue;
        char lit[16];
        if (m[2].str() == " >> 2") snprintf(lit, sizeof(lit), "10'h%03X", v->second >> 2);
        else snprintf(lit, sizeof(lit), "12'h%03X", v->second + (m[3].matched ? (unsigned)std::stoul(m[3].str()) : 0u));
        out.append(t, last, (size_t)m.position(0) - last);
        out += lit;
        last = (size_t)(m.position(0) + m.length(0));
    }
    out.append(t, last, std::string::npos);
    return out;
}
