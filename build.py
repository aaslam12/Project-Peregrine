#!/usr/bin/env python3
import argparse
import datetime
import json
import os
import platform
import re
import shutil
import subprocess
import sys


def run(cmd, cwd=None, env=None):
    print("+", " ".join(cmd), flush=True)
    subprocess.check_call(cmd, cwd=cwd, env=env)


def bool_to_cmake(value):
    return "ON" if value else "OFF"


def parse_cpu_list(text):
    cpus = set()
    for part in text.split(","):
        item = part.strip()
        if not item:
            continue
        if "-" in item:
            start, end = item.split("-", 1)
            cpus.update(range(int(start), int(end) + 1))
        else:
            cpus.add(int(item))
    return cpus


def detect_isolated_cpus():
    isolated = set()
    isolated_path = "/sys/devices/system/cpu/isolated"
    if os.path.exists(isolated_path):
        try:
            with open(isolated_path, "r", encoding="utf-8") as handle:
                isolated.update(parse_cpu_list(handle.read().strip()))
        except OSError:
            pass

    if isolated:
        return isolated

    try:
        with open("/proc/cmdline", "r", encoding="utf-8") as handle:
            cmdline = handle.read()
    except OSError:
        return isolated

    match = re.search(r"\bisolcpus=([^ ]+)", cmdline)
    if match:
        value = match.group(1).split(":", 1)[-1]
        isolated.update(parse_cpu_list(value))
    return isolated


def warn_if_cores_not_isolated(args):
    if platform.system() != "Linux":
        return

    isolated = detect_isolated_cpus()
    requested = {
        args.core_ingest,
        args.core_decode,
        args.core_match,
        args.core_logger,
        args.core_nack,
    }

    if not isolated:
        print(
            "Warning: thread pinning is enabled, but no isolated CPUs were detected via /sys/devices/system/cpu/isolated or /proc/cmdline."
        )
        return

    missing = sorted(cpu for cpu in requested if cpu not in isolated)
    if missing:
        print(
            "Warning: pinned cores are not isolated at the OS level:",
            ", ".join(str(cpu) for cpu in missing),
        )


def load_build_info(build_dir):
    path = os.path.join(build_dir, "build_info.json")
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError):
        return None


def write_run_metadata(path, data):
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, sort_keys=True)
        handle.write("\n")


def build_dir_name(args):
    parts = [args.profile, args.config.lower()]
    if args.asan:
        parts.append("asan")
    if args.tsan:
        parts.append("tsan")
    if args.coverage:
        parts.append("coverage")
    return "-".join(parts)


