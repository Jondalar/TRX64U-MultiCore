// Unzip on ABI 3 (T104.3 D9): the entry the base calls and the context of ABI 2 that src/unzip.c was written against. The application has no screens of
// its own; what it asks of the person is the base's dialogs and the bar (the compat layer maps the old calls onto them).
#include "glue.h"

extern "C" int unzip_main(const u2p_api_t *api, const u2p_app_context_t *ctx);

static AppCtx *g_base;
static int reply_fn(const char *text) { return u2p_reply_into(g_base, text); }

extern "C" int app_main(const LibApi *api, AppCtx *ctx)
{
    g_base = ctx;
    const u2p_api_t *u = u2p_compat_init(reinterpret_cast<const LibApi *>(api));
    char path[200];
    if (!ctx->file[0]) {                                  // the entry of the Tools area names no file: ask for one
        if (!api->dlg || !api->dlg->pick_file("/", ".zip", path, sizeof(path))) {
            return 0;
        }
    } else {
        for (int i = 0; i < (int)sizeof(path) - 1 && (path[i] = ctx->file[i]) != 0; i++) {
        }
        path[sizeof(path) - 1] = 0;
    }
    u2p_app_context_t uc;
    memset(&uc, 0, sizeof(uc));
    uc.size = sizeof(uc);
    uc.app_path = "";
    uc.selected_file = path;
    uc.action = ctx->action[0] ? ctx->action : "folder";
    uc.launch_source = U2P_LAUNCH_BROWSER;
    uc.has_ui = api->dlg != 0;
    uc.reply = reply_fn;
    return unzip_main(u, &uc);
}
