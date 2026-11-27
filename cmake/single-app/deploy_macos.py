"""Deploy and sign one application in place, including plain helper executables."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import signal
import shutil
import subprocess
import tempfile


SYSTEM_PREFIXES = ("/System/", "/usr/lib/")


def run(*args):
    try:
        return subprocess.check_output([str(arg) for arg in args], text=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f"{args[0]} failed:\n{error.output}") from error


def cached_macho(function):
    cache = {}
    def read(binary):
        binary = Path(binary).resolve()
        metadata = binary.stat()
        key = (str(binary), metadata.st_mtime_ns, metadata.st_size)
        if key not in cache:
            cache[key] = function(binary)
        return cache[key]
    return read


@cached_macho
def identities(binary):
    return [line.strip() for line in run("otool", "-D", binary).splitlines()
            if line.strip() and not line.rstrip().endswith(":")]


@cached_macho
def dependencies(binary):
    names = identities(binary)
    return list(dict.fromkeys(line.strip().split(" (compatibility", 1)[0]
            for line in run("otool", "-L", binary).splitlines()[1:]
            if line[:1].isspace() and line.strip().split(" (compatibility", 1)[0] not in names))


@cached_macho
def rpaths(binary):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset", run("otool", "-l", binary))


def expand(path, loader, executable):
    return Path(path.replace("@loader_path", str(loader.parent))
                .replace("@executable_path", str(executable.parent)))


def reject_retired_runtime(path):
    name = Path(path).name.lower()
    if "onnx" in name or "opencv_dnn" in name or name.startswith("libpiper."):
        raise RuntimeError(f"Retired model runtime is forbidden in product deployment: {path}")


def purge_retired_runtimes(app):
    removed = []
    for path in sorted(app.rglob("*"), key=lambda item: len(item.parts), reverse=True):
        try:
            reject_retired_runtime(path)
        except RuntimeError:
            removed.append(str(path.relative_to(app)))
            if path.is_symlink() or path.is_file():
                path.unlink()
            elif path.is_dir():
                shutil.rmtree(path)
    return removed


def native_sources(executable, libraries, search_paths, runtime_plugins=()):
    """Resolve before rewriting; explicitly selected SDK versions take precedence."""
    preferred = {}
    for library in libraries:
        if library.suffix != ".dylib":
            continue
        preferred[library.name] = library
        for name in identities(library):
            preferred[Path(name).name] = library
    result = {}
    for plugin in runtime_plugins:
        reject_retired_runtime(plugin)
        if not plugin.is_file():
            raise RuntimeError(f"Missing native runtime plugin: {plugin}")
        result[plugin.name] = plugin.resolve()
    inherited = rpaths(executable)
    queue = [executable] + list(result.values())
    for binary in queue:
        for link in dependencies(binary):
            if link.startswith(SYSTEM_PREFIXES) or not link.endswith(".dylib"):
                continue
            name = Path(link).name
            reject_retired_runtime(name)
            if name in result or binary != executable and name == binary.name:
                continue
            candidates = [preferred[name]] if name in preferred else []
            if link.startswith("@rpath/"):
                candidates += [expand(path, binary, executable) / link[len("@rpath/"):]
                               for path in rpaths(binary) + inherited]
            else:
                candidates.append(expand(link, binary, executable))
            candidates += [directory / name for directory in search_paths]
            source = next((path.resolve() for path in candidates if path.is_file()), None)
            if source is None:
                raise RuntimeError(f"Missing runtime dependency: {binary} -> {link}")
            result[name] = source
            queue.append(source)
    return result


def prepare_native(app, libraries, search_paths, executable=None, runtime_plugins=()):
    executable = executable or app / "Contents/MacOS" / app.stem
    frameworks = app / "Contents/Frameworks"
    sources = native_sources(executable, libraries, search_paths, runtime_plugins)
    frameworks.mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        target = frameworks / name
        if source != target.resolve():
            if target.is_symlink():
                target.unlink()
            elif target.exists():
                target.chmod(target.stat().st_mode | 0o200)
            shutil.copy2(source, target)
        target.chmod(target.stat().st_mode | 0o200)
        if name == "libmlx.dylib" and (source.parent / "mlx.metallib").is_file():
            # Keep GPU data in Resources; Frameworks entries must be signed code.
            resource = app / "Contents/Resources/mlx.metallib"
            resource.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source.parent / "mlx.metallib", resource)
            colocated = frameworks / "mlx.metallib"
            if colocated.exists() or colocated.is_symlink():
                colocated.unlink()
            colocated.symlink_to("../Resources/mlx.metallib")
    for binary in [executable] + [frameworks / name for name in sources]:
        changes = []
        for link in dependencies(binary):
            if link.startswith(SYSTEM_PREFIXES) or not link.endswith(".dylib"):
                continue
            target = frameworks / Path(link).name
            if not target.is_file() or target == binary:
                continue
            relocated = "@loader_path/" + os.path.relpath(target, binary.parent)
            if link != relocated:
                changes += ["-change", link, relocated]
        if changes:
            run("install_name_tool", *changes, binary)
    return sources


def is_native_tool_temporary(path):
    """Recognize interrupted native-tool output beside its real executable."""
    name = path.name
    return (name.endswith(".cstemp") or
            (name.endswith(".XXXXXX") and path.with_name(name[:-7]).is_file()))


def native_helpers(app):
    return [path for path in (app / "Contents/MacOS").iterdir()
            if path.is_file() and path.name != app.stem and not is_native_tool_temporary(path)]


def binaries(app):
    result = {path.resolve() for path in app.rglob("*.dylib") if path.is_file()
              and not any(parent.name.endswith(".dSYM") for parent in path.parents)}
    for framework in app.rglob("*.framework"):
        binary = framework / framework.stem
        if binary.is_file():
            result.add(binary.resolve())
    for bundle in [app] + list(app.rglob("*.app")):
        result.add(bundle / "Contents/MacOS" / bundle.stem)
    result.update(native_helpers(app))
    return sorted(result)


def deploy_qml_modules(destination, modules, compact=False, quick_style=None, embedded_resources_modules=()):
    """Copy imported modules without recursively deploying unrelated child modules."""
    modules = [module for module in modules if module.get("type") == "module"
               and module.get("path") and module.get("relativePath")]
    styles = set()
    for module in modules:
        if module.get("name") == "QtQuick.Controls":
            manifest = Path(module["path"]) / "qmldir"
            if manifest.is_file():
                styles.update(re.findall(r"^(?:optional|default) import (QtQuick\.Controls\.\w+)\b",
                                         manifest.read_text(), re.MULTILINE))
    if quick_style:
        selected_style = "QtQuick.Controls." + quick_style
        if not any(module.get("name") == selected_style for module in modules):
            raise RuntimeError(f"The requested Qt Quick Controls style is unavailable: {quick_style}")
        modules = [module for module in modules if not any(
            (module.get("name") == style or module.get("name", "").startswith(style + "."))
            and style != selected_style for style in styles)]
    if destination.exists():
        shutil.rmtree(destination)
    copied = []
    for module in modules:
        source, relative = Path(module["path"]), Path(module["relativePath"])
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError(f"Invalid QML module destination: {relative}")

        def ignore(directory, names):
            if not compact:
                return [name for name in names if name.endswith(".dSYM")]
            directory = Path(directory)
            skipped = []
            for name in names:
                path = directory / name
                if name == "designer" or name.endswith((".dSYM", ".qmltypes")):
                    skipped.append(name)
                elif (path / "qmldir").is_file():
                    declaration = re.search(r"^module\s+(\S+)",
                                            (path / "qmldir").read_text(), re.MULTILINE)
                    if declaration and declaration.group(1) != module.get("name"):
                        skipped.append(name)
                elif (module.get("name") in embedded_resources_modules
                      and path.is_relative_to(source / "resources")
                      and path.suffix.lower() in (".svg", ".ttf", ".otf", ".png", ".jpg", ".webp", ".qsb")):
                    skipped.append(name)
            return skipped

        shutil.copytree(source, destination / relative, symlinks=False,
                        dirs_exist_ok=True, ignore=ignore)
        copied.append(module.get("name"))
    return copied


def prune_native(app, runtime_plugins=()):
    """Remove stale Frameworks entries outside the executable and plugin dependency closure."""
    frameworks = app / "Contents/Frameworks"
    native = set(binaries(app))
    roots = {binary for binary in native if not binary.is_relative_to(frameworks)}
    roots.update((frameworks / plugin.name).resolve() for plugin in runtime_plugins)
    reachable = set()
    queue = list(roots)
    for binary in queue:
        if binary in reachable:
            continue
        reachable.add(binary)
        for link in dependencies(binary):
            if link.startswith(SYSTEM_PREFIXES):
                continue
            match = re.search(r"([^/]+\.framework/.+)$", link)
            target = frameworks / (match.group(1) if match else Path(link).name)
            if not target.is_file():
                raise RuntimeError(f"Bundle is missing a dependency: {binary} -> {link}")
            queue.append(target.resolve())
    removed = []
    for path in sorted(frameworks.iterdir()):
        if path.suffix == ".framework":
            keep = (path / path.stem).is_file() and (path / path.stem).resolve() in reachable
        elif path.suffix == ".dylib":
            keep = path.resolve() in reachable
        else:
            continue
        if not keep:
            removed.append(str(path.relative_to(app)))
            if path.is_symlink() or path.is_file():
                path.unlink()
            else:
                shutil.rmtree(path)
    return removed


def prune_framework_development_files(app):
    """Keep Qt framework runtime resources without SDK headers or module maps."""
    removed = []
    for framework in sorted((app / "Contents/Frameworks").glob("Qt*.framework")):
        if framework.is_symlink():
            continue
        roots = [framework]
        versions = framework / "Versions"
        if versions.is_dir() and not versions.is_symlink():
            roots.extend(path for path in versions.iterdir() if path.is_dir() and not path.is_symlink())
        for root in roots:
            for name in ("Headers", "PrivateHeaders", "Modules"):
                path = root / name
                if path.is_symlink():
                    path.unlink()
                elif path.is_dir():
                    shutil.rmtree(path)
                else:
                    continue
                removed.append(str(path.relative_to(app)))
    return removed


def macho_architectures(binary, app):
    """Retry a killed lipo probe once after restaging that bundle file."""
    try:
        return set(run("lipo", "-archs", binary).split())
    except RuntimeError as error:
        cause = error.__cause__
        path = Path(binary)
        root = Path(app).resolve()
        if (not isinstance(cause, subprocess.CalledProcessError)
                or cause.returncode != -signal.SIGKILL
                or path.is_symlink()
                or not path.is_file()
                or not path.resolve().is_relative_to(root)):
            raise
        descriptor, name = tempfile.mkstemp(prefix="." + path.name + ".", suffix=".lipo",
                                             dir=path.parent)
        os.close(descriptor)
        temporary = Path(name)
        try:
            shutil.copy2(path, temporary)
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)
        return set(run("lipo", "-archs", path).split())


def thin_native(app):
    """Keep only architectures supported by the enclosing product executable."""
    executable = app / "Contents/MacOS" / app.stem
    required = macho_architectures(executable, app)
    saved = 0
    for binary in binaries(app):
        available = macho_architectures(binary, app)
        if not required.issubset(available):
            raise RuntimeError(f"Runtime architectures do not match {sorted(required)}: {binary}")
        extra = available - required
        if not extra:
            continue
        before = binary.stat().st_size
        temporary = binary.with_name("." + binary.name + ".thin")
        # Removing slices can leave the original universal-file padding intact.
        # A one-architecture product needs a genuinely thin Mach-O file.
        options = (["-thin", next(iter(required))] if len(required) == 1 else
                   [arg for architecture in sorted(extra) for arg in ("-remove", architecture)])
        try:
            run("lipo", binary, *options, "-output", temporary)
            shutil.copymode(binary, temporary)
            os.replace(temporary, binary)
        finally:
            temporary.unlink(missing_ok=True)
        saved += before - binary.stat().st_size
    return saved


def strip_native(app):
    """Strip local/debug symbols only; retain exported symbols used by plugins and SDK APIs."""
    saved = 0
    for binary in binaries(app):
        before = binary.stat().st_size
        binary.chmod(binary.stat().st_mode | 0o200)
        run("strip", "-S", "-x", binary)
        saved += before - binary.stat().st_size
    return saved


def containing_app(binary):
    return next(parent for parent in binary.parents if (parent / "Contents/Info.plist").is_file())


def framework_runtime_ignore(directory, names):
    """Omit SDK-only framework directories while retaining runtime resources."""
    path = Path(directory)
    if path.name.endswith(".framework") or path.parent.name == "Versions":
        return set(names) & {"Headers", "PrivateHeaders", "Modules"}
    return set()


def complete_frameworks(app, qt):
    """Complete plugin-only Qt dependencies without redeploying the app per plugin."""
    queue = list(binaries(app))
    seen = set()
    for binary in queue:
        if binary in seen:
            continue
        seen.add(binary)
        for link in dependencies(binary):
            match = re.search(r"(Qt[^/]+\.framework)/(.+)$", link)
            if not match:
                continue
            framework, relative = match.groups()
            target = app / "Contents/Frameworks" / framework
            if not (target / relative).is_file():
                source = qt / "lib" / framework
                if not (source / relative).is_file():
                    raise RuntimeError(f"Missing Qt framework: {link}")
                shutil.copytree(source, target, symlinks=True, dirs_exist_ok=True,
                                ignore=framework_runtime_ignore)
            queue.append((target / relative).resolve())


def qt_plugins(qt, libraries):
    """Include the platform media backend when this app links Qt Multimedia."""
    plugins = [qt / "plugins/sqldrivers/libqsqlite.dylib",
               qt / "plugins/tls/libqsecuretransportbackend.dylib",
               qt / "plugins/platforms/libqcocoa.dylib", qt / "plugins/platforms/libqoffscreen.dylib"]
    categories = ["imageformats", "iconengines", "networkinformation"]
    if any(library.name == "QtMultimedia" for library in libraries):
        categories.append("multimedia")
    for category in categories:
        plugins += sorted((qt / "plugins" / category).glob("*.dylib"))
    return plugins


def localize(app):
    """Remove development paths, including those retained in transitive libraries."""
    for binary in binaries(app):
        owner = containing_app(binary)
        frameworks = owner / "Contents/Frameworks"
        changes = []
        for link in dependencies(binary):
            if link.startswith(SYSTEM_PREFIXES):
                continue
            framework = re.search(r"([^/]+\.framework/.+)$", link)
            target = frameworks / (framework.group(1) if framework else Path(link).name)
            if not target.is_file():
                raise RuntimeError(f"Bundle is missing a dependency: {binary} -> {link}")
            relocated = "@loader_path/" + os.path.relpath(target, binary.parent)
            if target.resolve() != binary.resolve() and link != relocated:
                changes += ["-change", link, relocated]
        for path in dict.fromkeys(rpaths(binary)):
            if path.startswith("/") and not path.startswith(SYSTEM_PREFIXES):
                changes += ["-delete_rpath", path]
        if changes:
            binary.chmod(binary.stat().st_mode | 0o200)
            run("install_name_tool", *changes, binary)


def macho_sections(binary, architecture):
    """Read one architecture's imports, exports and linked dylib load commands."""
    output = run("xcrun", "dyld_info", "-arch", architecture, "-imports", "-exports",
                 "-linked_dylibs", binary)
    sections = {name: [] for name in ("imports", "exports", "linked_dylibs")}
    current = None
    seen = set()
    for line in output.splitlines():
        heading = re.match(r"\s+-([a-z_]+):\s*$", line)
        if heading:
            current = heading.group(1) if heading.group(1) in sections else None
            if current:
                seen.add(current)
        elif current:
            sections[current].append(line)
    if seen != sections.keys():
        raise RuntimeError(f"Incomplete Mach-O inspection for {binary} ({architecture})")
    return sections


