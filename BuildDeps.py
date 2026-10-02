import os
import sys
import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Dict, Any, List, Set, Tuple

# --- 路径定义 ---
ROOT = Path(__file__).resolve().parent
PROJECT_DIR = ROOT
CONFIG_DIR = ROOT / "config" / "3rds"
DEPS_DIR = ROOT / "deps"
BUILD_DIR = DEPS_DIR / "build"
INSTALL_DIR_BASE = DEPS_DIR / "install"
CMAKE_OUTPUT = ROOT / "cmake"
CPU_COUNT = max(1, os.cpu_count() or 2)

def run(cmd: List[str], cwd: Path = None, env: Dict[str, str] = None) -> int:
    print(f"[run] {' '.join(cmd)}  (cwd={cwd})")
    p = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env, shell=(os.name == "nt"))
    return p.returncode

def load_json(path: Path) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)

def ensure_dirs():
    for d in [DEPS_DIR, BUILD_DIR, INSTALL_DIR_BASE, CMAKE_OUTPUT]:
        d.mkdir(parents=True, exist_ok=True)

def expand_placeholders(s: str, config: str = "Release") -> str:
    if not s: return ""
    # 替换内置变量
    result = s.replace("${INSTALL_DIR}", str(INSTALL_DIR_BASE).replace("\\", "/"))
    result = result.replace("${PROJECT_DIR}", str(PROJECT_DIR).replace("\\", "/"))
    result = result.replace("${CONFIG}", config)
    
    # 替换环境变量 ${ENV:VAR}
    result = re.sub(r"\$\{ENV:([^}]+)\}", lambda m: os.environ.get(m.group(1), "").replace("\\", "/"), result)
    return result

def get_layout_path(meta: Dict[str, Any], key: str, config: str = "Release") -> Path:
    """根据 install_layout 获取绝对路径"""
    layout = meta.get("install_layout", {})
    root_raw = layout.get("root", "")
    root_path = Path(expand_placeholders(root_raw, config))
    
    if key == "root":
        return root_path
    
    sub_path = layout.get(key, "")
    # 如果 sub_path 是绝对路径则直接使用，否则拼在 root 后面
    if os.path.isabs(expand_placeholders(sub_path, config)):
        return Path(expand_placeholders(sub_path, config))
    return root_path / expand_placeholders(sub_path, config)

def detect_platform_tag() -> str:
    if sys.platform.startswith("win"): return "windows"
    if sys.platform.startswith("linux"): return "linux"
    if sys.platform.startswith("darwin"): return "macos"
    return "unknown"

def compute_cmake_args(meta: Dict[str, Any]) -> Tuple[List[str], Dict[str, str]]:
    platform_tag = detect_platform_tag()
    comp_conf = meta.get("compiler", {}).get(platform_tag, {})
    
    args = []
    if comp_conf.get("cmake_generator"):
        args += ["-G", comp_conf["cmake_generator"]]
    if comp_conf.get("cmake_toolset"):
        args += ["-T", comp_conf["cmake_toolset"]]
    
    env = os.environ.copy()
    if comp_conf.get("preferred"):
        env["BUILD_PREFERRED_COMPILER"] = comp_conf["preferred"]
        
    return args, env

def has_existing_install_dir(install_prefix: Path) -> bool:
    return install_prefix.exists() and any(install_prefix.iterdir())

def clone_remote_source(meta: Dict[str, Any], src_dir: Path) -> bool:
    src_info = meta.get("source", {})
    if not src_info.get("git"):
        raise ValueError(f"Missing source.git for dependency: {meta['name']}")

    ref = src_info.get("tag") or src_info.get("branch", "main")

    if run([
        "git", "clone", "--depth=1", "--single-branch",
        "--branch", ref,
        src_info["git"], str(src_dir)
    ]) != 0:
        return False

    for extra in src_info.get("extra_repos", []):
        extra_path = src_dir / extra["path"]
        if run([
            "git", "clone", "--depth=1", "--single-branch",
            "--branch", extra["branch"],
            extra["git"], str(extra_path)
        ]) != 0:
            return False

    return True


