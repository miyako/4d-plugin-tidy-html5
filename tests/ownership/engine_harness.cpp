/*
 * Synthetic 4D engine boundary for ownership regressions.
 *
 * Compiles the real plugin entry point (4DPlugin-tidy-html5.cpp, included
 * below) and links the real support/4DPlugin-JSON.cpp, the built SDK class
 * (C_TEXT.cpp), the unmodified 4DPluginAPI.c and the bundled a/libtidy.a.
 * Only gCall4D is replaced. Every engine resource (Unistrings, pictures,
 * objects/collections, handles) is a real heap block tracked in a live set,
 * so leaks are detected exactly and a dispose of something the engine no
 * longer owns (double free) is reported by name instead of being freed a
 * second time.
 *
 * Input is synthetic in-memory HTML only.
 *
 * Engine semantics modelled (per the SDK contract):
 *  - PA_CreateVariable(eVK_Unistring/eVK_Picture) allocates an empty value.
 *  - PA_GetStringVariable returns a view borrowed from the variable.
 *  - PA_SetObjectProperty deep-copies strings and pictures and retains
 *    objects/collections; the caller still owns its variable.
 *  - PA_GetObjectProperty and PA_GetCollectionElement return an owned copy
 *    (or a retained object/collection) that must be cleared; on error the
 *    result is left uninitialised (poisoned here) and must be neither read
 *    nor cleared.
 *  - PA_ExecuteCommandByID returns an owned result.
 */

/* Resolved through -I <source-root>, so the same harness tests baseline and fixed trees. */
#include "4DPlugin-tidy-html5.cpp"

#include "PrivateTypes.h"
#include "EntryPoints.h"

#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" Call4DProcPtr gCall4D;

