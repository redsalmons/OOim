// vswhere-shim: fake vswhere.exe for copied/unregistered VS installs.
//
// Root cause it fixes: VS at E:\vsc was copied, not installed via the VS
// Installer, so the real vswhere.exe returns zero instances. vcpkg, CMake's
// "Visual Studio" generators, and flutter all locate VS through vswhere and
// therefore fail. This shim reports the E:\vsc instance in both XML (vcpkg,
// CMake) and JSON (flutter) formats.
//
// Build (in an MSVC arm64 environment, e.g. after vcvarsall.bat arm64):
//   cl.exe /nologo /O2 vswhere_shim.c /Fe:vswhere.exe
//
// Deploy (requires admin):
//   copy vswhere.exe "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
// Keep a backup of the original vswhere.exe first!

#include <stdio.h>
#include <string.h>

#define VS_PATH "E:\\vsc"
#define VS_VERSION "17.14.37314.3"

static int wants_json(int argc, char** argv)
{
    for (int i = 1; i < argc - 1; i++) {
        if (_stricmp(argv[i], "-format") == 0 && _stricmp(argv[i + 1], "json") == 0)
            return 1;
    }
    // flutter passes "-format", "json" as well; also accept -format=json
    for (int i = 1; i < argc; i++) {
        if (_strnicmp(argv[i], "-format=", 8) == 0 && _stricmp(argv[i] + 8, "json") == 0)
            return 1;
    }
    return 0;
}

static void print_xml(void)
{
    printf("<?xml version=\"1.0\"?>\n");
    printf("<instances>\n");
    printf("  <instance>\n");
    printf("    <instanceId>oim.vsc.copied</instanceId>\n");
    printf("    <installDate>2025-01-01T00:00:00Z</installDate>\n");
    printf("    <installationName>VisualStudio/17.14.0</installationName>\n");
    printf("    <installationPath>%s</installationPath>\n", VS_PATH);
    printf("    <installationVersion>%s</installationVersion>\n", VS_VERSION);
    printf("    <productId>Microsoft.VisualStudio.Product.Community</productId>\n");
    printf("    <productPath>%s\\Common7\\IDE\\devenv.exe</productPath>\n", VS_PATH);
    printf("    <state>4294967295</state>\n");
    printf("    <isComplete>1</isComplete>\n");
    printf("    <isLaunchable>1</isLaunchable>\n");
    printf("    <isPrerelease>0</isPrerelease>\n");
    printf("    <isRebootRequired>0</isRebootRequired>\n");
    printf("    <displayName>Visual Studio Community 2022</displayName>\n");
    printf("    <channelId>VisualStudio.17.Release</channelId>\n");
    printf("    <catalog>\n");
    printf("      <productLine>Dev17</productLine>\n");
    printf("      <productLineVersion>17</productLineVersion>\n");
    printf("      <productDisplayVersion>17.14.0</productDisplayVersion>\n");
    printf("      <productSemanticVersion>17.14.0+37314.3</productSemanticVersion>\n");
    printf("      <productMilestone>RTW</productMilestone>\n");
    printf("      <productMilestoneIsPreRelease>False</productMilestoneIsPreRelease>\n");
    printf("    </catalog>\n");
    printf("  </instance>\n");
    printf("</instances>\n");
}