def resolve_source_imported_dir(meta: Dict[str, Any]) -> Path:
    lib_type = meta.get("type", "")
    src_info = meta.get("source", {})

    if lib_type == "local_source_imported":
        location = src_info.get("location")
        if not location:
            raise ValueError(f"Missing source.location for local source imported dependency: {meta['name']}")
        src_dir = Path(expand_placeholders(location))
        if not src_dir.exists():
            raise FileNotFoundError(f"Local source directory not found for {meta['name']}: {src_dir}")
        return src_dir

    if lib_type == "remote_source_imported":
        src_dir = DEPS_DIR / meta["name"]
        if not src_dir.exists():
            if not clone_remote_source(meta, src_dir):
                raise RuntimeError(f"Failed to clone remote source for dependency: {meta['name']}")
        return src_dir

    raise ValueError(f"Unsupported imported source type: {lib_type}")


def handle_source_imported(meta: Dict[str, Any]) -> bool:
    src_dir = resolve_source_imported_dir(meta)
    install_source_imported_files(meta, src_dir)
    return True


def cmake_build_and_install(meta: Dict[str, Any]) -> bool:
    name = meta["name"]
    src_dir = DEPS_DIR / name
    build_root = BUILD_DIR / name
    
    build_conf = meta.get("build_config", {})
    # 默认编译 Debug 和 Release
    configs = ["Debug", "Release"]
    
    success = True
    for cfg in configs:
        cfg_build_dir = build_root / cfg
        # 从 layout 获取安装前缀
        install_prefix = get_layout_path(meta, "root", cfg)

        if has_existing_install_dir(install_prefix):
            print(f"[skip] {name} ({cfg}) already installed at {install_prefix}")
            continue

        cfg_build_dir.mkdir(parents=True, exist_ok=True)
        
        gen_args, env = compute_cmake_args(meta)
        
        # Configure
        install_prefix_str = str(install_prefix).replace('\\', '/')
        configure_cmd = [
            "cmake", "-S", str(src_dir), "-B", str(cfg_build_dir),
            f"-DCMAKE_INSTALL_PREFIX={install_prefix_str}",
            f"-DCMAKE_BUILD_TYPE={cfg}"
        ]
        configure_cmd += gen_args
        
        # 加入自定义参数
        user_args = build_conf.get("configure_args", [])
        for arg in user_args:
            configure_cmd.append(expand_placeholders(arg, cfg))
            
        if run(configure_cmd, cwd=cfg_build_dir, env=env) != 0:
            success = False; break
            
        # Build & Install
        build_cmd = ["cmake", "--build", str(cfg_build_dir), "--config", cfg, "--target", "install", "-j", str(CPU_COUNT)]
        if run(build_cmd, cwd=cfg_build_dir, env=env) != 0:
            success = False; break
            
    return success

def handle_remote_source_prebuild(meta: Dict[str, Any]) -> bool:
    src_dir = DEPS_DIR / meta["name"]

    if not src_dir.exists():
        if not clone_remote_source(meta, src_dir):
            return False

    return cmake_build_and_install(meta)

# ---- 拓扑排序 (保持不变) ----
def topological_sort(all_meta: List[Dict[str, Any]]) -> List[str]:
    graph = {m["name"]: set(m.get("dependencies", [])) for m in all_meta}
    inv_graph = {n: set() for n in graph}
    for n, deps in graph.items():
        for d in deps:
            if d in inv_graph: inv_graph[d].add(n)
            
    order = []
    queue = [n for n, d in graph.items() if not d]
    while queue:
        n = queue.pop(0)
        order.append(n)
        for m in list(inv_graph[n]):
            graph[m].remove(n)
            if not graph[m]: queue.append(m)
    if len(order) != len(graph):
        raise RuntimeError("Cycle or missing dependency detected!")
    return order

def append_remote_source_prebuild_find_entries(lines: List[str], meta: Dict[str, Any]):
    name = meta["name"]
    lines.append(f"# [{name}]")

    for cfg in ["Debug", "Release"]:
        root_path = str(get_layout_path(meta, "root", cfg)).replace("\\", "/")
        lines.append(f"if(CMAKE_BUILD_TYPE STREQUAL \"{cfg}\")")
        lines.append(f"  list(APPEND CMAKE_PREFIX_PATH \"{root_path}\")")
        lines.append("endif()")

    lines.append("")

def _normalize_define_list(defines: List[str]) -> List[str]:
    return [define for define in defines if define]

