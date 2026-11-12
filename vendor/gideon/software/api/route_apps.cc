// REST access to the apps of the App Loader (doc/AppLoader_Spec.md): list the registered apps and
// start one of them. An app only starts this way when its manifest names the action in "rest".

#include <string.h>

#include "routes.h"
#include "app_api.h"
#include "app_loader.h"
#include "app_registry.h"

#define APP_REPLY_SIZE 4096

static JSON_Object *app_info(AppRegistryEntry *entry)
{
    const AppManifest &m = entry->manifest;
    JSON_List *rest = JSON::List();
    for (int i = 0; i < m.num_rest; i++) {
        rest->add(m.rest[i]);
    }
    return JSON::Obj()
        ->add("id", m.id)
        ->add("name", m.name)
        ->add("version", m.version)
        ->add("path", entry->path.c_str())
        ->add("rest", rest);
}

API_DOC(GET, apps, none,
    TAG("Apps")
    SUMMARY("List the installed apps")
    DESCRIPTION("Lists the apps the App Loader has registered: every `.u2a` file with a manifest "
                "that was run once from the file browser. With an id it gives that one app.\n"
                "\n"
                "`rest` names the actions `apps/{id}:run` may start. An app with an empty list "
                "can only be started from the menu.")
    PATH("/v1/apps", "listApps", "List the installed apps")
    PATH("/v1/apps/{id}", "getApp", "Describe one app")
    PATH_PARAM("id", "string", "The id from the app's manifest.", "softboot")
    RESPONSE("200", "application/json", "AppsResponse", "The registered apps.", "listApps")
    RESPONSE_EXAMPLE("200", "One app", "{\n  \"apps\" : [ {\n    \"id\" : \"softboot\",\n    \"name\" : \"Softboot\",\n    \"version\" : \"1.1\",\n    \"path\" : \"/Flash/apps/softboot.u2a\",\n    \"rest\" : [ \"run\" ]\n  } ],\n  \"errors\" : []\n}", "listApps")
    RESPONSE("200", "application/json", "AppResponse", "The app.", "getApp")
    RESPONSE_ERROR("404", "No app with id 'foo'", "getApp")
    RESPONSE_ERROR("503", "The App Loader is not ready", "")
)
API_CALL(GET, apps, none, NULL, ARRAY({ }))
{
    AppRegistry *reg = AppRegistry::getRegistry();
    if (!reg) {
        resp->error("The App Loader is not ready");
        resp->json_response(HTTP_SERVICE_UNAVAILABLE);
        return;
    }
    if (args.get_path_depth() >= 1) {
        reg->lock();
        AppRegistryEntry *entry = reg->lookup(args.get_path(0));
        if (!entry) {
            reg->unlock();
            resp->error("No app with id '%s'", args.get_path(0));
            resp->json_response(HTTP_NOT_FOUND);
            return;
        }
        resp->json->add("app", app_info(entry));
        reg->unlock();
        resp->json_response(HTTP_OK);
        return;
    }
    JSON_List *list = JSON::List();
    resp->json->add("apps", list);
    reg->lock();
    for (int i = 0; i < reg->get_count(); i++) {
        list->add(app_info(reg->get_entry(i)));
    }
    reg->unlock();
    resp->json_response(HTTP_OK);
}

