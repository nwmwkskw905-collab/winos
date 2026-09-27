#!/usr/bin/env python3
"""Gera Portico.xcodeproj/project.pbxproj a partir da árvore de fontes.

Uso: python3 scripts/gen_xcodeproj.py
O projeto gerado referencia:
  - Sources/PorticoApp   (app iOS: SwiftUI/Metal/AVFoundation/GameController)
  - Sources/PorticoCore  (núcleo Swift)
  - Sources/PorticoRuntime (núcleo C11)
Alternativa: use XcodeGen com project.yml.
"""
import hashlib
import os
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUNDLE_ID = "io.portico.Portico"
DEPLOYMENT = "17.0"
SWIFT_VERSION = "5.0"


def xid(key: str) -> str:
    """UUID determinístico de 24 hex chars (formato Xcode)."""
    return hashlib.md5(key.encode()).hexdigest()[:24].upper()


def collect():
    app = sorted((ROOT / "Sources/PorticoApp").rglob("*.swift"))
    core = sorted((ROOT / "Sources/PorticoCore").rglob("*.swift"))
    csrc = sorted((ROOT / "Sources/PorticoRuntime/src").glob("*.c"))
    cinc = sorted((ROOT / "Sources/PorticoRuntime/include/portico").glob("*.h"))
    plist = ROOT / "Sources/PorticoApp/Info.plist"
    return app, core, csrc, cinc, plist


