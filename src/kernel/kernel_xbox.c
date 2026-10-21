/*
 * kernel_xbox.c - Xbox Identity & Hardware Stubs
 *
 * Provides Xbox-specific exported data (hardware info, kernel version,
 * keys, image filename) and section loading stubs.
 *
 * Most Xbox-specific hardware data is stubbed with plausible retail
 * values. Crypto keys are zeroed since we don't need Xbox Live or
 * EEPROM-based encryption on PC.
 */

#include "kernel.h"
#include "apu.h"                /* mcpx_apu_xc_audio: XC_AUDIO */
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Exported Data Objects
 *
 * These are global variables exported by the Xbox kernel at known ordinals.
 * Game code accesses them directly via the thunk table.
 * ============================================================================ */

/* Hardware info - default report is a standard retail Xbox.
 * A target may override via xbox_kernel_set_version() or by patching the
 * global directly; no title-specific values are baked in. */
XBOX_HARDWARE_INFO xbox_HardwareInfo = {
    .Flags       = 0x00000020,  /* Retail Xbox */
    .GpuRevision = 0xD2,       /* NV2A D2 revision */
    .McpRevision = 0xD4,       /* MCPX D4 revision */
    .Reserved    = {0, 0}
};

/* Kernel version - default is a stock retail value. Titles built against a
 * different XDK can report their own through xbox_kernel_set_version(). */
XBOX_KRNL_VERSION xbox_KrnlVersion = {
    .Major = 1,
    .Minor = 0,
    .Build = 5849,
    .Qfe   = 1
};

/* Override the kernel version reported to game code (ordinal 324).
 * Match the XDK revision the target title was built with if it checks it. */
void xbox_kernel_set_version(USHORT major, USHORT minor, USHORT build, USHORT qfe)
{
    xbox_KrnlVersion.Major = major;
    xbox_KrnlVersion.Minor = minor;
    xbox_KrnlVersion.Build = build;
    xbox_KrnlVersion.Qfe   = qfe;
}

/* Crypto keys - zeroed, not needed for PC operation.
 * These are unique per-console on real hardware. */
UCHAR xbox_EEPROMKey[16]              = {0};
UCHAR xbox_HDKey[16]                  = {0};
UCHAR xbox_SignatureKey[16]           = {0};
UCHAR xbox_LANKey[16]                 = {0};
UCHAR xbox_AlternateSignatureKeys[16][16] = {{0}};

/* Public key data for Xbox Live signature verification - not needed */
UCHAR xbox_XePublicKeyData[284] = {0};

/* Image filename - the XBE path as seen by the kernel */
static char g_image_filename[] = "\\Device\\CdRom0\\default.xbe";
XBOX_ANSI_STRING xbox_XeImageFileName = {
    .Length        = sizeof(g_image_filename) - 1,
    .MaximumLength = sizeof(g_image_filename),
    .Buffer        = g_image_filename
};

/* Launch data page - used for title-to-title launches (e.g., Xbox Dashboard → game).
 * Allocated dynamically and zeroed for normal game boot. */
static XBOX_LAUNCH_DATA_PAGE g_launch_data_page = {0};
XBOX_LAUNCH_DATA_PAGE* xbox_LaunchDataPage = &g_launch_data_page;

/* ============================================================================
 * Section Loading
 *
 * XeLoadSection/XeUnloadSection manage on-demand loading of XBE sections.
 * On Xbox, some sections are demand-paged from disc. In our recompilation,
 * the entire executable is loaded into memory, so these are reference-counting
 * no-ops.
 * ============================================================================ */

NTSTATUS __stdcall xbox_XeLoadSection(PXBE_SECTION_HEADER Section)
{
    if (!Section)
        return STATUS_INVALID_PARAMETER;

    InterlockedIncrement(&Section->SectionReferenceCount);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_XBOX,
        "XeLoadSection: '%s' at %p (size=%u, refcount=%d)",
        Section->SectionName ? Section->SectionName : "<null>",
        Section->VirtualAddress, Section->VirtualSize,
        Section->SectionReferenceCount);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_XeUnloadSection(PXBE_SECTION_HEADER Section)
{
    if (!Section)
        return STATUS_INVALID_PARAMETER;

    LONG new_count = InterlockedDecrement(&Section->SectionReferenceCount);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_XBOX,
        "XeUnloadSection: '%s' (refcount=%d)",
        Section->SectionName ? Section->SectionName : "<null>",
        new_count);

    return STATUS_SUCCESS;
}

