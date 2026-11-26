// T168 S7: the DDT descriptor (ddt_novic; ddt_mandel is a large core without an application since T255) against the code it describes. The numbers (ID, register offsets, status bit, buffer defaults, picture size) are read
// from cores/ddt_mandel/trx/ddt_regs.v and the two layer2 files, the name from core.toml; a descriptor that disagrees with any of them fails. The start
// sequence (held, then released last) runs against the register model.
#include <fstream>
#include <sstream>
#include <regex>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "ddt_regs.h"
#include "ops_glue.h"
extern const CoreDesc ddt_novic_desc;           // cores.h lists the shared app's cores; DDT Look Ma, No VIC is its own app (cores/ddt_novic/app/lib)

static std::string slurp(const std::string &rel)
{
    std::ifstream f(std::string(COREAPP_DIR) + "/../" + rel);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static bool has(const std::string &t, const std::string &re) { return std::regex_search(t, std::regex(re)); }

static void one(const CoreDesc &d, const char *id, const char *name_toml, const char *layer2, const char *idhex, int lines)
{
    std::string regs = slurp("cores/ddt_mandel/trx/ddt_regs.v");
    std::string l2 = slurp(layer2);
    std::string toml = slurp(name_toml);
    CHECK(!regs.empty() && !l2.empty() && !toml.empty());

    // identity: the four bytes of the window, byte 0 first, are what the layer2 file passes to the register block
    CHECK(!memcmp(d.id, id, 4));
    CHECK(has(l2, std::string("\\.ID\\(32'h") + idhex + "\\)"));
    std::smatch m;
    CHECK(std::regex_search(toml, m, std::regex("\\nname\\s*=\\s*\"([^\"]*)\"")));
    CHECK_STR(d.cor_name, m[1].str().c_str());
    CHECK_STR(d.title, d.cor_name);

    // the register numbers are the ones the block decodes
    char re[160];
    snprintf(re, sizeof(re), "12'h%03X: ctrl_r <= wdata", DDT_CTRL); CHECK(has(regs, re));
    snprintf(re, sizeof(re), "12'h%03X: q <= s_q", DDT_STATUS); CHECK(has(regs, re));
    snprintf(re, sizeof(re), "12'h%03X: q <= l_q", DDT_FB_LATEST); CHECK(has(regs, re));
    snprintf(re, sizeof(re), "12'h%03X: q <= n_q", DDT_FRAME_SEQ); CHECK(has(regs, re));
    snprintf(re, sizeof(re), "12'h%03X: fbb_r\\[7:0\\] <= wdata", DDT_FB_BASE); CHECK(has(regs, re));
    snprintf(re, sizeof(re), "12'h%03X: fbs_r\\[7:0\\] <= wdata", DDT_FB_STRIDE); CHECK(has(regs, re));
    snprintf(re, sizeof(re), "fbb_r = 32'h%08X", DDT_FB_ADDR); CHECK(has(regs, re));
    snprintf(re, sizeof(re), "fbs_r = 32'h%08X", DDT_FB_STRIDE_BYTES); CHECK(has(regs, re));
    CHECK_EQ(DDT_CTRL_HOLD, 1);
    CHECK(has(l2, "status = \\{[^}]*halted_q, hold \\| cpu_rst\\}"));      // b1 held, b0 in reset
    CHECK_EQ(DDT_ST_HELD, 2);

    // the picture: 352 wide, `lines` high, RGB565, four buffers, scale 3 centred on 1920 x 1080
    char g[80];
    snprintf(g, sizeof(g), ".GEOM_LINES(16'd%d), .PIX_W(16'd352)", lines); CHECK(l2.find(g) != std::string::npos);
    CHECK_EQ((int)d.video.w, 352); CHECK_EQ((int)d.video.h, lines);
    CHECK_EQ((int)d.video.scale, 3); CHECK_EQ((int)d.video.nbuf, 4); CHECK_EQ((int)d.video.bpp, 0);
    CHECK_EQ(d.video.x0, (1920 - 352 * 3) / 2); CHECK_EQ(d.video.y0, (1080 - lines * 3) / 2);
    CHECK_EQ(d.video.fb_base, DDT_FB_ADDR); CHECK_EQ(d.video.fb_stride, DDT_FB_STRIDE_BYTES);
    CHECK(d.video.fb_stride >= (uint32_t)(352 * lines * 2));
    CHECK_EQ(d.video.reg_latest, DDT_FB_LATEST); CHECK_EQ(d.video.reg_seq, DDT_FRAME_SEQ);
    CHECK_EQ(d.video.reg_fb_base, DDT_FB_BASE); CHECK_EQ(d.video.reg_fb_stride, DDT_FB_STRIDE);
    CHECK_EQ(d.freeze.reg_status, DDT_STATUS); CHECK_EQ(d.freeze.mask, DDT_ST_HELD);

    // what the menu has: one task, Reset; nothing else
    CHECK_EQ(d.nroms, 0); CHECK_EQ(d.nsettings, 0); CHECK_EQ(d.ntasks, 1); CHECK_EQ(d.nfiletypes, 0); CHECK_EQ(d.nmedia, 0);
    CHECK_STR(d.tasks[0].label, "Reset");
    CHECK(!d.needs_program);

    // the start: held first, released last (CTRL back to 0), and a Reset from the menu does the same
    FakeIo io; FakeFs fs; PxRec px;
    CoreCtx c; memset(&c, 0, sizeof(c));
    c.desc = &d; c.io = &io; c.fs = &fs;
    Settings st(d.settings, d.nsettings, d.store, &fs, "/sd/ddt.cfg", 0);
    st.load();
    c.settings = &st;
    Video vid(d.video, &px, &io);
    c.video = &vid;
    char msg[200];
    struct U : BootUi { void message(const char *, const char *, bool) { } void clear() { } void tick() { } } ui;
    CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
    CHECK(c.started);
    CHECK_EQ(io.regs[DDT_CTRL], 0);
    CHECK_STR(io.events.front().c_str(), "mode_reset");
    CHECK_STR(io.events.back().c_str(), "mode_unreset");
    io.events.clear();
    size_t n0 = io.log.size();
    CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
    int hold_at = -1, rel_at = -1;
    for (size_t i = n0; i < io.log.size(); i++) {
        if (io.log[i].reg == DDT_CTRL && io.log[i].v == DDT_CTRL_HOLD && hold_at < 0) hold_at = (int)i;
        if (io.log[i].reg == DDT_CTRL && io.log[i].v == 0) rel_at = (int)i;
    }
    CHECK(hold_at >= 0 && hold_at < rel_at);
    CHECK_EQ(rel_at, (int)io.log.size() - 1);
    CHECK_EQ(io.reset_releases, 2);
}

int main()
{
    one(ddt_novic_desc, "DDTN", "cores/ddt_novic/core.toml", "cores/ddt_novic/layer2_ddt_novic.v", "4E544444", 200);
    DONE("test_ddt");
}