def main():
    app, core, csrc, cinc, plist = collect()
    swift_files = app + core
    sources = swift_files + csrc

    F = []  # file refs
    B = []  # build files
    lines = []
    w = lines.append

    # ---------- file references ----------
    def fileref(path: pathlib.Path, ftype: str) -> str:
        rid = xid("ref:" + str(path.relative_to(ROOT)))
        rel = path.relative_to(ROOT).as_posix()
        name = path.name
        F.append(
            f'\t\t{rid} /* {name} */ = {{isa = PBXFileReference; lastKnownFileType = {ftype}; '
            f'path = {name}; sourceTree = "<group>"; }};'
        )
        return rid

    def buildfile(path: pathlib.Path, phase: str, ftype: str) -> str:
        rid = fileref(path, ftype)
        bid = xid(f"build:{phase}:" + str(path.relative_to(ROOT)))
        name = path.name
        B.append(f"\t\t{bid} /* {name} in Sources */ = "
                 f"{{isa = PBXBuildFile; fileRef = {rid} /* {name} */; }};")
        return bid

    swift_ids = [buildfile(p, "src", "sourcecode.swift") for p in swift_files]
    c_ids = [buildfile(p, "src", "sourcecode.c.c") for p in csrc]
    hdr_ids = [fileref(p, "sourcecode.c.h") for p in cinc]
    plist_id = fileref(plist, "text.plist.xml")

    # frameworks
    frameworks = [
        ("Metal.framework", "wrapper.framework"),
        ("MetalKit.framework", "wrapper.framework"),
        ("AVFoundation.framework", "wrapper.framework"),
        ("GameController.framework", "wrapper.framework"),
    ]
    fw_build_ids = []
    fw_ref_ids = []
    for name, _ in frameworks:
        rid = xid("fwref:" + name)
        F.append(
            f'\t\t{rid} /* {name} */ = {{isa = PBXFileReference; lastKnownFileType = wrapper.framework; '
            f'name = {name}; path = System/Library/Frameworks/{name}; sourceTree = SDKROOT; }};'
        )
        bid = xid("fwbuild:" + name)
        B.append(f"\t\t{bid} /* {name} in Frameworks */ = "
                 f"{{isa = PBXBuildFile; fileRef = {rid} /* {name} */; }};")
        fw_ref_ids.append(rid)
        fw_build_ids.append(bid)
    libz_ref = xid("fwref:libz.tbd")
    F.append(
        f'\t\t{libz_ref} /* libz.tbd */ = {{isa = PBXFileReference; lastKnownFileType = "sourcecode.text-based-dylib-definition"; '
        f'name = libz.tbd; path = usr/lib/libz.tbd; sourceTree = SDKROOT; }};'
    )
    libz_build = xid("fwbuild:libz.tbd")
    B.append(f"\t\t{libz_build} /* libz.tbd in Frameworks */ = "
             f"{{isa = PBXBuildFile; fileRef = {libz_ref} /* libz.tbd */; }};")
    fw_build_ids.append(libz_build)

    # ---------- grupos ----------
    def group_children(paths):
        return [xid("ref:" + p.relative_to(ROOT).as_posix()) for p in paths]

    # grupos por diretório de PorticoApp e PorticoCore
    dir_groups = {}

    def ensure_group(dirpath: pathlib.Path) -> str:
        rel = dirpath.relative_to(ROOT).as_posix()
        gid = xid("group:" + rel)
        if gid in dir_groups:
            return gid
        dir_groups[gid] = (dirpath, [])
        return gid

    for p in swift_files + csrc + cinc + [plist]:
        parent = p.parent
        gid = ensure_group(parent)
        dir_groups[gid][1].append(xid("ref:" + p.relative_to(ROOT).as_posix()))

    # hierarquia: grupos pais vazios
    all_dirs = sorted({p.parent for p in swift_files + csrc + cinc + [plist]})
    for d in list(all_dirs):
        cur = d
        while cur != ROOT:
            ensure_group(cur)
            cur = cur.parent

    group_blocks = []
    for gid, (dirpath, kids) in sorted(dir_groups.items(), key=lambda kv: kv[0]):
        rel = dirpath.relative_to(ROOT).as_posix()
        name = dirpath.name
        # adiciona subgrupos como filhos
        sub = []
        for ogid, (od, _) in dir_groups.items():
            if od.parent == dirpath:
                sub.append(ogid)
        children = sorted(set(sub + kids))
        child_str = "\n".join(f"\t\t\t\t{c} /* * */," for c in children)
        group_blocks.append(
            f"\t\t{gid} /* {name} */ = {{\n"
            f"\t\t\tisa = PBXGroup;\n"
            f"\t\t\tchildren = (\n{child_str}\n\t\t\t);\n"
            f"\t\t\tpath = {name};\n"
            f"\t\t\tsourceTree = \"<group>\";\n"
            f"\t\t}};"
        )

    # grupos de sistema
    fw_group = xid("group:Frameworks")
    fw_children = "\n".join(
        f"\t\t\t\t{r} /* * */," for r in fw_ref_ids + [libz_ref]
    )
    group_blocks.append(
        f"\t\t{fw_group} /* Frameworks */ = {{\n"
        f"\t\t\tisa = PBXGroup;\n"
        f"\t\t\tchildren = (\n{fw_children}\n\t\t\t);\n"
        f"\t\t\tname = Frameworks;\n"
        f"\t\t\tsourceTree = \"<group>\";\n"
        f"\t\t}};"
    )

    # grupo raiz
    root_children = []
    for dname in ["Sources", "Tests"]:
        d = ROOT / dname
        if d.exists():
            root_children.append(xid("group:" + dname))
    root_children.append(fw_group)
    root_group = xid("group:root")
    root_child_str = "\n".join(f"\t\t\t\t{c} /* * */," for c in root_children)
    group_blocks.append(
        f"\t\t{root_group} = {{\n"
        f"\t\t\tisa = PBXGroup;\n"
        f"\t\t\tchildren = (\n{root_child_str}\n\t\t\t);\n"
        f"\t\t\tsourceTree = \"<group>\";\n"
        f"\t\t}};"
    )

    # ---------- fases e target ----------
    proj_id = xid("proj")
    target_id = xid("target:Portico")
    app_product_id = xid("product:Portico.app")
    sources_phase = xid("phase:Sources")
    fw_phase = xid("phase:Frameworks")
    cfg_list_proj = xid("cfglist:proj")
    cfg_list_tgt = xid("cfglist:tgt")
    cfg_debug_proj = xid("cfg:proj:Debug")
    cfg_release_proj = xid("cfg:proj:Release")
    cfg_debug_tgt = xid("cfg:tgt:Debug")
    cfg_release_tgt = xid("cfg:tgt:Release")

    F.append(
        f'\t\t{app_product_id} /* Portico.app */ = {{isa = PBXFileReference; '
        f'explicitFileType = wrapper.application; includeInIndex = 0; path = Portico.app; '
        f'sourceTree = BUILT_PRODUCTS_DIR; }};'
    )
    # produto no grupo Products
    products_group = xid("group:Products")
    group_blocks.append(
        f"\t\t{products_group} /* Products */ = {{\n"
        f"\t\t\tisa = PBXGroup;\n"
        f"\t\t\tchildren = (\n\t\t\t\t{app_product_id} /* Portico.app */,\n\t\t\t);\n"
        f"\t\t\tname = Products;\n"
        f"\t\t\tsourceTree = \"<group>\";\n"
        f"\t\t}};"
    )
    # Products na raiz
    # (regenera root com Products)
    group_blocks = [g for g in group_blocks if not g.startswith(f"\t\t{root_group} = ")]
    root_children.append(products_group)
    root_child_str = "\n".join(f"\t\t\t\t{c} /* * */," for c in root_children)
    group_blocks.append(
        f"\t\t{root_group} = {{\n"
        f"\t\t\tisa = PBXGroup;\n"
        f"\t\t\tchildren = (\n{root_child_str}\n\t\t\t);\n"
        f"\t\t\tsourceTree = \"<group>\";\n"
        f"\t\t}};"
    )

    src_list = "\n".join(
        f"\t\t\t\t{b} /* src */," for b in swift_ids + c_ids
    )
    fw_list = "\n".join(f"\t\t\t\t{b} /* fw */," for b in fw_build_ids)

    common_settings = (
        f"\t\t\t\tCLANG_ENABLE_MODULES = YES;\n"
        f"\t\t\t\tENABLE_STRICT_OBJC_MSGSEND = YES;\n"
        f"\t\t\t\tGCC_C_LANGUAGE_STANDARD = gnu11;\n"
        f"\t\t\t\tGCC_NO_COMMON_BLOCKS = YES;\n"
        f"\t\t\t\tIPHONEOS_DEPLOYMENT_TARGET = {DEPLOYMENT};\n"
        f"\t\t\t\tSDKROOT = iphoneos;\n"
        f"\t\t\t\tSWIFT_VERSION = {SWIFT_VERSION};\n"
    )

    tgt_settings = (
        f"\t\t\t\tCODE_SIGN_STYLE = Automatic;\n"
        f"\t\t\t\tCURRENT_PROJECT_VERSION = 1;\n"
        f"\t\t\t\tGENERATE_INFOPLIST_FILE = NO;\n"
        f"\t\t\t\tINFOPLIST_FILE = Sources/PorticoApp/Info.plist;\n"
        f"\t\t\t\tLD_RUNPATH_SEARCH_PATHS = (\n"
        f'\t\t\t\t\t"$(inherited)",\n'
        f'\t\t\t\t\t"@executable_path/Frameworks",\n'
        f"\t\t\t\t);\n"
        f"\t\t\t\tMARKETING_VERSION = 0.1.0;\n"
        f"\t\t\t\tOTHER_CFLAGS = (\n"
        f'\t\t\t\t\t"-DPR_ENABLE_ZLIB=1",\n'
        f"\t\t\t\t);\n"
        f"\t\t\t\tPRODUCT_BUNDLE_IDENTIFIER = {BUNDLE_ID};\n"
        f"\t\t\t\tPRODUCT_NAME = Portico;\n"
        f"\t\t\t\tSWIFT_EMIT_LOC_STRINGS = NO;\n"
        f"\t\t\t\tTARGETED_DEVICE_FAMILY = \"1,2\";\n"
        f"\t\t\t\tHEADER_SEARCH_PATHS = (\n"
        f'\t\t\t\t\t"$(SRCROOT)/Sources/PorticoRuntime/include",\n'
        f"\t\t\t\t);\n"
    )

    w("// !$*UTF8*$!")
    w("{")
    w("\tarchiveVersion = 1;")
    w("\tclasses = {\n\t};")
    w("\tobjectVersion = 56;")
    w("\tobjects = {")
    w("")
    w("/* Begin PBXBuildFile section */")
    lines.extend(B)
    w("/* End PBXBuildFile section */")
    w("")
    w("/* Begin PBXFileReference section */")
    lines.extend(F)
    w("/* End PBXFileReference section */")
    w("")
    w("/* Begin PBXFrameworksBuildPhase section */")
    w(f"\t\t{fw_phase} /* Frameworks */ = {{")
    w("\t\t\tisa = PBXFrameworksBuildPhase;")
    w("\t\t\tbuildActionMask = 2147483647;")
    w("\t\t\tfiles = (")
    lines.append(fw_list)
    w("\t\t\t);")
    w("\t\t\trunOnlyForDeploymentPostprocessing = 0;")
    w("\t\t};")
    w("/* End PBXFrameworksBuildPhase section */")
    w("")
    w("/* Begin PBXGroup section */")
    lines.extend(group_blocks)
    w("/* End PBXGroup section */")
    w("")
    w("/* Begin PBXNativeTarget section */")
    w(f"\t\t{target_id} /* Portico */ = {{")
    w("\t\t\tisa = PBXNativeTarget;")
    w(f"\t\t\tbuildConfigurationList = {cfg_list_tgt} /* Build configuration list for PBXNativeTarget \"Portico\" */;")
    w("\t\t\tbuildPhases = (")
    w(f"\t\t\t\t{sources_phase} /* Sources */,")
    w(f"\t\t\t\t{fw_phase} /* Frameworks */,")
    w("\t\t\t);")
    w("\t\t\tbuildRules = (")
    w("\t\t\t);")
    w("\t\t\tdependencies = (")
    w("\t\t\t);")
    w("\t\t\tname = Portico;")
    w("\t\t\tproductName = Portico;")
    w(f"\t\t\tproductReference = {app_product_id} /* Portico.app */;")
    w("\t\t\tproductType = \"com.apple.product-type.application\";")
    w("\t\t};")
    w("/* End PBXNativeTarget section */")
    w("")
    w("/* Begin PBXProject section */")
    w(f"\t\t{proj_id} /* Project object */ = {{")
    w("\t\t\tisa = PBXProject;")
    w("\t\t\tattributes = {")
    w("\t\t\t\tLastSwiftUpdateCheck = 1500;")
    w("\t\t\t\tLastUpgradeCheck = 1500;")
    w("\t\t\t};")
    w(f"\t\t\tbuildConfigurationList = {cfg_list_proj} /* Build configuration list for PBXProject \"Portico\" */;")
    w('\t\t\tcompatibilityVersion = "Xcode 14.0";')
    w('\t\t\tdevelopmentRegion = "pt-BR";')
    w("\t\t\thasScannedForEncodings = 0;")
    w("\t\t\tknownRegions = (\n\t\t\t\ten,\n\t\t\t\tBase,\n\t\t\t);")
    w(f"\t\t\tmainGroup = {root_group};")
    w(f"\t\t\tproductRefGroup = {products_group} /* Products */;")
    w('\t\t\tprojectDirPath = "";')
    w('\t\t\tprojectRoot = "";')
    w("\t\t\ttargets = (")
    w(f"\t\t\t\t{target_id} /* Portico */,")
    w("\t\t\t);")
    w("\t\t};")
    w("/* End PBXProject section */")
    w("")
    w("/* Begin PBXSourcesBuildPhase section */")
    w(f"\t\t{sources_phase} /* Sources */ = {{")
    w("\t\t\tisa = PBXSourcesBuildPhase;")
    w("\t\t\tbuildActionMask = 2147483647;")
    w("\t\t\tfiles = (")
    lines.append(src_list)
    w("\t\t\t);")
    w("\t\t\trunOnlyForDeploymentPostprocessing = 0;")
    w("\t\t};")
    w("/* End PBXSourcesBuildPhase section */")
    w("")
    w("/* Begin XCBuildConfiguration section */")
    w(f"\t\t{cfg_debug_proj} /* Debug */ = {{")
    w("\t\t\tisa = XCBuildConfiguration;")
    w("\t\t\tbuildSettings = {")
    lines.append(common_settings)
    w("\t\t\t\tONLY_ACTIVE_ARCH = YES;")
    w("\t\t\t\tSWIFT_ACTIVE_COMPILATION_CONDITIONS = DEBUG;")
    w("\t\t\t\tSWIFT_OPTIMIZATION_LEVEL = \"-Onone\";")
    w("\t\t\t\tGCC_OPTIMIZATION_LEVEL = 0;")
    w("\t\t\t\tGCC_PREPROCESSOR_DEFINITIONS = (\n\t\t\t\t\t\"DEBUG=1\",\n\t\t\t\t);")
    w("\t\t\t};")
    w("\t\t\tname = Debug;")
    w("\t\t};")
    w(f"\t\t{cfg_release_proj} /* Release */ = {{")
    w("\t\t\tisa = XCBuildConfiguration;")
    w("\t\t\tbuildSettings = {")
    lines.append(common_settings)
    w("\t\t\t\tSWIFT_OPTIMIZATION_LEVEL = \"-O\";")
    w("\t\t\t\tGCC_OPTIMIZATION_LEVEL = s;")
    w("\t\t\t};")
    w("\t\t\tname = Release;")
    w("\t\t};")
    w(f"\t\t{cfg_debug_tgt} /* Debug */ = {{")
    w("\t\t\tisa = XCBuildConfiguration;")
    w("\t\t\tbuildSettings = {")
    lines.append(tgt_settings)
    w("\t\t\t};")
    w("\t\t\tname = Debug;")
    w("\t\t};")
    w(f"\t\t{cfg_release_tgt} /* Release */ = {{")
    w("\t\t\tisa = XCBuildConfiguration;")
    w("\t\t\tbuildSettings = {")
    lines.append(tgt_settings)
    w("\t\t\t};")
    w("\t\t\tname = Release;")
    w("\t\t};")
    w("/* End XCBuildConfiguration section */")
    w("")
    w("/* Begin XCConfigurationList section */")
    w(f"\t\t{cfg_list_proj} /* Build configuration list for PBXProject \"Portico\" */ = {{")
    w("\t\t\tisa = XCConfigurationList;")
    w("\t\t\tbuildConfigurations = (")
    w(f"\t\t\t\t{cfg_debug_proj} /* Debug */,")
    w(f"\t\t\t\t{cfg_release_proj} /* Release */,")
    w("\t\t\t);")
    w("\t\t\tdefaultConfigurationIsVisible = 0;")
    w("\t\t\tdefaultConfigurationName = Release;")
    w("\t\t};")
    w(f"\t\t{cfg_list_tgt} /* Build configuration list for PBXNativeTarget \"Portico\" */ = {{")
    w("\t\t\tisa = XCConfigurationList;")
    w("\t\t\tbuildConfigurations = (")
    w(f"\t\t\t\t{cfg_debug_tgt} /* Debug */,")
    w(f"\t\t\t\t{cfg_release_tgt} /* Release */,")
    w("\t\t\t);")
    w("\t\t\tdefaultConfigurationIsVisible = 0;")
    w("\t\t\tdefaultConfigurationName = Release;")
    w("\t\t};")
    w("/* End XCConfigurationList section */")
    w("\t};")
    w(f"\trootObject = {proj_id} /* Project object */;")
    w("}")

    out_dir = ROOT / "Portico.xcodeproj"
    out_dir.mkdir(exist_ok=True)
    (out_dir / "project.pbxproj").write_text("\n".join(lines) + "\n")
    print(f"gerado: {out_dir / 'project.pbxproj'}")
    print(f"  swift: {len(swift_files)}  c: {len(csrc)}  headers: {len(cinc)}")


if __name__ == "__main__":
    main()