def main():
    parser = argparse.ArgumentParser(
        description="Build, test, benchmark, and run Project Peregrine."
    )
    parser.add_argument(
        "--profile",
        default="dev",
        choices=["dev", "bench"],
        help="Project profile. dev favors correctness/debuggability; bench favors measurement defaults.",
    )
    parser.add_argument(
        "--clean", action="store_true", help="Remove build outputs and exit."
    )
    parser.add_argument(
        "--build-only", action="store_true", help="Configure and build only."
    )
    parser.add_argument(
        "--no-tests", action="store_true", help="Disable unit test build/run."
    )
    parser.add_argument(
        "--benchmarks", action="store_true", help="Build Google Benchmark targets."
    )
    parser.add_argument(
        "--run-benchmarks",
        action="store_true",
        help="Run Google Benchmark executable after building.",
    )
    parser.add_argument(
        "--benchmark-filter", default=None, help="Google Benchmark filter regex."
    )
    parser.add_argument(
        "--benchmark-min-time",
        default=None,
        help="Google Benchmark minimum runtime per benchmark.",
    )
    parser.add_argument(
        "--artifacts-dir",
        default=None,
        help="Directory for timestamped benchmark and perf artifacts.",
    )
    parser.add_argument(
        "--static",
        action="store_true",
        help="Link libraries statically when supported.",
    )
    parser.add_argument("--install", help="Install to the specified prefix.")
    parser.add_argument(
        "--af-xdp",
        action="store_true",
        help="Enable AF_XDP support and select it as the backend.",
    )
    parser.add_argument(
        "--syscall-backend",
        action="store_true",
        help="Force the syscall socket backend.",
    )
    parser.add_argument(
        "--iperf3",
        action="store_true",
        help="Enable iperf3 support for network benchmarking.",
    )
    parser.add_argument(
        "--native",
        action="store_true",
        help="Enable -march=native tuning in dev builds.",
    )
    parser.add_argument(
        "--hdrhistogram",
        dest="hdrhistogram",
        action="store_true",
        default=True,
        help="Enable HdrHistogram support.",
    )
    parser.add_argument(
        "--no-hdrhistogram",
        dest="hdrhistogram",
        action="store_false",
        help="Disable HdrHistogram support.",
    )
    parser.add_argument(
        "--fetch-google-benchmark",
        dest="fetch_google_benchmark",
        action="store_true",
        default=True,
        help="Allow CMake FetchContent to download Google Benchmark.",
    )
    parser.add_argument(
        "--no-fetch-google-benchmark",
        dest="fetch_google_benchmark",
        action="store_false",
        help="Disable FetchContent for Google Benchmark.",
    )
    parser.add_argument(
        "--fetch-hdrhistogram",
        dest="fetch_hdrhistogram",
        action="store_true",
        default=True,
        help="Allow CMake FetchContent to download HdrHistogram_c.",
    )
    parser.add_argument(
        "--no-fetch-hdrhistogram",
        dest="fetch_hdrhistogram",
        action="store_false",
        help="Disable FetchContent for HdrHistogram_c.",
    )
    parser.add_argument(
        "--fetch-all-deps",
        action="store_true",
        help="Force CMake to fetch supported third-party dependencies instead of using system packages.",
    )
    parser.add_argument(
        "--run-format",
        action="store_true",
        help="Run the clang-format target after configuring.",
    )
    parser.add_argument(
        "--coverage",
        action="store_true",
        help="Enable dev-only coverage instrumentation.",
    )
    parser.add_argument(
        "--run-coverage",
        action="store_true",
        help="Run the dev-only coverage target after building.",
    )
    parser.add_argument(
        "--run-client",
        action="store_true",
        help="Run the client binary after building.",
    )
    parser.add_argument(
        "--run-server",
        action="store_true",
        help="Run the server binary after building.",
    )
    parser.add_argument(
        "--run-perf",
        choices=["client", "server", "benchmarks"],
        help="Run the perf helper script against the selected binary.",
    )
    parser.add_argument(
        "--perf-events",
        default="cycles,instructions,branches,branch-misses,L1-icache-load-misses,LLC-load-misses,cache-misses",
        help="Comma-separated perf event list.",
    )
    parser.add_argument(
        "--flamegraphs",
        action="store_true",
        help="Generate flamegraph SVGs when running perf.",
    )
    parser.add_argument(
        "--no-flamegraphs",
        action="store_true",
        help="Disable flamegraph SVG generation when running perf.",
    )
    parser.add_argument(
        "--flamegraph-dir",
        default=None,
        help="Directory containing FlameGraph scripts.",
    )
    parser.add_argument(
        "--client-only", action="store_true", help="Build only the client binary."
    )
    parser.add_argument(
        "--server-only", action="store_true", help="Build only the server binary."
    )
    parser.add_argument(
        "--target",
        action="append",
        default=[],
        help="Additional CMake build target(s) to build.",
    )
    parser.add_argument(
        "--core-ingest", type=int, default=0, help="Pinned core id for ingest thread."
    )
    parser.add_argument(
        "--core-decode", type=int, default=1, help="Pinned core id for decode thread."
    )
    parser.add_argument(
        "--core-match", type=int, default=2, help="Pinned core id for match thread."
    )
    parser.add_argument(
        "--core-logger", type=int, default=3, help="Pinned core id for logger thread."
    )
    parser.add_argument(
        "--core-nack", type=int, default=4, help="Pinned core id for NACK thread."
    )

    sanitizer_group = parser.add_mutually_exclusive_group()
    sanitizer_group.add_argument(
        "--asan",
        action="store_true",
        help="Enable AddressSanitizer + UndefinedBehaviorSanitizer (Debug only).",
    )
    sanitizer_group.add_argument(
        "--tsan",
        action="store_true",
        help="Enable ThreadSanitizer (Debug only).",
    )

    args = parser.parse_args()

    if args.client_only and args.server_only:
        print("Error: --client-only and --server-only are mutually exclusive.")
        sys.exit(1)

    args.config = "Release" if args.profile == "bench" else "Debug"

    if args.run_coverage:
        args.coverage = True

    if (args.asan or args.tsan or args.coverage) and args.config != "Debug":
        print("Error: sanitizers and coverage are only supported for the dev profile.")
        sys.exit(1)

    if args.coverage and (args.asan or args.tsan):
        print("Error: coverage, ASan, and TSan builds must be separate.")
        sys.exit(1)

    if args.run_coverage and (not shutil.which("lcov") or not shutil.which("genhtml")):
        print("Error: --run-coverage requires lcov and genhtml.")
        sys.exit(1)

    if args.af_xdp and args.syscall_backend:
        print("Error: choose either --af-xdp or --syscall-backend, not both.")
        sys.exit(1)

    if args.flamegraphs and args.no_flamegraphs:
        print("Error: choose either --flamegraphs or --no-flamegraphs, not both.")
        sys.exit(1)

    project_root = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.join(project_root, "build", build_dir_name(args))

    exe_suffix = ".exe" if platform.system() == "Windows" else ""
    client_exe = os.path.join(build_dir, f"peregrine-client{exe_suffix}")
    server_exe = os.path.join(build_dir, f"peregrine-server{exe_suffix}")
    benchmark_exe = os.path.join(build_dir, f"peregrine-benchmarks{exe_suffix}")

    if args.clean:
        print(f"=== Cleaning {os.path.join(project_root, 'build')} ===")
        shutil.rmtree(os.path.join(project_root, "build"), ignore_errors=True)
        compile_commands_path = os.path.join(project_root, "compile_commands.json")
        if os.path.lexists(compile_commands_path):
            os.remove(compile_commands_path)
        return

    build_tests = (args.config == "Debug") and not args.no_tests
    if args.coverage:
        build_tests = True
    build_benchmarks = args.benchmarks or args.run_benchmarks
    build_client = not args.server_only
    build_server = not args.client_only

    pinning = args.profile == "bench"
    lto = args.profile == "bench"
    native = args.profile == "bench" or args.native

    if not args.af_xdp and not args.syscall_backend:
        args.syscall_backend = True

    if pinning:
        warn_if_cores_not_isolated(args)

    cmake_args = [
        f"-DPEREGRINE_PROFILE={args.profile}",
        f"-DCMAKE_BUILD_TYPE={args.config}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        f"-DPEREGRINE_BUILD_TESTS={bool_to_cmake(build_tests)}",
        f"-DPEREGRINE_BUILD_BENCHMARKS={bool_to_cmake(build_benchmarks)}",
        f"-DPEREGRINE_BUILD_STRESS_TESTS={bool_to_cmake(build_benchmarks)}",
        f"-DPEREGRINE_STATIC_LINKING={bool_to_cmake(args.static)}",
        f"-DPEREGRINE_AF_XDP={bool_to_cmake(args.af_xdp)}",
        f"-DPEREGRINE_NETWORK_BACKEND_AFXDP={bool_to_cmake(args.af_xdp)}",
        f"-DPEREGRINE_NETWORK_BACKEND_SYSCALL={bool_to_cmake(args.syscall_backend)}",
        f"-DPEREGRINE_IPERF3={bool_to_cmake(args.iperf3)}",
        f"-DPEREGRINE_USE_THREAD_PINNING={bool_to_cmake(pinning)}",
        f"-DPEREGRINE_FORCE_FETCH_DEPS={bool_to_cmake(args.fetch_all_deps)}",
        f"-DPEREGRINE_ENABLE_NATIVE_TUNING={bool_to_cmake(native)}",
        f"-DPEREGRINE_ENABLE_LTO={bool_to_cmake(lto)}",
        f"-DPEREGRINE_ENABLE_PROFILING=ON",
        "-DPEREGRINE_USE_CLANG_TIDY=ON",
        "-DPEREGRINE_USE_CLANG_FORMAT=ON",
        f"-DPEREGRINE_GENERATE_COVERAGE={bool_to_cmake(args.coverage)}",
        f"-DPEREGRINE_ENABLE_SANITIZERS={bool_to_cmake(args.asan)}",
        f"-DPEREGRINE_ENABLE_TSAN={bool_to_cmake(args.tsan)}",
        f"-DPEREGRINE_BUILD_CLIENT={bool_to_cmake(build_client)}",
        f"-DPEREGRINE_BUILD_SERVER={bool_to_cmake(build_server)}",
        f"-DPEREGRINE_ENABLE_HDRHISTOGRAM={bool_to_cmake(args.hdrhistogram)}",
        f"-DPEREGRINE_FETCH_GOOGLE_BENCHMARK={bool_to_cmake(args.fetch_google_benchmark)}",
        f"-DPEREGRINE_FETCH_HDRHISTOGRAM={bool_to_cmake(args.fetch_hdrhistogram)}",
        f"-DPEREGRINE_CORE_INGEST={args.core_ingest}",
        f"-DPEREGRINE_CORE_DECODE={args.core_decode}",
        f"-DPEREGRINE_CORE_MATCH={args.core_match}",
        f"-DPEREGRINE_CORE_LOGGER={args.core_logger}",
        f"-DPEREGRINE_CORE_NACK={args.core_nack}",
    ]

    os.makedirs(build_dir, exist_ok=True)

    print(f"=== Configuring ({args.profile}/{args.config}) ===")
    cmd_config = ["cmake", "-S", project_root, "-B", build_dir]
    if shutil.which("ninja"):
        cmd_config.extend(["-G", "Ninja"])
    cmd_config.extend(cmake_args)
    run(cmd_config)

    if args.run_format:
        print("=== Running format target ===")
        run(["cmake", "--build", build_dir, "--target", "format"])

    print(f"=== Building ({args.profile}/{args.config}) ===")
    build_cmd = ["cmake", "--build", build_dir, "--parallel", str(os.cpu_count() or 1)]
    if args.target:
        for target in args.target:
            run(build_cmd + ["--target", target])
    else:
        run(build_cmd)

    compile_commands_src = os.path.join(build_dir, "compile_commands.json")
    compile_commands_dst = os.path.join(project_root, "compile_commands.json")
    if os.path.exists(compile_commands_src):
        try:
            if os.path.lexists(compile_commands_dst):
                os.remove(compile_commands_dst)
            try:
                os.symlink(compile_commands_src, compile_commands_dst)
            except OSError:
                shutil.copy(compile_commands_src, compile_commands_dst)
        except OSError as exc:
            print(f"Warning: could not link compile_commands.json: {exc}")

    if args.build_only:
        print("Build complete.")
        return

    env = os.environ.copy()
    env["CTEST_COLOR_OUTPUT"] = "ON"
    env["CLICOLOR_FORCE"] = "1"

    if args.tsan:
        env.setdefault("TSAN_OPTIONS", "suppress_equal_stacks=1")
    if args.asan:
        env.setdefault("ASAN_OPTIONS", "halt_on_error=0:detect_leaks=1")

    if build_tests:
        print("=== Running unit tests ===")
        try:
            run(
                ["ctest", "--output-on-failure", "--test-dir", build_dir, "-L", "unit"],
                env=env,
            )
        except subprocess.CalledProcessError:
            print("Unit tests failed.")

    if build_benchmarks and args.run_benchmarks:
        if os.path.exists(benchmark_exe):
            print("=== Running benchmarks ===")
            timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            artifacts_root = args.artifacts_dir or os.path.join(
                project_root, "artifacts"
            )
            benchmark_dir = os.path.join(artifacts_root, "benchmarks")
            os.makedirs(benchmark_dir, exist_ok=True)

            json_out = os.path.join(benchmark_dir, f"results_{timestamp}.json")
            metadata_out = os.path.join(benchmark_dir, f"results_{timestamp}.meta.json")

            common_args = [benchmark_exe]
            if args.benchmark_filter:
                common_args.append(f"--benchmark_filter={args.benchmark_filter}")
            if args.benchmark_min_time:
                common_args.append(f"--benchmark_min_time={args.benchmark_min_time}")

            run(
                common_args
                + [f"--benchmark_out={json_out}", "--benchmark_out_format=json"],
                env=env,
            )

            metadata = {
                "timestamp": timestamp,
                "profile": args.profile,
                "config": args.config,
                "json_output": json_out,
                "build_info": load_build_info(build_dir),
            }
            write_run_metadata(metadata_out, metadata)
            print(f"Benchmark artifacts written to {benchmark_dir}")
        else:
            print(f"Warning: benchmark executable not found at {benchmark_exe}")

    if args.run_coverage:
        print("=== Running coverage target ===")
        run(["cmake", "--build", build_dir, "--target", "coverage"], env=env)

    if args.run_perf:
        script_path = os.path.join(project_root, "scripts", "perf_profile.sh")
        target_map = {
            "client": client_exe,
            "server": server_exe,
            "benchmarks": benchmark_exe,
        }
        perf_target = target_map[args.run_perf]
        if not os.path.exists(script_path):
            print(f"Error: perf helper script not found at {script_path}")
            sys.exit(1)
        if not os.path.exists(perf_target):
            print(f"Error: selected perf target not found at {perf_target}")
            sys.exit(1)

        artifacts_root = args.artifacts_dir or os.path.join(project_root, "artifacts")
        perf_dir = os.path.join(artifacts_root, "perf")
        os.makedirs(perf_dir, exist_ok=True)

        perf_cmd = [
            script_path,
            "--target",
            perf_target,
            "--artifacts-dir",
            perf_dir,
            "--label",
            args.run_perf,
            "--events",
            args.perf_events,
        ]
        if args.flamegraph_dir:
            perf_cmd.extend(["--flamegraph-dir", args.flamegraph_dir])
        if args.flamegraphs:
            perf_cmd.append("--flamegraphs")
        if args.no_flamegraphs:
            perf_cmd.append("--no-flamegraphs")
        run(perf_cmd, env=env)

    if args.run_server:
        if os.path.exists(server_exe):
            print("=== Running Peregrine Server ===")
            run([server_exe], env=env)
        else:
            print(f"Error: server executable not found at {server_exe}")
            sys.exit(1)

    if args.run_client:
        if os.path.exists(client_exe):
            print("=== Running Peregrine Client ===")
            run([client_exe], env=env)
        else:
            print(f"Error: client executable not found at {client_exe}")
            sys.exit(1)

    if args.install:
        print(f"=== Installing to {args.install} ===")
        run(["cmake", "--install", build_dir, "--prefix", args.install])


if __name__ == "__main__":
    main()
