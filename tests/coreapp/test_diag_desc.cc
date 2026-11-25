// T260 P7: the diag core's descriptor as its own app has it (built as that app: run.sh diagapp): it passes the descriptor check, it has no machine (menu first, no picture, the freeze
// waits for nothing), and its memory is not reachable by the application. The cases the shared test_desc and test_memops held for it before it left the shared app.
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "desc_check.h"
#include "portdiag.h"

int main()
{
    CHECK(!desc_check(&portdiag_desc));
    { CoreDesc d = portdiag_desc; d.menu_first = false; CHECK(desc_check(&d) && strstr(desc_check(&d), "freeze")); }       // only a core with no machine may have no PAUSED bit
    { CoreDesc d = portdiag_desc; CHECK(!desc_check(&d)); d.freeze.timeout_ms = 0; CHECK(desc_check(&d) && strstr(desc_check(&d), "freeze")); }
    CHECK(portdiag_desc.menu_first);
    CHECK_EQ((int)portdiag_desc.video.w, 0);
    CHECK_STR(portdiag_desc.core_id, "diag");
    DONE("test_diag_desc");
}
