// The flows a module owns and the menu controller only shows (T192.3 D-B): a wizard that asks for the pad's buttons, a question the machine asks when the menu opens. A flow is DATA:
// the states that hold what the screen shows and the operations the controller's keys call. The controller knows no module by name; menuflows.cc names them.
#ifndef COREAPP_MENUFLOWS_H
#define COREAPP_MENUFLOWS_H

// A wizard: up while `active` has text. The row shows the three texts, RETURN / the left arrow / ESC call next / back / cancel (the key table, stage KS_WIZARD), and the run loop's pump
// calls `tick` (its answer: `more` = the screen changed, a text = the status line, a failure = a bad one).
struct MenuWizard {
    const char *active, *title, *left, *right, *sub;
    const char *next, *back, *cancel, *tick;
};

// A question the machine asks at the menu's open: `open` runs when the menu has opened with the machine held (its text is a note for the status line), `ask` holds the question (empty:
// none), RETURN calls `answer` with value 1 and ESC with value 0 (the text is the status line), leaving the question unanswered calls `leave` (it is asked again at the next open).
struct MenuFlow {
    const char *open, *ask, *answer, *leave;
    const char *hint;                // the status line while it is asked
};

// The facts the Home panel reads, as state names: the picture as the descriptor states it.
struct MenuFacts {
    const char *video_line;
};
const MenuFacts *menu_facts();

// The operation a tab's layout runs when the tab is shown (the monitor starts its session), null when it has none. Not for an application's screens: they name their own `enter`.
const char *menu_layout_enter(const char *layout_name);

// The operation the run loop's pump runs while a layout is shown (the monitor's stops, finished run-tos and live log); its answer `more` says the screen changed.
const char *menu_layout_tick(const char *layout_name);

// Does a file action that succeeded close the menu? Its own flag, except a plain mount, which the core's setting "Leave Menu on Mount" can keep open.
struct FileAction;
struct CoreCtx;
bool menu_action_closes(CoreCtx *c, const FileAction &a);

// The Network line says "online" when it holds an address, not the text of "no network".
bool menu_link_online(const char *network_line);

const MenuWizard *menu_wizards(int *n);
const MenuFlow *menu_flows(int *n);

#endif