namespace {

typedef std::basic_string<PA_Unichar> u16;

struct Obj {
    int rc;
    bool coll;
    std::map<u16, PA_Variable> props;
    std::vector<PA_Variable> items;
};

/* data first: the SDK dereferences array handles directly (*(T**)handle). */
struct Handle { char *data; size_t size; int locks; };

std::set<void *> g_strings;
std::set<void *> g_pictures;
std::set<Obj *> g_objects;
std::set<void *> g_handles;
std::vector<std::string> g_violations;

/* PA_GetObjectProperty for this key fails: eb.fError is set and the result
   is left uninitialised. The poison is readable (tests fail by assertion,
   not by crashing) but never engine-owned, so clearing it is counted. */
std::string g_fail_key;
/* PA_GetCollectionElement fails the same way for this index (-2: never). */
PA_long32 g_fail_index = -2;
/* JSON Stringify (1217) fails the same way. */
bool g_fail_stringify = false;
const char kPoisonMark = 0x5A;
int g_poison_clears = 0;
PA_Unichar kPoisonBuf[] = { 'B', 'A', 'D', '!', 0 };
PA_Unichar *const kPoison = kPoisonBuf;

PA_Variable undefined_var();

void poison(PA_Variable *out) {
    *out = undefined_var();
    out->fFiller = kPoisonMark;
    out->fType = eVK_Unistring;
    out->uValue.fString.fString = kPoison;
    out->uValue.fString.fLength = 4;
}

void violation(const std::string &what) {
    fprintf(stderr, "  ENGINE CONTRACT: %s\n", what.c_str());
    g_violations.push_back(what);
}

[[noreturn]] void die(const char *msg) {
    fprintf(stderr, "HARNESS ERROR: %s\n", msg);
    fflush(stderr);
    abort();
}

std::string narrow(const u16 &s) { std::string r; for(PA_Unichar c : s) r.push_back((char)c); return r; }
u16 widen(const std::string &s) { u16 r; for(char c : s) r.push_back((PA_Unichar)(unsigned char)c); return r; }

PA_Unistring new_string(const PA_Unichar *src, size_t len) {
    PA_Unichar *buf = (PA_Unichar *)malloc((len + 1) * sizeof(PA_Unichar));
    if(len) memcpy(buf, src, len * sizeof(PA_Unichar));
    buf[len] = 0;
    g_strings.insert(buf);
    PA_Unistring s;
    s.fLength = (PA_long32)len;
    s.fString = buf;
    s.fReserved1 = 0;
    s.fReserved2 = 0;
    return s;
}

void free_string(PA_Unistring *s) {
    if(!s->fString) return;
    if(s->fString == kPoison) {
        g_poison_clears++;
        fprintf(stderr, "  cleared the uninitialised result of a failed engine call\n");
    } else if(!g_strings.erase(s->fString)) {
        violation("double_dispose_string");
    } else {
        free(s->fString);
    }
    s->fString = 0;
    s->fLength = 0;
}

void *new_picture() {
    void *p = malloc(16);
    g_pictures.insert(p);
    return p;
}

void free_picture(void *p) {
    if(!p) return;
    if(!g_pictures.erase(p)) { violation("double_dispose_picture"); return; }
    free(p);
}

Handle *new_handle(size_t size) {
    Handle *h = (Handle *)malloc(sizeof(Handle));
    h->size = size;
    h->locks = 0;
    h->data = (char *)calloc(1, size ? size : 1);
    g_handles.insert(h);
    return h;
}

void free_handle(Handle *h) {
    if(!h) return;
    if(!g_handles.erase(h)) { violation("double_dispose_handle"); return; }
    free(h->data);
    free(h);
}

Handle *live_handle(void *p) {
    if(!g_handles.count(p)) die("used an unknown handle");
    return (Handle *)p;
}

void release_object(Obj *o);

void clear_var(PA_Variable *v) {
    if(v->fFiller == kPoisonMark) {
        g_poison_clears++;
        fprintf(stderr, "  cleared the uninitialised result of a failed engine call\n");
        v->fType = eVK_Undefined;
        v->fFiller = 0;
        return;
    }
    switch(v->fType) {
        case eVK_Unistring: free_string(&v->uValue.fString); break;
        case eVK_Picture: free_picture(v->uValue.fPicture); break;
        case eVK_Blob: free_handle((Handle *)v->uValue.fBlob.fHandle); break;
        case eVK_Object: if(v->uValue.fObject) release_object((Obj *)v->uValue.fObject); break;
        case eVK_Collection: if(v->uValue.fCollection) release_object((Obj *)v->uValue.fCollection); break;
        default: break;
    }
    v->fType = eVK_Undefined;
}

Obj *live(void *p) {
    if(!g_objects.count((Obj *)p)) die("used a dead object/collection");
    return (Obj *)p;
}

PA_Variable undefined_var() { PA_Variable v; memset(&v, 0, sizeof(v)); v.fType = eVK_Undefined; return v; }

PA_Variable copy_var(const PA_Variable &src) {
    PA_Variable dst = src;
    switch(src.fType) {
        case eVK_Unistring:
            dst.uValue.fString = new_string(src.uValue.fString.fString, src.uValue.fString.fLength);
            break;
        case eVK_Picture:
            if(!g_pictures.count(PA_Picture(src.uValue.fPicture))) die("stored a dead picture");
            dst.uValue.fPicture = new_picture();
            break;
        case eVK_Object:
            if(src.uValue.fObject) live(src.uValue.fObject)->rc++;
            break;
        case eVK_Collection:
            if(src.uValue.fCollection) live(src.uValue.fCollection)->rc++;
            break;
        default: break;
    }
    return dst;
}

Obj *new_object(bool coll = false) {
    Obj *o = new Obj();
    o->rc = 1;
    o->coll = coll;
    g_objects.insert(o);
    return o;
}

void release_object(Obj *o) {
    if(!g_objects.count(o)) { violation("double_release_object"); return; }
    if(--o->rc == 0) {
        for(auto &kv : o->props) clear_var(&kv.second);
        for(auto &v : o->items) clear_var(&v);
        g_objects.erase(o);
        delete o;
    }
}

u16 key_of(const PA_Unistring &k) { return u16(k.fString, k.fLength); }

PA_Variable object_result(Obj *o) {
    PA_Variable v = undefined_var();
    v.fType = o->coll ? eVK_Collection : eVK_Object;
    if(o->coll) v.uValue.fCollection = (PA_CollectionRef)o; else v.uValue.fObject = (PA_ObjectRef)o;
    return v;
}

void execute_command(EngineBlock *eb) {
    PA_Variable **params = (PA_Variable **)eb->fPtr1;
    PA_Variable *ret = (PA_Variable *)eb->fPtr2;
    switch(eb->fParam1) {
        case 1217: { /* JSON Stringify */
            live(params[0]->uValue.fObject);
            if(g_fail_stringify) { poison(ret); eb->fError = -1; break; }
            u16 json = widen("{}");
            ret->fType = eVK_Unistring;
            ret->uValue.fString = new_string(json.c_str(), json.size());
            break;
        }
        case 1225: { /* OB Copy (PA_DuplicateObject) */
            Obj *src = live(params[0]->uValue.fObject);
            Obj *dst = new_object(false);
            for(auto &kv : src->props) dst->props[kv.first] = copy_var(kv.second);
            *ret = object_result(dst);
            break;
        }
        case 1231: { /* OB Is defined */
            Obj *o = live(params[0]->uValue.fObject);
            ret->fType = eVK_Boolean;
            ret->uValue.fBoolean = o->props.count(key_of(params[1]->uValue.fString)) ? 1 : 0;
            break;
        }
        case 1471: *ret = object_result(new_object(false)); break; /* New Object */
        case 1472: *ret = object_result(new_object(true)); break; /* New Collection */
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected PA_ExecuteCommandByID %d", (int)eb->fParam1);
            die(msg);
        }
    }
}