/* ============================================================================
 * EEPROM / Non-Volatile Settings
 *
 * ExQueryNonVolatileSetting / ExSaveNonVolatileSetting read and write EEPROM
 * settings. On real hardware these are stored in the 256-byte EEPROM on the
 * SMBus. For recompilation, we return sensible defaults:
 *   - Region: North America
 *   - Video: none of widescreen/480p/720p/1080i unless set (kernel.h,
 *     xbox_SetVideoSettings)
 *   - Language: English
 *   - Audio: the APU's speaker layout, stereo or 5.1 (Dolby Digital)
 *   - DVD Region: Region 1 (North America)
 * ============================================================================ */

/* ============================================================================
 * The dashboard's language setting
 * ==========================================================================*/

/* The language names the setting is given by, and the number of the Xbox
 * EEPROM encoding each means. The numbers are the console's: a title reads
 * the number and looks it up in its own table, so a title that keeps its
 * own numbering (Burnout 3's PAL build, which reads 6 for Italian) needs
 * the number of that table, not this one. A title that renumbers tells
 * the toolkit so (RECOMP_XBOX_LANGUAGE_ENCODING).
 */
typedef struct {
    const char *name;
    ULONG code;             /* the console's EEPROM encoding */
    ULONG alt;              /* the same language in the PAL build's encoding */
} xbox_language_entry;

static const xbox_language_entry s_languages[] = {
    { "japanese",           0,  2 },
    { "english",            1,  1 },
    { "german",             2,  3 },
    { "french",             3,  4 },
    { "spanish",            4,  5 },
    { "italian",            5,  6 },
    { "korean",             6,  7 },
    { "chinese_traditional", 7, 8 },
    { "chinese_simplified", 8,  9 },
    { "portuguese",         9, 10 },
    { "dutch",             10, 11 },
    { "swedish",           11, 12 },
};

static int word_is(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb)
            return 0;
    }
    return !*a && !*b;
}

/* The number the EEPROM carries for the language named in `text`.
 *
 * The name is matched first ("italian"), then a number, which is taken as
 * it is (the console's encoding, unless RECOMP_XBOX_LANGUAGE_ENCODING says
 * the title renumbers, in which case the number is looked up in that
 * encoding and translated). An unknown name falls back to English. */
static ULONG xbox_language_code(const char *text)
{
    size_t i;
    int pal_encoding = 0;
    const char *enc = getenv("RECOMP_XBOX_LANGUAGE_ENCODING");
    long parsed;

    if (!text || !*text)
        return 1;                          /* English */

    if (enc && (word_is(enc, "pal") || word_is(enc, "europe")))
        pal_encoding = 1;

    for (i = 0; i < sizeof s_languages / sizeof s_languages[0]; i++) {
        if (word_is(text, s_languages[i].name))
            return pal_encoding ? s_languages[i].alt : s_languages[i].code;
    }

    /* A number: the console's encoding, or the title's own. */
    parsed = strtol(text, NULL, 10);
    if (text[0] >= '0' && text[0] <= '9' && parsed >= 0 && parsed <= 11) {
        if (pal_encoding) {
            /* The number is the PAL build's; translate it to the console's. */
            for (i = 0; i < sizeof s_languages / sizeof s_languages[0]; i++)
                if (s_languages[i].alt == (ULONG)parsed)
                    return s_languages[i].code;
            return (ULONG)parsed;
        }
        return (ULONG)parsed;
    }

    xbox_log(XBOX_LOG_WARN, XBOX_LOG_XBOX,
        "RECOMP_XBOX_LANGUAGE='%s' is not a language; English", text);
    return 1;                              /* English */
}

/* The language the EEPROM carries. Set by the title (xbox_SetLanguage),
 * else RECOMP_XBOX_LANGUAGE, else English. */
static ULONG g_xc_language = 0xFFFFFFFFu;

void xbox_SetLanguage(const char *name)
{
    g_xc_language = xbox_language_code(name);
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_XBOX,
        "EEPROM language: %s -> %u", name ? name : "(none)", (unsigned)g_xc_language);
}

ULONG xbox_GetLanguage(void)
{
    if (g_xc_language != 0xFFFFFFFFu)
        return g_xc_language;
    {
        const char *lang = getenv("RECOMP_XBOX_LANGUAGE");
        if (lang && *lang)
            return xbox_language_code(lang);
    }
    return 1;                              /* English */
}

