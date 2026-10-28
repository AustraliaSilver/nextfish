import argparse
import filecmp
import re
import sys
import pathlib
import os

from testing import (
    EPD,
    TSAN,
    Stockfish as Engine,
    MiniTestFramework,
    OrderedClassMembers,
    Valgrind,
    Syzygy,
)

PATH = pathlib.Path(__file__).resolve().parent
CWD = os.getcwd()

INFO_DEPTH_RE = re.compile(r"info depth \d+ seldepth \d+ multipv \d+ score cp -?\d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv")
INFO_DEPTH_WDL_RE = re.compile(r"info depth (\d+) seldepth \d+ multipv \d+ score cp -?\d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv")


def get_prefix(args):
    if args.valgrind:
        return Valgrind.get_valgrind_command()
    if args.valgrind_thread:
        return Valgrind.get_valgrind_thread_command()

    return []


def get_threads(args):
    if args.valgrind_thread or args.sanitizer_thread:
        return 2
    return 1


def get_path(args):
    return os.path.abspath(os.path.join(CWD, args.stockfish_path))


def postfix_check(args, output):
    if args.sanitizer_undefined:
        for idx, line in enumerate(output):
            if "runtime error:" in line:
                # print next possible 50 lines
                for i in range(50):
                    debug_idx = idx + i
                    if debug_idx < len(output):
                        print(output[debug_idx])
                return False

    if args.sanitizer_thread:
        for idx, line in enumerate(output):
            if "WARNING: ThreadSanitizer:" in line:
                # print next possible 50 lines
                for i in range(50):
                    debug_idx = idx + i
                    if debug_idx < len(output):
                        print(output[debug_idx])
                return False

    return True


class EngineTestBase(metaclass=OrderedClassMembers):
    args = None

    def new_engine(self, *engine_args, **engine_kwargs):
        return Engine(get_prefix(self.args), get_path(self.args), *engine_args, **engine_kwargs)