void get_obj_value(EngineBlock *eb) {
    PA_Variable *out = (PA_Variable *)eb->fPtr2;
    if(!eb->fPtr1) { violation("property_of_null_object"); *out = undefined_var(); eb->fError = -1; return; }
    Obj *o = live(eb->fPtr1);
    if(eb->fShort == eVK_Collection) {
        if(!o->coll) die("collection get on an object");
        if(eb->fLongint == -1) { /* PA_GetCollectionLength: "length" */
            if(narrow(key_of(eb->fUniString1)) != "length") die("unexpected collection property");
            *out = undefined_var();
            out->fType = eVK_Longint;
            out->uValue.fLongint = (PA_long32)o->items.size();
            return;
        }
        if(eb->fLongint == g_fail_index) { poison(out); eb->fError = -1; return; }
        size_t i = (size_t)eb->fLongint;
        *out = i < o->items.size() ? copy_var(o->items[i]) : undefined_var();
        return;
    }
    if(o->coll) die("object get on a collection");
    u16 k = key_of(eb->fUniString1);
    if(!g_fail_key.empty() && narrow(k) == g_fail_key) { poison(out); eb->fError = -1; return; }
    auto it = o->props.find(k);
    *out = it == o->props.end() ? undefined_var() : copy_var(it->second);
}

void engine(short selector, EngineBlock *eb) {
    switch(selector) {
        case EX_CREATE_UNISTRING: {
            const PA_Unichar *src = (const PA_Unichar *)eb->fHandle;
            size_t len = 0;
            while(src[len]) len++;
            eb->fUniString1 = new_string(src, len);
            break;
        }
        case EX_DISPOSE_UNISTRING:
            free_string(&eb->fUniString1);
            break;
        case EX_VARIABLE_TO_STRING: {
            PA_Variable *v = (PA_Variable *)eb->fHandle;
            if(v->fFiller == kPoisonMark) violation("read_uninitialised_result");
            if(v->fType == eVK_Unistring) eb->fUniString1 = v->uValue.fString; /* borrowed */
            break;
        }
        case EX_CREATE_PICTURE:
            eb->fPicture = new_picture();
            break;
        case EX_DISPOSE_PICTURE:
            free_picture(eb->fPicture);
            break;
        case EX_CLEAR_VARIABLE:
            clear_var((PA_Variable *)eb->fPtr1);
            break;
        case EX_GET_INFORMATION:
            eb->fParam2 = 0x00002000; /* 4D v20: PA_CreateObject uses New Object */
            break;
        case EX_SET_OBJ_VALUE: {
            Obj *o = live(eb->fPtr1);
            if(eb->fShort == eVK_Collection) die("collection set not modelled");
            PA_Variable value = copy_var(*(PA_Variable *)eb->fPtr2);
            u16 k = key_of(eb->fUniString1);
            auto it = o->props.find(k);
            if(it != o->props.end()) clear_var(&it->second);
            o->props[k] = value;
            break;
        }
        case EX_GET_OBJ_VALUE:
            get_obj_value(eb);
            break;
        case EX_EXECUTE_COMMAND_BY_ID:
            execute_command(eb);
            break;
        case EX_HANDLE_MANAGER: {
            switch(eb->fParam1) {
                case 1: eb->fHandle = (PA_Handle)new_handle((size_t)eb->fParam2); break;
                case 2: free_handle((Handle *)eb->fHandle); break;
                case 3: {
                    Handle *h = live_handle(eb->fHandle);
                    size_t n = (size_t)eb->fParam2;
                    char *d = (char *)calloc(1, n ? n : 1);
                    memcpy(d, h->data, n < h->size ? n : h->size);
                    free(h->data);
                    h->data = d;
                    h->size = n;
                    break;
                }
                case 4: eb->fParam2 = (sLONG_PTR)live_handle(eb->fHandle)->size; break;
                case 5: { Handle *h = live_handle(eb->fHandle); h->locks++; eb->fParam3 = (sLONG_PTR)h->data; break; }
                case 6: {
                    Handle *h = live_handle(eb->fHandle);
                    if(h->locks <= 0) violation("unlock_of_unlocked_handle"); else h->locks--;
                    break;
                }
                default: die("unexpected handle manager op");
            }
            break;
        }
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unmodelled engine selector %d", selector);
            die(msg);
        }
    }
}

