# Ownership regression harness

`run.sh` builds the real plugin sources (`4DPlugin-tidy-html5.cpp`,
`support/4DPlugin-JSON.cpp`, `C_TEXT.cpp`), the unchanged SDK
`4DPluginAPI.c` and the bundled `a/libtidy.a` with ASan/UBSan against a
synthetic `Call4D` engine (`engine_harness.cpp`). The engine tracks every
string, picture, object, collection and handle it hands out, poisons the
results of failed calls, and reports leaks and contract violations. Inputs
are synthetic in-memory HTML; there is no network access.

```sh
tests/ownership/run.sh . fixed                 # every test must pass
git archive <base> | tar -x -C /tmp/base
tests/ownership/run.sh /tmp/base baseline      # named defects must fail,
                                               # controls must pass
```

Contracts covered:

- `PA_GetCollectionElement` / `PA_GetObjectProperty` / `PA_ExecuteCommandByID`
  results are owned: clear them, but only when `PA_GetLastError()` taken
  immediately after the call is `eER_NoErr`.
- A variable that receives a shallow `PA_Set*Variable` must start as
  `eVK_Undefined`, otherwise the empty owner leaks.
- A collection read from an object property is borrowed from its parent.

Each test runs in its own process. Test names prefixed `defect_live_` are
reachable from `Tidy`, `defect_dormant_` are built but uncalled,
`hardening_` cover error paths, and `control_` must pass on both trees.