def macho_library_label(link):
    """Match the abbreviated install-name labels emitted by dyld_info."""
    name = Path(link).name.removesuffix(".dylib")
    base, dot, suffix = name.rpartition(".")
    return base if dot and len(suffix) == 1 else name


def verify_symbol_bindings(app):
    """Reject required imports missing from their selected bundled provider.

    This is a static, per-architecture check. System libraries, weak imports,
    and dynamic or weak-coalesced lookup are left to the macOS loader; it does
    not start the application or claim to validate runtime behavior.
    """
    executable = app / "Contents/MacOS" / app.stem
    architectures = run("lipo", "-archs", executable).split()
    images = binaries(app)
    inspected = 0
    for architecture in architectures:
        tables = {binary.resolve(): macho_sections(binary, architecture) for binary in images}
        exports = {}
        links = {}
        for binary, sections in tables.items():
            exports[binary] = set()
            for line in sections["exports"]:
                match = re.match(r"\s*0x[0-9A-Fa-f]+\s+(\S+)", line)
                if match:
                    exports[binary].add(match.group(1))
            linked = []
            for line in sections["linked_dylibs"]:
                match = re.match(r"\s*(?:(re-export|weak-link|upward)\s+)?((?:@|/)[^\s]+)\s*$", line)
                if not match:
                    continue
                attribute, path = match.groups()
                if path.startswith(SYSTEM_PREFIXES):
                    target = None
                elif path.startswith("@loader_path/"):
                    target = (binary.parent / path[len("@loader_path/"):]).resolve()
                elif path.startswith("@executable_path/"):
                    target = (executable.parent / path[len("@executable_path/"):]).resolve()
                else:
                    raise RuntimeError(f"Cannot statically resolve bundled dependency: {binary} -> {path}")
                if target is not None and target not in tables:
                    raise RuntimeError(f"Bundled dependency is not an inspected Mach-O: {binary} -> {path}")
                linked.append((macho_library_label(path), target, attribute, path))
            links[binary] = linked

        def available_symbols(binary, seen):
            """Include symbols exposed through explicitly re-exported dylibs."""
            if binary in seen:
                return set(), False
            seen = seen | {binary}
            result = set(exports[binary])
            unknown_system_export = False
            for _, target, attribute, _ in links[binary]:
                if attribute != "re-export":
                    continue
                if target is None:
                    unknown_system_export = True
                else:
                    inherited, unknown = available_symbols(target, seen)
                    result.update(inherited)
                    unknown_system_export |= unknown
            return result, unknown_system_export

        provider_symbols = {binary: available_symbols(binary, set()) for binary in tables}
        failures = []
        for binary, sections in tables.items():
            by_label = {}
            for label, target, _, path in links[binary]:
                by_label.setdefault(label, []).append((target, path))
            for line in sections["imports"]:
                match = re.match(r"\s*0x[0-9A-Fa-f]+\s+(\S+)((?:\s+\[[^]]+\])*)\s+\(from ([^)]+)\)", line)
                if not match:
                    if re.match(r"\s*0x[0-9A-Fa-f]+", line):
                        raise RuntimeError(f"Cannot inspect Mach-O import in {binary}: {line.strip()}")
                    continue
                symbol, attributes, label = match.groups()
                if "weak-import" in attributes or label in ("<weak-def-coalesce>", "<dynamic-lookup>"):
                    continue
                if label == "<this-image>":
                    target, path = binary, label
                elif label == "<main-executable>":
                    target, path = executable.resolve(), label
                else:
                    candidates = by_label.get(label, [])
                    if len({target for target, _ in candidates}) != 1:
                        raise RuntimeError(f"Ambiguous or unknown Mach-O provider {label} in {binary}")
                    target, path = candidates[0]
                if target is None:
                    continue
                available, unknown_system_export = provider_symbols[target]
                if symbol not in available:
                    reason = "unverified system re-export" if unknown_system_export else "missing symbol"
                    failures.append(f"{binary} -> {path}: {symbol} ({reason})")
                inspected += 1
        if failures:
            details = "\n".join(failures[:25])
            raise RuntimeError(f"Bundled Mach-O bindings failed for {architecture}:\n{details}")
    return {"architectures": architectures, "images": len(images), "checkedImports": inspected}