/* ---------- test helpers ---------- */

PA_Unistring U(const char *ascii) {
    u16 s = widen(ascii);
    return new_string(s.c_str(), s.size());
}

void put(Obj *o, const char *key, PA_Variable v) {
    PA_Unistring k = U(key);
    PA_SetObjectProperty((PA_ObjectRef)o, &k, v);
    PA_DisposeUnistring(&k);
}

PA_Variable text_var(const char *value) {
    PA_Variable v = undefined_var();
    PA_Unistring u = U(value);
    PA_SetStringVariable(&v, &u);
    return v;
}

void put_text(Obj *o, const char *key, const char *value) {
    PA_Variable v = text_var(value);
    put(o, key, v);
    PA_ClearVariable(&v);
}

void put_real(Obj *o, const char *key, double d) {
    PA_Variable v = undefined_var();
    PA_SetRealVariable(&v, d);
    put(o, key, v);
}

void put_bool(Obj *o, const char *key, bool b) {
    PA_Variable v = undefined_var();
    PA_SetBooleanVariable(&v, b);
    put(o, key, v);
}

Obj *put_object(Obj *o, const char *key, bool coll) {
    Obj *c = new_object(coll);
    PA_Variable v = object_result(c);
    put(o, key, v);
    PA_ClearVariable(&v);
    return c;
}

/* Appends an owned engine value; the collection takes it over. */
void push(Obj *coll, PA_Variable v) { coll->items.push_back(v); }

bool check(bool ok, const char *what) {
    if(!ok) fprintf(stderr, "  check failed: %s\n", what);
    return ok;
}

bool string_alive(const char *ascii) {
    u16 w = widen(ascii);
    for(void *p : g_strings) if(u16((PA_Unichar *)p) == w) return true;
    return false;
}

bool no_leaks() {
    bool ok = true;
    for(void *p : g_strings) {
        fprintf(stderr, "  LEAK: engine string \"%s\"\n", narrow(u16((PA_Unichar *)p)).c_str());
        ok = false;
    }
    if(!g_pictures.empty()) { fprintf(stderr, "  LEAK: %zu engine picture(s)\n", g_pictures.size()); ok = false; }
    if(!g_objects.empty()) { fprintf(stderr, "  LEAK: %zu engine object(s)/collection(s)\n", g_objects.size()); ok = false; }
    if(!g_handles.empty()) { fprintf(stderr, "  LEAK: %zu engine handle(s)\n", g_handles.size()); ok = false; }
    if(g_poison_clears) { fprintf(stderr, "  %d failed result(s) cleared\n", g_poison_clears); ok = false; }
    if(!g_violations.empty()) ok = false;
    return ok;
}

struct TidyResult {
    double status = -99, errorCount = -99;
    std::string html, info;
    bool has_html = false, has_info = false;
};

std::string text_prop(Obj *o, const char *key, bool *has) {
    auto it = o->props.find(widen(key));
    *has = it != o->props.end() && it->second.fType == eVK_Unistring;
    return *has ? narrow(u16(it->second.uValue.fString.fString, it->second.uValue.fString.fLength)) : std::string();
}

double real_prop(Obj *o, const char *key) {
    auto it = o->props.find(widen(key));
    return it != o->props.end() && it->second.fType == eVK_Real ? it->second.uValue.fReal : -99;
}

/* Calls the real `Tidy` command (selector 1) with a 4D-owned BLOB holding
   synthetic HTML and a borrowed options object, then releases the result
   the way 4D would after copying it into the caller's variable. */
