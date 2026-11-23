Online search (T239.3 D12, P3-1, L-3). A resident sub-app of format 3 (`APP_KIND = lib`, needs `base>=3.3`): the Assembly64 / CommoServe search that was a global object of the shared
application. `online_lib.cc` is `lib_register`: it registers the operations `online.*` and the state values of the form (`online_search.cc`) and exports the table "online"
(`coreapp/src/online_api.h`, major 1) that the file manager (`netfs.cc`) reads the folder `/Online` through. `app.toml` is its menu record: the tab "Online" of F3 with its twenty rows.
The client is `online_http.cc` (one GET over `api->net->connect`, `send`, `recv`, `wait`) and `online_json.cc` (a reader of the answers); the state of the search is in
`online_search.cc`, in the image. Every request to the server runs on a job (`api->job->start`), never on the menu task or the executor task (api.md 1.3.9): `online.presets` and `online.search` are
nodes with the attribute `job` that answer `more` while the job waits; the folder `/Online` answers from what is kept and starts a prefetch for the rest (api.md 9.5.11).

A core gets the search by carrying `online.u2a` in its `.cor` (`cor_pack.py --lib`, record 7); a core without it has no tab "Online" and no `/Online`. Built by `ci/trxapps.sh`, shipped
beside `unzip.u2a`. Tested by `tests/loader/test_online.inc` (included by `test_libload.cc`): the file loads with its menu record and export, the form (stepped and not) and the folder (with its prefetch) run over a model of
`api->net` and of the base's job table, which runs a job only when the test says so, the client alone is run through every way an answer can end, and the image gives all its memory back when it is unloaded.
PROVENANCE: ours; see the rows "Assembly64 / CommoServe client" and "Online search" of `PROVENANCE.md`.
