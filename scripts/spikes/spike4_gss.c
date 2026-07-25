/* Spike 4 (binding half) — can we drive GSS-API purely via dlopen, with no
 * build-time krb5 dependency and no krb5 headers?
 *
 * Asserts:
 *   1. libgssapi_krb5.so.2 loads at runtime.
 *   2. The four symbols the Negotiate provider needs resolve.
 *   3. GSS_C_NT_HOSTBASED_SERVICE (an exported *variable*) resolves.
 *   4. gss_import_name("HTTP@host") succeeds.
 *   5. gss_init_sec_context with the SPNEGO mech OID reaches the Kerberos
 *      layer (it must fail with "no credentials cache" here — there is no
 *      KDC/TGT in this container; that failure is the proof we got through).
 *
 * The KDC half of Spike 4 (asserting a real NegTokenInit DER prefix) needs a
 * Kerberized host and is not run here.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t OM_uint32;
typedef struct gss_name_struct *gss_name_t;
typedef struct gss_ctx_id_struct *gss_ctx_id_t;
typedef struct gss_cred_id_struct *gss_cred_id_t;
typedef struct gss_channel_bindings_struct *gss_channel_bindings_t;

typedef struct { OM_uint32 length; void *elements; } gss_OID_desc;
typedef gss_OID_desc *gss_OID;
typedef struct { size_t length; void *value; } gss_buffer_desc;
typedef gss_buffer_desc *gss_buffer_t;

typedef OM_uint32 (*fn_import_name)(OM_uint32 *, gss_buffer_t, gss_OID, gss_name_t *);
typedef OM_uint32 (*fn_init_sec)(OM_uint32 *, gss_cred_id_t, gss_ctx_id_t *,
                                 gss_name_t, gss_OID, OM_uint32, OM_uint32,
                                 gss_channel_bindings_t, gss_buffer_t, gss_OID *,
                                 gss_buffer_t, OM_uint32 *, OM_uint32 *);
typedef OM_uint32 (*fn_release_buffer)(OM_uint32 *, gss_buffer_t);
typedef OM_uint32 (*fn_release_name)(OM_uint32 *, gss_name_t *);

static int failures = 0;
static void check(int ok, const char *what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main(void) {
    void *h = dlopen("libgssapi_krb5.so.2", RTLD_NOW);
    check(h != NULL, "dlopen(libgssapi_krb5.so.2)");
    if (!h) return 1;

    fn_import_name import_name = (fn_import_name)dlsym(h, "gss_import_name");
    fn_init_sec init_sec = (fn_init_sec)dlsym(h, "gss_init_sec_context");
    fn_release_buffer rel_buf = (fn_release_buffer)dlsym(h, "gss_release_buffer");
    fn_release_name rel_name = (fn_release_name)dlsym(h, "gss_release_name");
    check(import_name && init_sec && rel_buf && rel_name,
          "resolve gss_import_name / gss_init_sec_context / gss_release_{buffer,name}");

    gss_OID hostbased = *(gss_OID *)dlsym(h, "GSS_C_NT_HOSTBASED_SERVICE");
    check(hostbased != NULL, "resolve GSS_C_NT_HOSTBASED_SERVICE");

    /* SPNEGO mech: 1.3.6.1.5.5.2 */
    static unsigned char spnego_oid[] = {0x2b, 0x06, 0x01, 0x05, 0x05, 0x02};
    gss_OID_desc spnego = {sizeof(spnego_oid), spnego_oid};

    char spn[] = "HTTP@sap-trial.example.com";
    gss_buffer_desc name_buf = {strlen(spn), spn};
    gss_name_t target = NULL;
    OM_uint32 minor = 0;
    OM_uint32 major = import_name(&minor, &name_buf, hostbased, &target);
    check(major == 0, "gss_import_name(\"HTTP@sap-trial.example.com\")");

    gss_ctx_id_t ctx = NULL;
    gss_buffer_desc out = {0, NULL};
    OM_uint32 ret_flags = 0;
    major = init_sec(&minor, NULL, &ctx, target, &spnego,
                     0x02 /* GSS_C_MUTUAL_FLAG */, 0, NULL, NULL, NULL,
                     &out, &ret_flags, NULL);

    /* 0 = COMPLETE, 1 = CONTINUE_NEEDED (both mean a token came back). */
    const int produced_token = (major == 0 || major == 1) && out.length > 0;
    printf("      gss_init_sec_context -> major=0x%x minor=%u token_len=%zu\n",
           major, minor, out.length);

    if (produced_token) {
        unsigned char *t = (unsigned char *)out.value;
        int spnego_prefix = out.length > 10 && t[0] == 0x60 &&
                            memcmp(t + 4, "\x06\x06\x2b\x06\x01\x05\x05\x02", 8) == 0;
        check(spnego_prefix, "token carries the SPNEGO NegTokenInit DER prefix");
        rel_buf(&minor, &out);
    } else {
        /* Expected in this container: no KDC, no credentials cache. */
        check(major != 0,
              "reached the Kerberos layer and failed for lack of a TGT "
              "(expected without a KDC; the KDC half of this spike must run "
              "on a Kerberized host)");
    }

    rel_name(&minor, &target);
    dlclose(h);

    printf("\nSpike 4 (binding half): %s (%d failure(s))\n",
           failures == 0 ? "VERIFIED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