TidyResult call_tidy(const char *html, Obj *options) {
    PA_Handle h = NULL;
    if(html) {
        size_t n = strlen(html);
        h = (PA_Handle)new_handle(n);
        memcpy(((Handle *)h)->data, html, n);
    }
    PA_ObjectRef p2 = (PA_ObjectRef)options;
    PA_ObjectRef result = NULL;
    void *args[2] = { &h, &p2 };
    PluginBlock pb;
    pb.fParameters = args;
    pb.fResult = &result;
    PluginMain(1, &pb);

    TidyResult r;
    if(h) {
        if(live_handle(h)->locks != 0) violation("caller_blob_left_locked");
        free_handle((Handle *)h);
    }
    if(!result) { violation("no_result_object"); return r; }
    Obj *o = live(result);
    r.status = real_prop(o, "status");
    r.errorCount = real_prop(o, "errorCount");
    r.html = text_prop(o, "html", &r.has_html);
    r.info = text_prop(o, "info", &r.has_info);
    release_object(o);
    return r;
}

const char *kDoc = "<!DOCTYPE html><html><head><title>t</title></head><body><p>hello</p></body></html>";

/* ---------- LIVE defects (Tidy) ---------- */

/* LIVE: every Tidy call stores html/info through ob_set_s(wchar_t*, char*),
   which overwrote the empty Unistring allocated by
   PA_CreateVariable(eVK_Unistring). */
bool defect_live_tidy_result_empty_owner_leaks() {
    TidyResult r = call_tidy(kDoc, NULL);
    bool ok = check(r.errorCount == 0 && r.has_html && r.html.find("hello") != std::string::npos, "html produced");
    ok &= check(r.has_info, "info produced");
    ok &= check(!string_alive(""), "no empty Unistring owner left behind");
    return no_leaks() && ok;
}

/* LIVE: the blockTags/emptyTags/inlineTags/preTags loops never cleared the
   owned copy returned by PA_GetCollectionElement. */
bool defect_live_tidy_tag_elements_not_cleared() {
    Obj *o = new_object(false);
    push(put_object(o, "blockTags", true), text_var("x-block"));
    push(put_object(o, "emptyTags", true), text_var("x-empty"));
    push(put_object(o, "inlineTags", true), text_var("x-inline"));
    push(put_object(o, "preTags", true), text_var("x-pre"));
    TidyResult r = call_tidy("<!DOCTYPE html><html><head><title>t</title></head><body>"
                             "<x-block><x-inline>a</x-inline></x-block><x-pre>b</x-pre><x-empty></body></html>", o);
    bool ok = check(r.errorCount == 0 && r.html.find("<x-block>") != std::string::npos
                    && r.html.find("<x-inline>") != std::string::npos
                    && r.html.find("<x-pre>") != std::string::npos
                    && r.html.find("<x-empty>") != std::string::npos, "custom tags honoured");
    ok &= check(g_objects.count(o) && o->rc == 1, "caller's options object untouched");
    release_object(o);
    ok &= check(!string_alive("x-block") && !string_alive("x-empty") && !string_alive("x-inline") && !string_alive("x-pre"),
                "collection element copies released");
    return no_leaks() && ok;
}

/* LIVE: a non-text element (here an object) was retained and never released. */
bool defect_live_tidy_tag_non_text_element_not_released() {
    Obj *o = new_object(false);
    Obj *tags = put_object(o, "blockTags", true);
    Obj *elem = new_object(false);
    push(tags, object_result(elem));
    TidyResult r = call_tidy(kDoc, o);
    bool ok = check(r.errorCount == 0, "tidied");
    ok &= check(elem->rc == 1, "element reference released");
    release_object(o);
    return no_leaks() && ok;
}

/* LIVE: ob_get_c never released the retained collection reference, so the
   caller's tag collections outlived the options object. */
bool defect_live_tidy_tag_collection_ref_not_released() {
    Obj *o = new_object(false);
    Obj *t[4] = { put_object(o, "blockTags", true), put_object(o, "emptyTags", true),
                  put_object(o, "inlineTags", true), put_object(o, "preTags", true) };
    TidyResult r = call_tidy(kDoc, o);
    bool ok = check(r.errorCount == 0, "tidied");
    for(Obj *c : t) ok &= check(c->rc == 1, "collection reference borrowed from the options object");
    release_object(o);
    return no_leaks() && ok;
}