static void print_json(void)
{
    printf("[\n");
    printf("  {\n");
    printf("    \"instanceId\": \"oim.vsc.copied\",\n");
    printf("    \"installDate\": \"2025-01-01T00:00:00Z\",\n");
    printf("    \"installationName\": \"VisualStudio/17.14.0\",\n");
    printf("    \"installationPath\": \"E:\\\\vsc\",\n");
    printf("    \"installationVersion\": \"%s\",\n", VS_VERSION);
    printf("    \"productId\": \"Microsoft.VisualStudio.Product.Community\",\n");
    printf("    \"productPath\": \"E:\\\\vsc\\\\Common7\\\\IDE\\\\devenv.exe\",\n");
    printf("    \"state\": 4294967295,\n");
    printf("    \"isComplete\": true,\n");
    printf("    \"isLaunchable\": true,\n");
    printf("    \"isPrerelease\": false,\n");
    printf("    \"isRebootRequired\": false,\n");
    printf("    \"displayName\": \"Visual Studio Community 2022\",\n");
    printf("    \"description\": \"Copied VS install (vswhere-shim)\",\n");
    printf("    \"channelId\": \"VisualStudio.17.Release\",\n");
    printf("    \"channelUri\": \"https://aka.ms/vs/17/release/channel\",\n");
    printf("    \"enginePath\": \"C:\\\\Program Files (x86)\\\\Microsoft Visual Studio\\\\Installer\\\\setup.exe\",\n");
    printf("    \"releaseNotes\": \"https://learn.microsoft.com/en-us/visualstudio/releases/2022/release-notes\",\n");
    printf("    \"catalog\": {\n");
    printf("      \"buildBranch\": \"d17.14\",\n");
    printf("      \"buildVersion\": \"%s\",\n", VS_VERSION);
    printf("      \"id\": \"VisualStudio/17.14.0\",\n");
    printf("      \"localBuild\": \"build-lab\",\n");
    printf("      \"manifestName\": \"VisualStudio\",\n");
    printf("      \"manifestType\": \"installer\",\n");
    printf("      \"productDisplayVersion\": \"17.14.0\",\n");
    printf("      \"productLine\": \"Dev17\",\n");
    printf("      \"productLineVersion\": \"17\",\n");
    printf("      \"productMilestone\": \"RTW\",\n");
    printf("      \"productMilestoneIsPreRelease\": \"False\",\n");
    printf("      \"productName\": \"Visual Studio\",\n");
    printf("      \"productPatchVersion\": \"0\",\n");
    printf("      \"productSemanticVersion\": \"17.14.0+37314.3\",\n");
    printf("      \"requiredEngineVersion\": \"17.14.0\"\n");
    printf("    },\n");
    printf("    \"properties\": {\n");
    printf("      \"campaignId\": \"\",\n");
    printf("      \"channelManifestId\": \"VisualStudio.17.Release/17.14.0\",\n");
    printf("      \"nickname\": \"\",\n");
    printf("      \"setupEngineFilePath\": \"C:\\\\Program Files (x86)\\\\Microsoft Visual Studio\\\\Installer\\\\setup.exe\"\n");
    printf("    },\n");
    printf("    \"packages\": [\n");
    printf("      { \"id\": \"Microsoft.VisualStudio.Workload.NativeDesktop\" },\n");
    printf("      { \"id\": \"Microsoft.VisualStudio.Component.VC.Tools.x86.x64\" },\n");
    printf("      { \"id\": \"Microsoft.VisualStudio.Component.VC.Tools.ARM64\" },\n");
    printf("      { \"id\": \"Microsoft.VisualStudio.Component.VC.Tools.ARM64EC\" },\n");
    printf("      { \"id\": \"Microsoft.VisualStudio.Component.Windows11SDK.26100\" },\n");
    printf("      { \"id\": \"Microsoft.VisualStudio.Component.VC.CMake.Project\" },\n");
    printf("      { \"id\": \"Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core\" }\n");
    printf("    ]\n");
    printf("  }\n");
    printf("]\n");
}

/* -property <name> support: return the bare property value, one per line.
 * VsDevCmd.bat calls `vswhere -property catalog_productSemanticVersion -path <dir>`
 * to detect VSCMD_VER, and expects the raw value (split on '+' takes the semver). */
static const char* find_property(int argc, char** argv)
{
    static const struct { const char* name; const char* value; } props[] = {
        { "instanceId",              "oim.vsc.copied" },
        { "installDate",             "2025-01-01T00:00:00Z" },
        { "installationName",        "VisualStudio/17.14.0" },
        { "installationPath",        VS_PATH },
        { "installationVersion",     VS_VERSION },
        { "productId",               "Microsoft.VisualStudio.Product.Community" },
        { "productPath",             VS_PATH "\\Common7\\IDE\\devenv.exe" },
        { "state",                   "4294967295" },
        { "isComplete",              "1" },
        { "isLaunchable",            "1" },
        { "isPrerelease",            "0" },
        { "isRebootRequired",        "0" },
        { "displayName",             "Visual Studio Community 2022" },
        { "channelId",               "VisualStudio.17.Release" },
        { "channelUri",              "https://aka.ms/vs/17/release/channel" },
        /* catalog.* properties; vsdevcmd uses catalog_productSemanticVersion
         * (underscores). real vswhere accepts both '.' and '_' separators. */
        { "catalog.productDisplayVersion",   "17.14.0" },
        { "catalog_productDisplayVersion",   "17.14.0" },
        { "catalog.productSemanticVersion",  "17.14.0+37314.3" },
        { "catalog_productSemanticVersion",  "17.14.0+37314.3" },
        { "catalog.productLineVersion",      "17" },
        { "catalog_productLineVersion",      "17" },
        { "catalog.productLine",             "Dev17" },
        { "catalog_productLine",             "Dev17" },
        { "catalog.buildVersion",            VS_VERSION },
        { "catalog_buildVersion",            VS_VERSION },
        { "properties.nickname",             "" },
        { "properties_nickname",             "" },
        { NULL, NULL }
    };
    const char* prop = NULL;
    for (int i = 1; i < argc - 1; i++) {
        if (_stricmp(argv[i], "-property") == 0) { prop = argv[i + 1]; break; }
    }
    if (!prop) return NULL;
    for (int i = 0; props[i].name; i++) {
        if (_stricmp(props[i].name, prop) == 0)
            return props[i].value;
    }
    return ""; /* known-arg form but unknown property: emit empty line */
}

int main(int argc, char** argv)
{
    const char* pv = find_property(argc, argv);
    if (pv) {
        printf("%s\n", pv);
    } else if (wants_json(argc, argv)) {
        print_json();
    } else {
        print_xml();
    }
    return 0;
}