def _normalize_path_list(root: Path, entries: List[str]) -> List[str]:
    return [str(root / Path(entry)).replace("\\", "/") for entry in entries if entry]

def _normalize_path_mode(info: Dict[str, Any]) -> str:
    return str(info.get("PathMode", "relative")).strip().lower()

def _ensure_suffix(path_text: str, suffix: str) -> str:
    if not suffix:
        return path_text
    if Path(path_text).suffix:
        return path_text
    return path_text + suffix

def _resolve_system_path_entry(entry: str, suffix: str) -> Path:
    raw_entry = expand_placeholders(entry).replace("${SYSTEM_PATH}", "")
    raw_entry = raw_entry.replace("\\", "/").lstrip("/\\")
    if not raw_entry:
        raise ValueError(f"Invalid SYSTEM_PATH entry: {entry}")

    candidate_text = _ensure_suffix(raw_entry, suffix)
    candidate = Path(candidate_text)

    system_paths = [Path(path_text.strip('"')) for path_text in os.environ.get("PATH", "").split(os.pathsep) if path_text.strip()]
    for system_dir in system_paths:
        direct_candidate = system_dir / candidate
        if direct_candidate.exists():
            return direct_candidate

        name_candidate = system_dir / candidate.name
        if name_candidate.exists():
            return name_candidate

    if candidate.is_absolute():
        return candidate

    return candidate

def _resolve_manual_path_entry(entry: str, root: Path, suffix: str, path_mode: str) -> Path:
    normalized_mode = path_mode.strip().lower()
    if normalized_mode in {"absolute", "absolite", "system_path"}:
        if "${SYSTEM_PATH}" in entry or normalized_mode == "system_path":
            return _resolve_system_path_entry(entry, suffix)

        expanded = expand_placeholders(entry).replace("\\", "/")
        expanded = _ensure_suffix(expanded, suffix)
        return Path(expanded)

    expanded = expand_placeholders(entry).replace("\\", "/")
    expanded = _ensure_suffix(expanded, suffix)
    return root / Path(expanded)

def _append_imported_location_properties(
    lines: List[str],
    info: Dict[str, Any],
    property_name: str,
    root_release: Path,
    root_debug: Path,
    default_suffix: str,
    path_mode: str = "relative",
):
    release_paths = []
    debug_paths = []

    for entry in info.get("release", []):
        if entry:
            resolved = _resolve_manual_path_entry(entry, root_release, default_suffix, path_mode)
            release_paths.append(str(resolved).replace("\\", "/"))

    for entry in info.get("debug", []):
        if entry:
            resolved = _resolve_manual_path_entry(entry, root_debug, default_suffix, path_mode)
            debug_paths.append(str(resolved).replace("\\", "/"))

    if release_paths:
        lines.append(f"    set_target_properties({property_name[0]} PROPERTIES {property_name[1]} \"{release_paths[0]}\")")
    if debug_paths:
        lines.append(f"    set_target_properties({property_name[0]} PROPERTIES {property_name[2]} \"{debug_paths[0]}\")")

def _normalize_imported_path_list(root: Path, entries: List[str]) -> List[str]:
    normalized = []
    for entry in entries or []:
        if not entry:
            continue
        expanded = expand_placeholders(entry).replace("\\", "/")
        if os.path.isabs(expanded):
            normalized.append(Path(expanded).as_posix())
        else:
            normalized.append(str((root / Path(expanded))).replace("\\", "/"))
    return normalized


def _copy_imported_entries(src_root: Path, target_root: Path, entries: List[str]) -> None:
    for entry in entries or []:
        if not entry:
            continue

        src_path = src_root / Path(entry)
        if not src_path.exists():
            raise FileNotFoundError(f"Imported file not found: {src_path}")

        target_path = target_root / Path(entry)
        target_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_path, target_path)


def install_source_imported_files(meta: Dict[str, Any], src_dir: Path) -> None:
    import_info = meta.get("import_info", {})

    for cfg in ["Debug", "Release"]:
        include_root = get_layout_path(meta, "include", cfg)
        source_root = get_layout_path(meta, "source", cfg)
        include_root.mkdir(parents=True, exist_ok=True)
        source_root.mkdir(parents=True, exist_ok=True)

        _copy_imported_entries(src_dir, include_root, import_info.get("headers", []))
        _copy_imported_entries(src_dir, source_root, import_info.get("sources", []))