/* LIVE: ob_get_a never cleared the owned copy of text options. */
bool defect_live_tidy_text_options_not_cleared() {
    Obj *o = new_object(false);
    put_text(o, "altText", "alt-value");
    put_text(o, "CSSPrefix", "css-prefix");
    put_text(o, "doctype", "html5");
    put_text(o, "encoding", "utf8");
    put_text(o, "language", "en");
    TidyResult r = call_tidy(kDoc, o);
    bool ok = check(r.errorCount == 0 && r.has_html, "tidied");
    release_object(o);
    ok &= check(!string_alive("alt-value") && !string_alive("css-prefix") && !string_alive("html5")
                && !string_alive("utf8") && !string_alive("en"), "option copies released");
    return no_leaks() && ok;
}

/* LIVE: ob_get_b/ob_get_n never cleared their result; a wrong-kind (text)
   copy leaked. */
bool defect_live_tidy_wrong_kind_options_not_cleared() {
    Obj *o = new_object(false);
    put_text(o, "quiet", "yes");
    put_text(o, "wrapLen", "eighty");
    TidyResult r = call_tidy(kDoc, o);
    bool ok = check(r.errorCount == 0 && r.has_html, "tidied");
    release_object(o);
    ok &= check(!string_alive("yes") && !string_alive("eighty"), "wrong-kind copies released");
    return no_leaks() && ok;
}

/* HARDENING (LIVE): a failed PA_GetCollectionElement leaves its result
   uninitialised; Tidy must neither read it as a tag nor clear it. */
bool hardening_live_tidy_tag_element_error_untouched() {
    Obj *o = new_object(false);
    Obj *tags = put_object(o, "blockTags", true);
    push(tags, text_var("x-block"));
    g_fail_index = 0;
    TidyResult r = call_tidy(kDoc, o);
    g_fail_index = -2;
    bool ok = check(r.errorCount == 0 && r.has_html, "tidied");
    release_object(o);
    return no_leaks() && ok;
}

/* HARDENING (LIVE): a failed PA_GetObjectProperty leaves its result
   uninitialised; option parsing must neither read it nor clear it. */
bool hardening_live_tidy_option_error_untouched() {
    Obj *o = new_object(false);
    put_text(o, "altText", "alt-value");
    g_fail_key = "altText";
    TidyResult r = call_tidy(kDoc, o);
    g_fail_key.clear();
    bool ok = check(r.errorCount == 0 && r.has_html, "tidied");
    release_object(o);
    return no_leaks() && ok;
}

/* ---------- JSON helpers called directly ---------- */

/* HARDENING: on error PA_GetObjectProperty leaves its result uninitialised. */
bool hardening_ob_get_error_result_untouched() {
    Obj *o = new_object(false);
    put_text(o, "a", "x");
    put_real(o, "n", 1.0);
    put_bool(o, "b", true);
    put_object(o, "c", true);
    CUTF8String s;
    g_fail_key = "a"; bool ra = ob_get_a((PA_ObjectRef)o, L"a", &s);
    g_fail_key = "n"; double d = ob_get_n((PA_ObjectRef)o, L"n");
    g_fail_key = "b"; bool b = ob_get_b((PA_ObjectRef)o, L"b");
    g_fail_key = "c"; PA_CollectionRef c = ob_get_c((PA_ObjectRef)o, L"c");
    g_fail_key.clear();
    release_object(o);
    bool ok = check(!ra && s.empty() && d == 0.0 && !b && !c, "failed reads yield nothing");
    return no_leaks() && ok;
}

/* DORMANT: ob_set_s(char*, char*), ob_set_a and ob_set_p overwrote the
   value allocated by PA_CreateVariable(eVK_Unistring/eVK_Picture). */
bool defect_dormant_ob_set_empty_owner_leaks() {
    Obj *o = new_object(false);
    ob_set_s((PA_ObjectRef)o, "k1", "v1");
    ob_set_a((PA_ObjectRef)o, L"k2", L"v2");
    PA_Picture pic = PA_CreatePicture(NULL, 0);
    // ob_set_p consumes `pic` (shallow set, deep-copying set, then clear).
    ob_set_p((PA_ObjectRef)o, L"k3", pic);
    bool ok = check(o->props.size() == 3, "three properties set");
    release_object(o);
    return no_leaks() && ok;
}