API_DOC(PUT, apps, run,
    TAG("Apps")
    SUMMARY("Start an app")
    CAUTION("machine-state", "The C64 is stopped while the app runs. An app may start a program or reset the machine when it returns.")
    DESCRIPTION("Starts a registered app without a screen and answers when it has returned, with "
                "its exit code and whatever text it handed back, at most 4 KB.\n"
                "\n"
                "`action` has to be one of the app's `rest` actions and defaults to the first. "
                "`file` reaches the app as the selected file, the way a file browser action "
                "passes it, and `args` reaches it unchanged. While the app runs the C64 is "
                "stopped; it goes on afterwards unless the app started a program, a cartridge or "
                "a reset. An app that hands the machine to other firmware, such as `softboot`, "
                "does not answer.\n"
                "\n"
                "Only one app runs at a time. A launch while another app is running, from here "
                "or from the menu, is answered with 423.")
    PATH("/v1/apps/{id}:run", "runApp", "")
    PATH_PARAM("id", "string", "The id from the app's manifest.", "softboot")
    PARAM("action", "string", "One of the app's `rest` actions. Defaults to the first.", "", "run")
    PARAM("file", "string", "Full path handed to the app as the selected file.", "", "/Usb0/update.ue2")
    PARAM("args", "string", "Text handed to the app unchanged.", "", "verbose")
    RESPONSE("200", "application/json", "AppRunResponse", "The app has returned.", "")
    RESPONSE_EXAMPLE("200", "Returned", "{\n  \"app\" : \"selftest\",\n  \"action\" : \"run\",\n  \"exit_code\" : 0,\n  \"output\" : \"12 checks passed\",\n  \"errors\" : []\n}", "")
    RESPONSE_ERROR("400", "App 'softboot' has no REST action 'flash'", "")
    RESPONSE_ERROR("403", "App 'trex_crt' cannot be started over REST", "")
    RESPONSE_ERROR("404", "No app with id 'foo'", "")
    RESPONSE_ERROR("409", "The app file is incomplete or invalid (for example while it is uploaded)", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "The App Loader is not ready", "")
)
API_CALL(PUT, apps, run, NULL, ARRAY({ { "action", P_OPTIONAL }, { "file", P_OPTIONAL }, { "args", P_OPTIONAL } }))
{
    AppRegistry *reg = AppRegistry::getRegistry();
    if (!reg || !g_app_loader) {
        resp->error("The App Loader is not ready");
        resp->json_response(HTTP_SERVICE_UNAVAILABLE);
        return;
    }
    const char *id = args.get_path(0);
    // The registry may be refreshed by another task as soon as the lock is released, so what the launch needs is
    // copied under it.
    AppRegistry::Check check;
    reg->lock();
    AppRegistryEntry *entry = reg->lookup_fresh(id, check);
    if (!entry) {
        reg->unlock();
        if (check == AppRegistry::CHECK_INVALID) {
            resp->error("App '%s': app file incomplete or invalid", id);
            resp->json_response(HTTP_CONFLICT);
        } else if (check == AppRegistry::CHECK_NO_MEDIUM) {
            resp->error("App '%s': medium not present", id);
            resp->json_response(HTTP_NOT_FOUND);
        } else {
            resp->error("No app with id '%s' (registry refreshed)", id);
            resp->json_response(HTTP_NOT_FOUND);
        }
        return;
    }
    // On the heap: the manifest is 1.3 KB and this runs on the HTTP listener's small stack (T67).
    AppManifest *manifest = new AppManifest(entry->manifest);
    char *app_path = new char[entry->path.length() + 1];
    strcpy(app_path, entry->path.c_str());
    reg->unlock();
    struct PathGuard
    {
        char *p;
        AppManifest *m;
        ~PathGuard()
        {
            delete[] p;
            delete m;
        }
    } path_guard = { app_path, manifest };
    const AppManifest &m = *manifest;

    if (!m.num_rest) {
        resp->error("App '%s' cannot be started over REST", id);
        resp->json_response(HTTP_FORBIDDEN);
        return;
    }
    const char *action = args.get_or("action", m.rest[0]);
    int found = -1;
    for (int i = 0; i < m.num_rest; i++) {
        if (!strcmp(m.rest[i], action)) {
            found = i;
        }
    }
    if (found < 0) {
        resp->error("App '%s' has no REST action '%s'", id, action);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    // Directory and file name of the app, as the App Loader command takes them.
    const char *slash = strrchr(app_path, '/');
    if (!slash || (slash == app_path) || !slash[1]) {
        resp->error("App '%s' has no valid path", id);
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    int dir_len = slash - app_path;
    char *dir = new char[dir_len + 1];
    memcpy(dir, app_path, dir_len);
    dir[dir_len] = 0;

    char *reply = new char[APP_REPLY_SIZE];
    reply[0] = 0;
    AppLaunchParams params = {};
    params.selected_file = args["file"];
    params.launch_source = U2P_LAUNCH_HTTP;
    params.action = m.rest[found];
    params.args = args["args"];
    params.reply = reply;
    params.reply_size = APP_REPLY_SIZE;
    params.exit_code = -1;

    SubsysCommand *run = new SubsysCommand(NULL, SUBSYSID_APP_LOADER, APP_CMD_RUN_ACTION, 0, dir, slash + 1);
    run->buffer = &params;
    run->bufferSize = sizeof(params);
    SubsysResultCode_t result = run->execute();
    delete[] dir;

    resp->json->add("app", id)->add("action", params.action)->add("exit_code", params.exit_code)->add("output", reply);
    delete[] reply;
    if (result.status != SSRET_OK) {
        resp->error(SubsysCommand::error_string(result.status));
    }
    resp->json_response(SubsysCommand::http_response_map(result.status));
}

API_DOC(PUT, apps, run_file,
    TAG("Apps")
    SUMMARY("Start an app file in place")
    CAUTION("machine-state", "The C64 is stopped while the app runs. An app may start a program or reset the machine when it returns.")
    DESCRIPTION("Starts the `.u2a` file at `app` without a screen, from where it is, and answers when "
                "`app_main` has returned, as `PUT /v1/apps/{id}:run` does. The app is neither "
                "registered nor copied anywhere, so this also starts an app that has no manifest "
                "or no `rest` actions. An app that went on in a task of its own (trxmon) keeps "
                "running after the answer, and no other app starts until it has ended (423).")
    PATH("/v1/apps:run_file", "runAppFile", "")
    PARAM("app", "string", "Full path of the `.u2a` file.", "", "/Usb0/apps/trxmon.u2a")
    PARAM("action", "string", "Handed to the app as its action.", "", "serve")
    PARAM("file", "string", "Full path handed to the app as the selected file.", "", "/Usb0/update.ue2")
    PARAM("args", "string", "Text handed to the app unchanged.", "", "verbose")
    RESPONSE("200", "application/json", "AppRunResponse", "The app has returned.", "")
    RESPONSE_ERROR("400", "app must be a full path", "")
    RESPONSE_ERROR("404", "Cannot open file", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "The App Loader is not ready", "")
)
API_CALL(PUT, apps, run_file, NULL, ARRAY({ { "app", P_REQUIRED }, { "action", P_OPTIONAL }, { "file", P_OPTIONAL }, { "args", P_OPTIONAL } }))
{
    if (!g_app_loader) {
        resp->error("The App Loader is not ready");
        resp->json_response(HTTP_SERVICE_UNAVAILABLE);
        return;
    }
    const char *app_path = args["app"];
    const char *slash = strrchr(app_path, '/');
    if ((app_path[0] != '/') || !slash || (slash == app_path) || !slash[1]) {
        resp->error("app must be a full path");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    int dir_len = slash - app_path;
    char *dir = new char[dir_len + 1];
    memcpy(dir, app_path, dir_len);
    dir[dir_len] = 0;

    char *reply = new char[APP_REPLY_SIZE];
    reply[0] = 0;
    AppLaunchParams params = {};
    params.selected_file = args["file"];
    params.launch_source = U2P_LAUNCH_HTTP;
    params.action = args["action"];
    params.args = args["args"];
    params.reply = reply;
    params.reply_size = APP_REPLY_SIZE;
    params.exit_code = -1;

    // With params the loader runs the file as it is: no registration (section 3.1 is for "Run App").
    SubsysCommand *run = new SubsysCommand(NULL, SUBSYSID_APP_LOADER, APP_CMD_RUN_ACTION, 0, dir, slash + 1);
    run->buffer = &params;
    run->bufferSize = sizeof(params);
    SubsysResultCode_t result = run->execute();
    delete[] dir;

    resp->json->add("app", app_path)->add("action", params.action ? params.action : "")->add("exit_code", params.exit_code)->add("output", reply);
    delete[] reply;
    if (result.status != SSRET_OK) {
        resp->error(SubsysCommand::error_string(result.status));
    }
    resp->json_response(SubsysCommand::http_response_map(result.status));
}