def append_source_imported_find_entries(lines: List[str], meta: Dict[str, Any]):
    name = meta["name"]
    safe_name = name.replace("-", "_").replace(".", "_").upper()
    import_info = meta.get("import_info", {})

    release_root = get_layout_path(meta, "root", "Release")
    debug_root = get_layout_path(meta, "root", "Debug")
    release_include_root = get_layout_path(meta, "include", "Release")
    debug_include_root = get_layout_path(meta, "include", "Debug")
    release_source_root = get_layout_path(meta, "source", "Release")
    debug_source_root = get_layout_path(meta, "source", "Debug")

    header_entries = _normalize_imported_path_list(release_include_root, import_info.get("headers", []))
    source_entries = _normalize_imported_path_list(release_source_root, import_info.get("sources", []))

    release_root_str = str(release_root).replace("\\", "/")
    debug_root_str = str(debug_root).replace("\\", "/")
    include_dir_release_str = str(release_include_root).replace("\\", "/")
    include_dir_debug_str = str(debug_include_root).replace("\\", "/")

    lines.append(f"# [{name}]")
    lines.append(f"set({safe_name}_SOURCE_ROOT \"{release_root_str}\")")
    lines.append(f"set({safe_name}_INCLUDE_DIR \"{include_dir_release_str}\")")
    lines.append("if(CMAKE_BUILD_TYPE STREQUAL \"Debug\")")
    lines.append(f"  set({safe_name}_SOURCE_ROOT \"{debug_root_str}\")")
    lines.append(f"  set({safe_name}_INCLUDE_DIR \"{include_dir_debug_str}\")")
    lines.append("endif()")

    if header_entries:
        lines.append(f"set({safe_name}_HEADER_FILES")
        for header in header_entries:
            lines.append(f'  "{header}"')
        lines.append(")")
    else:
        lines.append(f"set({safe_name}_HEADER_FILES)")

    if source_entries:
        lines.append(f"set({safe_name}_SOURCE_FILES")
        for source in source_entries:
            lines.append(f'  "{source}"')
        lines.append(")")
    else:
        lines.append(f"set({safe_name}_SOURCE_FILES)")

    lines.append("")