/* HARDENING (DORMANT): ob_stringify cleared the 1217 result without
   checking the error. */
bool hardening_ob_stringify_error_untouched() {
    Obj *o = new_object(false);
    put_text(o, "a", "x");
    g_fail_stringify = true;
    CUTF8String json;
    ob_stringify((PA_ObjectRef)o, &json);
    g_fail_stringify = false;
    release_object(o);
    bool ok = check(json.empty(), "failed stringify yields nothing");
    return no_leaks() && ok;
}

/* ---------- controls (pass on both trees) ---------- */

/* No BLOB: Tidy returns an empty object and touches nothing else. */
bool control_tidy_no_blob() {
    Obj *o = new_object(false);
    put_bool(o, "quiet", true);
    TidyResult r = call_tidy(NULL, o);
    bool ok = check(r.status == -99 && !r.has_html && !r.has_info, "empty result");
    ok &= check(g_objects.count(o) && o->rc == 1, "caller's options object untouched");
    release_object(o);
    return no_leaks() && ok;
}

bool control_ob_set_get_scalars() {
    Obj *o = new_object(false);
    ob_set_n((PA_ObjectRef)o, L"n", 2.5);
    ob_set_b((PA_ObjectRef)o, L"b", true);
    ob_set_i((PA_ObjectRef)o, L"i", 7);
    ob_set_o((PA_ObjectRef)o, L"o", PA_CreateObject());
    bool ok = check(o->props.size() == 4, "four properties set");
    ok &= check(ob_get_n((PA_ObjectRef)o, L"n") == 2.5 && ob_get_b((PA_ObjectRef)o, L"b"), "real and boolean read back");
    ok &= check(ob_is_defined((PA_ObjectRef)o, L"n") && !ob_is_defined((PA_ObjectRef)o, L"x"), "ob_is_defined");
    release_object(o);
    return no_leaks() && ok;
}

bool control_ob_stringify() {
    Obj *o = new_object(false);
    put_text(o, "a", "x");
    CUTF8String json;
    ob_stringify((PA_ObjectRef)o, &json);
    release_object(o);
    bool ok = check(json == (const uint8_t *)"{}", "stringified");
    return no_leaks() && ok;
}

struct Test { const char *name; bool (*fn)(); };
const Test kTests[] = {
    { "defect_live_tidy_result_empty_owner_leaks", defect_live_tidy_result_empty_owner_leaks },
    { "defect_live_tidy_tag_elements_not_cleared", defect_live_tidy_tag_elements_not_cleared },
    { "defect_live_tidy_tag_non_text_element_not_released", defect_live_tidy_tag_non_text_element_not_released },
    { "defect_live_tidy_tag_collection_ref_not_released", defect_live_tidy_tag_collection_ref_not_released },
    { "defect_live_tidy_text_options_not_cleared", defect_live_tidy_text_options_not_cleared },
    { "defect_live_tidy_wrong_kind_options_not_cleared", defect_live_tidy_wrong_kind_options_not_cleared },
    { "hardening_live_tidy_tag_element_error_untouched", hardening_live_tidy_tag_element_error_untouched },
    { "hardening_live_tidy_option_error_untouched", hardening_live_tidy_option_error_untouched },
    { "hardening_ob_get_error_result_untouched", hardening_ob_get_error_result_untouched },
    { "defect_dormant_ob_set_empty_owner_leaks", defect_dormant_ob_set_empty_owner_leaks },
    { "hardening_ob_stringify_error_untouched", hardening_ob_stringify_error_untouched },
    { "control_tidy_no_blob", control_tidy_no_blob },
    { "control_ob_set_get_scalars", control_ob_set_get_scalars },
    { "control_ob_stringify", control_ob_stringify },
};

} // namespace

int main(int argc, char **argv) {
    gCall4D = engine;
    if(argc == 2 && !strcmp(argv[1], "--list")) {
        for(const Test &t : kTests) printf("%s\n", t.name);
        return 0;
    }
    if(argc != 2) { fprintf(stderr, "usage: %s --list | <test>\n", argv[0]); return 2; }
    for(const Test &t : kTests) {
        if(!strcmp(argv[1], t.name)) {
            bool ok = t.fn();
            printf("%s: %s\n", t.name, ok ? "PASS" : "FAIL");
            return ok ? 0 : 1;
        }
    }
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