NTSTATUS __stdcall xbox_ExQueryNonVolatileSetting(
    ULONG ValueIndex, PULONG Type, PVOID Value, ULONG ValueLength, PULONG ResultLength)
{
    if (!Value)
        return STATUS_INVALID_PARAMETER;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_XBOX,
        "ExQueryNonVolatileSetting: index=0x%02X len=%u", ValueIndex, ValueLength);

    switch (ValueIndex) {
    case XC_LANGUAGE:
        /* The language the dashboard would have set in the EEPROM.
         *
         * RECOMP_XBOX_LANGUAGE names it ("english", "italian", "french",
         * ...) and the number of the Xbox EEPROM encoding is accepted too.
         * The numbers are the console's (the table the title reads at
         * 0x3A7BC4 in the NTSC-U executable):
         *
         *   0=Japanese 1=English 2=German  3=French 4=Spanish 5=Italian
         *   6=Korean   7=Chinese (Traditional) 8=Chinese (Simplified)
         *   9=Portuguese
         *
         * A title reads the number and looks it up in its own table, so the
         * number that means "Italian" is the console's, not the title's.
         * A title that keeps its own numbering (the PAL build of Burnout 3
         * reads 6 for Italian) needs its own table; the title says which
         * numbering it uses (RECOMP_XBOX_LANGUAGE_ENCODING). */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = xbox_GetLanguage();
            if (Type) *Type = 4; /* REG_DWORD */
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_VIDEO:
        /* The dashboard's video settings, XC_VIDEO_AV_* bits (kernel.h):
         * stock none, else display.widescreen / display.hdtv. */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = xbox_GetVideoSettings();
            if (Type) *Type = 4; /* REG_DWORD */
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_AUDIO:
        /* The dashboard's audio setting, the layout the APU mixes for
         * (RECOMP_AUDIO_SPEAKERS): stereo 0, or for 5.1 SURROUND |
         * ENABLE_AC3 (0x00010002), "Dolby Digital" ticked. XAPI's
         * XGetAudioFlags passes it on unchanged on our AV pack (it forces
         * mono on the RF pack, clears the AC-3/DTS bits on the composite
         * one: Burnout 3's sub_001D00F2), and RenderWare's DirectSound
         * driver takes 5.1 only with both bits (sub_0020A120: surround
         * alone is its matrixed mode 4, with AC-3 mode 5). No AC-3 is
         * encoded: the six feeds leave as LPCM (apu_speakers.c). */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = mcpx_apu_xc_audio();
            if (Type) *Type = 4; /* REG_DWORD */
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_P_CONTROL_GAMES:
    case XC_P_CONTROL_MOVIES:
        /* Unrestricted. Titles compare their XBE certificate's rating against
         * this and refuse to run (Halo boots to the dashboard) if the console
         * is configured more strictly, so 0 means "no restriction". */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = 0;
            if (Type) *Type = 4;
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_DVD_REGION:
        /* Region 1 (North America) */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = 1;
            if (Type) *Type = 4;
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_MISC:
        /* Misc flags: 0 = no special flags */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = 0;
            if (Type) *Type = 4;
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_TIMEZONE_BIAS:
        /* UTC-5 (Eastern Time) in minutes: -300 */
        if (ValueLength >= sizeof(LONG)) {
            *(PLONG)Value = -300;
            if (Type) *Type = 4;
            if (ResultLength) *ResultLength = sizeof(LONG);
        }
        break;

    default:
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_XBOX,
            "ExQueryNonVolatileSetting: unhandled index 0x%02X", ValueIndex);
        memset(Value, 0, ValueLength);
        if (Type) *Type = 4;
        if (ResultLength) *ResultLength = ValueLength;
        break;
    }

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_ExSaveNonVolatileSetting(
    ULONG ValueIndex, ULONG Type, PVOID Value, ULONG ValueLength)
{
    (void)Type;
    (void)Value;
    (void)ValueLength;

    xbox_log(XBOX_LOG_INFO, XBOX_LOG_XBOX,
        "ExSaveNonVolatileSetting: index=0x%02X len=%u (ignored - read-only on PC)",
        ValueIndex, ValueLength);

    /* Accept writes silently but don't persist them */
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Network / PHY
 *
 * PhyGetLinkState reports Ethernet link status: XNET_ETHERNET_LINK_ACTIVE
 * (0x01) and the speed and duplex bits, or 0 for no cable.
 *
 * There is no NIC here, so the answer is "no cable", as on a console with
 * nothing plugged in and in xemu with networking off (its default). Reporting
 * a link sent XNet into DHCP on a NIC that never answers: Burnout 3 polls
 * XNetGetTitleXnAddr on its boot loading screen until that gives up, and the
 * bar sat near the end for seconds (BO-164). With no link XNet reports it at
 * once and the title goes on.
 * ============================================================================ */

ULONG __stdcall xbox_PhyGetLinkState(BOOLEAN Verify)
{
    (void)Verify;
    return 0; /* no link */
}

NTSTATUS __stdcall xbox_PhyInitialize(BOOLEAN ForceReset, PVOID Param2)
{
    (void)ForceReset;
    (void)Param2;
    return STATUS_SUCCESS;
}