def append_manual_find_entries(lines: List[str], meta: Dict[str, Any]):
    name = meta["name"]
    safe_name = name.replace("-", "_").replace(".", "_").upper()
    manual = meta.get("manual_info", {})
    common_defines = _normalize_define_list(manual.get("common_defines", []))

    lines.append(f"# [{name}]")

    root_release = get_layout_path(meta, "root", "Release")
    root_debug = get_layout_path(meta, "root", "Debug")
    include_dirs = _normalize_path_list(root_release, manual.get("include_dirs", []))
    all_runtime_release: List[str] = []
    all_runtime_debug: List[str] = []

    for component_name, info in manual.get("components", {}).items():
        target_name = f"Deps::{name}::{component_name}"
        lib_info = info.get("lib", {})
        runtime_info = info.get("runtime", {})
        lib_path_mode = _normalize_path_mode(lib_info)
        runtime_path_mode = _normalize_path_mode(runtime_info)
        has_linkable_lib = any(entry for entry in lib_info.get("debug", []) + lib_info.get("release", []))
        has_runtime_lib = any(entry for entry in runtime_info.get("debug", []) + runtime_info.get("release", []))
        if has_linkable_lib:
            lines.append(f"if(NOT TARGET {target_name})")
            importedType = "STATIC"
            if has_runtime_lib:
                importedType = "SHARED"
            lines.append(f"  add_library({target_name} {importedType} IMPORTED)")

            if include_dirs:
                joined = ";".join(include_dirs)
                lines.append(f"  set_target_properties({target_name} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"{joined}\")")

            if common_defines:
                joined_defines = ";".join(common_defines)
                lines.append(f"  set_target_properties({target_name} PROPERTIES INTERFACE_COMPILE_DEFINITIONS \"{joined_defines}\")")

            lib_property = (
                target_name,
                "IMPORTED_IMPLIB_RELEASE" if sys.platform.startswith("win") else "IMPORTED_LOCATION_RELEASE",
                "IMPORTED_IMPLIB_DEBUG" if sys.platform.startswith("win") else "IMPORTED_LOCATION_DEBUG",
            )
            _append_imported_location_properties(lines, lib_info, lib_property, root_release, root_debug, ".lib" if sys.platform.startswith("win") else ".a", lib_path_mode)

            if has_runtime_lib:
                runtime_property = (
                    target_name,
                    "IMPORTED_LOCATION_RELEASE",
                    "IMPORTED_LOCATION_DEBUG",
                )
                _append_imported_location_properties(lines, runtime_info, runtime_property, root_release, root_debug, ".dll" if sys.platform.startswith("win") else ".so", runtime_path_mode)

            dependencies = [dep for dep in info.get("dependencies", []) if dep]
            if dependencies:
                joined_dependencies = ";".join(dependencies)
                lines.append(f"  set_target_properties({target_name} PROPERTIES INTERFACE_LINK_LIBRARIES \"{joined_dependencies}\")")

            lines.append("endif()")

        for entry in runtime_info.get("release", []):
            if entry:
                suffix = ".dll" if sys.platform.startswith("win") else ".so"
                resolved = _resolve_manual_path_entry(entry, root_release, suffix, runtime_path_mode)
                all_runtime_release.append(str(resolved).replace("\\", "/"))
        for entry in runtime_info.get("debug", []):
            if entry:
                suffix = ".dll" if sys.platform.startswith("win") else ".so"
                resolved = _resolve_manual_path_entry(entry, root_debug, suffix, runtime_path_mode)
                all_runtime_debug.append(str(resolved).replace("\\", "/"))

    if all_runtime_release or all_runtime_debug:
        joined_release = ";".join(all_runtime_release)
        joined_debug = ";".join(all_runtime_debug)
        joined = ";".join(include_dirs)
        lines.append(f"set({safe_name}_INCLUDE_DIRS \"{joined}\")")
        lines.append(f"set({safe_name}_DLLS_RELEASE \"{joined_release}\")")
        lines.append(f"set({safe_name}_DLLS_DEBUG \"{joined_debug}\")")
        lines.append(f"if(CMAKE_BUILD_TYPE STREQUAL \"Debug\")")
        lines.append(f"  set({safe_name}_DLLS \"{joined_debug}\")")
        lines.append(f"else()")
        lines.append(f"  set({safe_name}_DLLS \"{joined_release}\")")
        lines.append(f"endif()")

    lines.append("")

# ---- 生成 Find3rdsGenerated.cmake ----
def generate_cmake_find_file(processed_meta: List[Dict[str, Any]]):
    lines = [
        "# Auto-generated by BuildDeps.py",
        "cmake_minimum_required(VERSION 3.16)\n"
    ]
    
    for meta in processed_meta:
        lib_type = meta.get("type", "")

        if lib_type == "remote_source_prebuild":
            append_remote_source_prebuild_find_entries(lines, meta)
        elif lib_type in {"remote_source_imported", "local_source_imported"}:
            append_source_imported_find_entries(lines, meta)
        elif lib_type == "manual":
            append_manual_find_entries(lines, meta)

    with open(CMAKE_OUTPUT / "Find3rdsGenerated.cmake", "w") as f:
        f.write("\n".join(lines))

def main():
    ensure_dirs()
    config_files = list(CONFIG_DIR.glob("*.json"))
    all_meta = [load_json(f) for f in config_files]
    order = topological_sort(all_meta)
    name_to_meta = {m["name"]: m for m in all_meta}
    
    processed_meta = []
    
    for name in order:
        meta = name_to_meta[name]
        ignore = meta.get("ignore", False)
        if ignore: 
            continue
        lib_type = meta.get("type", "remote_source_prebuild")
        print(f"\n>>> Processing: {name} ({lib_type})")
        
        if lib_type == "remote_source_prebuild":
            if not handle_remote_source_prebuild(meta):
                raise RuntimeError(f"Failed to process dependency: {name}")
        elif lib_type in {"remote_source_imported", "local_source_imported"}:
            if not handle_source_imported(meta):
                raise RuntimeError(f"Failed to process dependency: {name}")
                
        processed_meta.append(meta)
    
    generate_cmake_find_file(processed_meta)
    print("\n[Done] All dependencies processed.")

if __name__ == "__main__":
    main()