def remove_signing_temporaries(app):
    """Discard incomplete native-tool output inside the generated bundle only."""
    removed = []
    candidates = list(app.rglob("*.cstemp"))
    macos = app / "Contents/MacOS"
    if macos.is_dir():
        candidates += [path for path in macos.glob("*.XXXXXX") if is_native_tool_temporary(path)]
    for path in candidates:
        if path.is_file() or path.is_symlink():
            removed.append(str(path.relative_to(app)))
            path.unlink()
    return removed


def sign(app, identity, entitlements):
    remove_signing_temporaries(app)
    for binary in binaries(app):
        if binary == app / "Contents/MacOS" / app.stem:
            continue  # The main executable is sealed with its bundle.
        if binary.parent.name == "MacOS":
            # The daemon must satisfy the GUI's designated requirement when
            # reading existing macOS file-keychain items. Both binaries are
            # owned and signed by Society; other helpers retain their identity.
            identifier = []
            if binary == app / "Contents/MacOS/SocietyDaemon":
                with (app / "Contents/Info.plist").open("rb") as source:
                    bundle_id = plistlib.load(source).get("CFBundleIdentifier")
                if not isinstance(bundle_id, str) or not bundle_id:
                    raise RuntimeError("Society's signing identifier is missing.")
                identifier = ["--identifier", bundle_id]
            run("codesign", "--force", "--sign", identity, "--timestamp=none",
                *identifier, *(["--entitlements", entitlements] if entitlements else []), binary)
            continue
        run("codesign", "--force", "--sign", identity, "--timestamp=none", binary)
    for framework in sorted(app.rglob("*.framework"), key=lambda p: len(p.parts), reverse=True):
        run("codesign", "--force", "--sign", identity, "--timestamp=none", framework)
    for bundle in sorted([app] + list(app.rglob("*.app")), key=lambda p: len(p.parts), reverse=True):
        run("codesign", "--force", "--sign", identity, "--timestamp=none",
            *(["--entitlements", entitlements] if entitlements else []), bundle)
    run("codesign", "--verify", "--deep", "--strict", app)