class TestCLI(EngineTestBase):
    def beforeAll(self):
        pass

    def afterAll(self):
        pass

    def beforeEach(self):
        self.stockfish = None

    def afterEach(self):
        if self.stockfish is None:
            return
        assert postfix_check(self.args, self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    def test_eval(self):
        self.stockfish = self.new_engine("eval".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_nodes_1000(self):
        self.stockfish = self.new_engine("go nodes 1000".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_depth_10(self):
        self.stockfish = self.new_engine("go depth 10".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_perft_4(self):
        self.stockfish = self.new_engine("go perft 4".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_movetime_1000(self):
        self.stockfish = self.new_engine("go movetime 1000".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_wtime_8000_btime_8000_winc_500_binc_500(self):
        self.stockfish = self.new_engine(
            "go wtime 8000 btime 8000 winc 500 binc 500".split(" "),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_go_wtime_1000_btime_1000_winc_0_binc_0(self):
        self.stockfish = self.new_engine(
            "go wtime 1000 btime 1000 winc 0 binc 0".split(" "),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_go_wtime_1000_btime_1000_winc_0_binc_0_movestogo_5(self):
        self.stockfish = self.new_engine(
            "go wtime 1000 btime 1000 winc 0 binc 0 movestogo 5".split(" "),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_go_movetime_200(self):
        self.stockfish = self.new_engine("go movetime 200".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_nodes_20000_searchmoves_e2e4_d2d4(self):
        self.stockfish = self.new_engine(
            "go nodes 20000 searchmoves e2e4 d2d4".split(" "), True
        )
        assert self.stockfish.process.returncode == 0

    def test_bench_128_threads_8_default_depth(self):
        self.stockfish = self.new_engine(
            f"bench 128 {get_threads(self.args)} 8 default depth".split(" "),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_bench_128_threads_3_bench_tmp_epd_depth(self):
        self.stockfish = self.new_engine(
            f"bench 128 {get_threads(self.args)} 3 {os.path.join(PATH, 'bench_tmp.epd')} depth".split(
                " "
            ),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_d(self):
        self.stockfish = self.new_engine("d".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_compiler(self):
        self.stockfish = self.new_engine("compiler".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_license(self):
        self.stockfish = self.new_engine("license".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_uci(self):
        self.stockfish = self.new_engine("uci".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_export_net_verify_nnue(self):
        current_path = os.path.abspath(os.getcwd())
        self.stockfish = self.new_engine(
            f"export_net {os.path.join(current_path, 'verify.nnue')}".split(" "), True
        )
        assert self.stockfish.process.returncode == 0

    # verify the generated net equals the base net

    def test_network_equals_base(self):
        self.stockfish = self.new_engine(
            ["uci"],
            True,
        )

        output = self.stockfish.process.stdout

        # find line
        network = None
        for line in output.split("\n"):
            if "option name EvalFile type string default" in line:
                network = line.split(" ")[-1]
                break

        if network is None:
            raise RuntimeError(
                "Could not find the 'option name EvalFile' line in the engine output"
            )

        # find network file in src dir
        network = os.path.join(PATH.parent.resolve(), "src", network)

        if not os.path.exists(network):
            print(
                f"Network file {network} not found, please download the network file over the make command."
            )
            assert False

        if not filecmp.cmp(network, "verify.nnue", shallow=False):
            print(f"Network file {network} differs from the exported verify.nnue")
            assert False


class TestInteractive(EngineTestBase):
    def beforeAll(self):
        self.stockfish = self.new_engine()

    def afterAll(self):
        self.stockfish.quit()
        assert self.stockfish.close() == 0

    def afterEach(self):
        assert postfix_check(self.args, self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    def test_startup_output(self):
        self.stockfish.starts_with("Stockfish")

    def test_uci_command(self):
        self.stockfish.send_command("uci")
        self.stockfish.equals("uciok")

    def test_set_threads_option(self):
        self.stockfish.send_command(f"setoption name Threads value {get_threads(self.args)}")

    def test_ucinewgame_and_startpos_nodes_1000(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go nodes 1000")
        self.stockfish.starts_with("bestmove")

    def test_ucinewgame_and_startpos_moves(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position startpos moves e2e4 e7e6")
        self.stockfish.send_command("go nodes 1000")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_1(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1")
        self.stockfish.send_command("go nodes 1000")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_2_flip(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1")
        self.stockfish.send_command("flip")
        self.stockfish.send_command("go nodes 1000")
        self.stockfish.starts_with("bestmove")

    def test_depth_5_with_callback(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 5")

        def callback(output):
            if output.startswith("info depth") and not INFO_DEPTH_RE.match(output):
                assert False
            if output.startswith("bestmove"):
                return True
            return False

        self.stockfish.check_output(callback)

    def test_ucinewgame_and_go_depth_9(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("setoption name UCI_ShowWDL value true")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 9")

        depth = 1

        def callback(output):
            nonlocal depth

            if output.startswith("info depth"):
                match = INFO_DEPTH_WDL_RE.match(output)
                if not match or int(match.group(1)) != depth:
                    assert False
                depth += 1

            if output.startswith("bestmove"):
                assert depth == 10
                return True

            return False

        self.stockfish.check_output(callback)

    def test_clear_hash(self):
        self.stockfish.send_command("setoption name Clear Hash")

    def test_fen_position_mate_1(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 5K2/8/2qk4/2nPp3/3r4/6B1/B7/3R4 w - e6"
        )
        self.stockfish.send_command("go depth 18")

        self.stockfish.expect("* score mate 1 * pv d5e6")
        self.stockfish.equals("bestmove d5e6")

    def test_fen_position_mate_minus_1(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 2brrb2/8/p7/Q7/1p1kpPp1/1P1pN1K1/3P4/8 b - -"
        )
        self.stockfish.send_command("go depth 18")
        self.stockfish.expect("* score mate -1 *")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_fixed_node(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 5K2/8/2P1P1Pk/6pP/3p2P1/1P6/3P4/8 w - - 0 1"
        )
        self.stockfish.send_command("go nodes 500000")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_depth(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.stockfish.send_command("go depth 18 searchmoves c6d7")
        self.stockfish.expect("* score mate 2 * pv f7f5")

        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_mate(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.stockfish.send_command("go mate 2 searchmoves c6d7")
        self.stockfish.expect("* score mate 2 *")

        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_nodes(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.stockfish.send_command("go nodes 500000")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_depth_27(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen r1b2r1k/pp1p2pp/2p5/2B1q3/8/8/P1PN2PP/R4RK1 w - - 0 18"
        )
        self.stockfish.send_command("go")
        self.stockfish.contains("score mate 1")

        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_depth_and_promotion(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - - moves c6d7 f2f1q"
        )
        self.stockfish.send_command("go depth 18")
        self.stockfish.expect("* score mate 1 * pv f7f5")
        self.stockfish.starts_with("bestmove f7f5")

    def test_fen_position_with_mate_go_depth_and_searchmoves(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.stockfish.send_command("go depth 18 searchmoves c6d7")
        self.stockfish.expect("* score mate 2 * pv f7f5")

        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_moves_with_mate_go_depth_and_searchmoves(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - - moves c6d7"
        )
        self.stockfish.send_command("go depth 18 searchmoves e3e2")
        self.stockfish.expect("* score mate -1 * pv f7f5")
        self.stockfish.starts_with("bestmove")

    def test_verify_nnue_network(self):
        current_path = os.path.abspath(os.getcwd())
        self.new_engine(
            f"export_net {os.path.join(current_path, 'verify.nnue')}".split(" "), True
        )

        self.stockfish.send_command("setoption name EvalFile value verify.nnue")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 5")
        self.stockfish.starts_with("bestmove")

    def test_multipv_setting(self):
        self.stockfish.send_command("setoption name MultiPV value 4")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 5")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_skill_level(self):
        self.stockfish.send_command("setoption name Skill Level value 10")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 5")
        self.stockfish.starts_with("bestmove")

        self.stockfish.send_command("setoption name Skill Level value 20")


class TestSyzygy(EngineTestBase):
    def beforeAll(self):
        self.stockfish = self.new_engine()

    def afterAll(self):
        self.stockfish.quit()
        assert self.stockfish.close() == 0

    def afterEach(self):
        assert postfix_check(self.args, self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    def test_syzygy_setup(self):
        self.stockfish.starts_with("Stockfish")
        self.stockfish.send_command("uci")
        self.stockfish.send_command(
            f"setoption name SyzygyPath value {os.path.join(PATH, 'syzygy')}"
        )
        self.stockfish.expect(
            "info string Found 35 WDL and 35 DTZ tablebase files (up to 4-man)."
        )

    def test_syzygy_bench(self):
        self.stockfish.send_command("bench 128 1 8 default depth")
        self.stockfish.expect("Nodes searched  :*")

    def test_syzygy_position(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 4k3/PP6/8/8/8/8/8/4K3 w - - 0 1")
        self.stockfish.send_command("go depth 5")

        def check_output(output):
            if "score cp 20000" in output or "score mate" in output:
                return True

        self.stockfish.check_output(check_output)
        self.stockfish.expect("bestmove *")

    def test_syzygy_position_2(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 8/1P6/2B5/8/4K3/8/6k1/8 w - - 0 1")
        self.stockfish.send_command("go depth 5")

        def check_output(output):
            if "score cp 20000" in output or "score mate" in output:
                return True

        self.stockfish.check_output(check_output)
        self.stockfish.expect("bestmove *")

    def test_syzygy_position_3(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 8/1P6/2B5/8/4K3/8/6k1/8 b - - 0 1")
        self.stockfish.send_command("go depth 5")

        def check_output(output):
            if "score cp -20000" in output or "score mate -" in output:
                return True

        self.stockfish.check_output(check_output)
        self.stockfish.expect("bestmove *")


def parse_args():
    parser = argparse.ArgumentParser(description="Run Stockfish with testing options")
    parser.add_argument("--valgrind", action="store_true", help="Run valgrind testing")
    parser.add_argument(
        "--valgrind-thread", action="store_true", help="Run valgrind-thread testing"
    )
    parser.add_argument(
        "--sanitizer-undefined",
        action="store_true",
        help="Run sanitizer-undefined testing",
    )
    parser.add_argument(
        "--sanitizer-thread", action="store_true", help="Run sanitizer-thread testing"
    )

    parser.add_argument(
        "--none", action="store_true", help="Run without any testing options"
    )
    parser.add_argument("stockfish_path", type=str, help="Path to Stockfish binary")

    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()

    for test_class in (TestCLI, TestInteractive, TestSyzygy):
        test_class.args = args

    EPD.create_bench_epd()
    TSAN.set_tsan_option()
    Syzygy.download_syzygy()

    framework = MiniTestFramework()

    # Each test suite will be run inside a temporary directory
    framework.run([TestCLI, TestInteractive, TestSyzygy])

    EPD.delete_bench_epd()
    TSAN.unset_tsan_option()

    if framework.has_failed():
        sys.exit(1)

    sys.exit(0)