@contextmanager
def deployment_lock(app, lock_dir=None):
    """Serialize bundle writers with a persistent project-resource lock."""
    import fcntl
    app = app.resolve()
    if lock_dir is None:
        build_dir = next((parent for parent in app.parents if parent.name == "build"), None)
        script_dir = Path(__file__).resolve().parent
        if script_dir.name == "single-app" and script_dir.parent.name == "cmake":
            script_dir = script_dir.parent.parent
        lock_dir = (build_dir.parent if build_dir else script_dir) / "resources/.state/deploy-locks"
    else:
        lock_dir = Path(lock_dir)
    lock_dir.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(os.fsencode(app)).hexdigest()
    lock_path = lock_dir / f"{digest}.lock"
    with lock_path.open("a") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            yield app
        finally:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def deploy(args):
    with deployment_lock(args.app, getattr(args, "lock_dir", None)) as app:
        _deploy_locked(args, app)


def emit_report(report, path=None):
    output = json.dumps(report, indent=2) + "\n"
    if path is None:
        print(output, end="")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(output)


def _deploy_locked(args, app):
    removed_retired_runtimes = purge_retired_runtimes(app)
    removed_signing_temporaries = remove_signing_temporaries(app)
    helpers = native_helpers(app)
    qt = args.macdeployqt.resolve().parent.parent
    search = args.search_path + [app / "Contents/Frameworks"]
    runtime_plugins = getattr(args, "runtime_plugin", [])
    sources = prepare_native(app, args.library, search, runtime_plugins=runtime_plugins)
    for helper in helpers:
        sources.update(prepare_native(app, args.library, search, helper))
    plugins = qt_plugins(qt,args.library)
    for source in plugins:
        plugin = app / "Contents/PlugIns" / source.parent.name / source.name
        plugin.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, plugin)
    modules = []
    if args.qml_dir:
        command = [qt / "libexec/qmlimportscanner", "-rootPath", args.qml_dir]
        for path in args.qml_import + [qt / "qml"]:
            command += ["-importPath", path]
        modules = json.loads(run(*command))
        copied_modules = deploy_qml_modules(app / "Contents/Resources/qml", modules,
                                             args.compact, args.quick_style, args.embedded_resources_module)
    config = app / "Contents/Resources/qt.conf"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("[Paths]\nPlugins = PlugIns\nQmlImports = Resources/qml\nQml2Imports = Resources/qml\n")
    report = {"qmlModules": copied_modules if args.qml_dir else [],
              "removedSigningTemporaries": removed_signing_temporaries,
              "compact": args.compact, "quickStyle": args.quick_style,
              "embeddedResourceModules": args.embedded_resources_module,
              "runtimePlugins": [plugin.name for plugin in runtime_plugins]}
    complete_frameworks(app, qt)
    # Stage plugin/framework-specific non-Qt libraries too. Each executable's
    # dependency graph is resolved before relocation, with selected SDKs first.
    extra_sources = {}
    for binary in binaries(app):
        extra_sources.update(native_sources(binary, args.library, search + [qt / "lib"]))
    for name, source in extra_sources.items():
        target = app / "Contents/Frameworks" / name
        if source != target.resolve():
            if target.is_symlink():
                target.unlink()
            elif target.exists():
                target.chmod(target.stat().st_mode | 0o200)
            shutil.copy2(source, target)
    complete_frameworks(app, qt)
    if args.compact:
        # The plugin closure must exist before pruning validates every link.
        report["removedNativeEntries"] = prune_native(app, runtime_plugins)
        report["removedFrameworkDevelopmentEntries"] = prune_framework_development_files(app)
    report["removedRetiredRuntimes"] = removed_retired_runtimes + purge_retired_runtimes(app)
    localize(app)
    if args.compact and args.configuration in ("Release", "MinSizeRel"):
        report["thinnedBytes"] = thin_native(app)
        report["strippedBytes"] = strip_native(app)
    if list(app.rglob("*.app")):
        raise RuntimeError("Nested .app bundles violate the single application output contract")
    report["symbolBindings"] = verify_symbol_bindings(app)
    sign(app, args.identity, args.entitlements)
    emit_report(report, args.log)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--macdeployqt", type=Path, required=True)
    parser.add_argument("--qml-dir", type=Path)
    parser.add_argument("--qml-import", type=Path, action="append", default=[])
    parser.add_argument("--library", type=Path, action="append", default=[])
    parser.add_argument("--runtime-plugin", type=Path, action="append", default=[],
                        help="SDK-selected lazy native plugin to bundle with its dependency closure")
    parser.add_argument("--search-path", type=Path, action="append", default=[])
    parser.add_argument("--identity", default="-")
    parser.add_argument("--entitlements", type=Path)
    parser.add_argument("--compact", action="store_true")
    parser.add_argument("--quick-style", help="Deploy only the configured Qt Quick Controls style")
    parser.add_argument("--configuration", default="", help="Release/MinSizeRel enable symbol stripping with --compact")
    parser.add_argument("--embedded-resources-module", action="append", default=[],
                        help="Module whose fonts/images/icons are already embedded; retain filesystem licenses")
    parser.add_argument("--log", type=Path, help="Optional explicit report path; defaults to console output")
    parser.add_argument("--lock-dir", type=Path, help="Persistent deployment lock directory")
    deploy(parser.parse_args())
