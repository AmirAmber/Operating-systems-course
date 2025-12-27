import subprocess
import os
import time
import glob
import random

# --- CONFIGURATION ---
EXECUTABLE = "./hw2"
DEFAULT_THREADS = 4
DEFAULT_COUNTERS = 10

def clean_files():
    """Removes old txt files to ensure fresh tests."""
    for f in glob.glob("*.txt"):
        try: os.remove(f)
        except: pass

def create_cmd_file(content):
    with open("cmdfile.txt", "w") as f:
        f.write(content)

def read_counter(counter_id):
    filename = f"count{counter_id:02d}.txt"
    if not os.path.exists(filename):
        return None
    with open(filename, "r") as f:
        try:
            return int(f.read().strip())
        except ValueError:
            return None

def check_stats_file():
    if not os.path.exists("stats.txt"):
        return False
    with open("stats.txt", "r") as f:
        lines = f.readlines()
        return len(lines) >= 5

def run_executable(num_threads=DEFAULT_THREADS, num_counters=DEFAULT_COUNTERS, log_mode=1, timeout=10):
    try:
        subprocess.run(
            [EXECUTABLE, "cmdfile.txt", str(num_threads), str(num_counters), str(log_mode)],
            check=True, 
            timeout=timeout,
            stdout=subprocess.DEVNULL, # Keep terminal clean
            stderr=subprocess.PIPE
        )
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ CRASH: Program returned error code {e.returncode}.")
        return False
    except subprocess.TimeoutExpired:
        print(f"❌ TIMEOUT: Deadlock detected (waited {timeout}s).")
        return False

# --- PART 1: BASIC TESTS ---

def run_basic_tests():
    print("========================================")
    print("       PART 1: BASIC FUNCTIONALITY      ")
    print("========================================")

    # Test 1
    print("--- Test 1: Basic Logic ---")
    clean_files()
    cmd = "worker increment 0; increment 0; increment 0\nworker decrement 0\nworker increment 5; increment 5"
    create_cmd_file(cmd)
    
    if run_executable():
        c0 = read_counter(0)
        c5 = read_counter(5)
        if c0 == 2 and c5 == 2:
            print(f"🎉 PASSED (Count0={c0}, Count5={c5})")
        else:
            print(f"❌ FAILED. Expected 0:2, 5:2. Got 0:{c0}, 5:{c5}")
    print()

    # Test 2
    print("--- Test 2: Wait & Sleep ---")
    clean_files()
    cmd = "worker msleep 100; increment 1\ndispatcher_wait\nworker increment 1"
    create_cmd_file(cmd)
    
    if run_executable():
        c1 = read_counter(1)
        if c1 == 2:
            print(f"🎉 PASSED (Count1={c1})")
        else:
            print(f"❌ FAILED. Expected 2. Got {c1}")
    print()

    # Test 3
    print("--- Test 3: Repeat Logic ---")
    clean_files()
    cmd = "worker repeat 5; increment 2"
    create_cmd_file(cmd)
    
    if run_executable():
        c2 = read_counter(2)
        if c2 == 5:
            print(f"🎉 PASSED (Count2={c2})")
        else:
            print(f"❌ FAILED. Expected 5. Got {c2}")
    print()

    # Test 4
    print("--- Test 4: Simple Concurrency ---")
    clean_files()
    lines = ["worker repeat 10; increment 3"] * 40
    create_cmd_file("\n".join(lines))
    
    if run_executable():
        c3 = read_counter(3)
        if c3 == 400:
            print(f"🎉 PASSED (Count3={c3})")
        else:
            print(f"❌ FAILED. Expected 400. Got {c3} (Race Condition?)")
    print()

# --- PART 2: EXTREME TESTS ---

def run_extreme_tests():
    print("========================================")
    print("       PART 2: EXTREME STRESS TESTS     ")
    print("========================================")

    # Test 5
    print("--- Test 5: The 'Zero Sum' Torture ---")
    clean_files()
    lines = []
    for _ in range(50):
        lines.append("worker repeat 20; increment 0")
        lines.append("worker repeat 20; decrement 0")
    random.shuffle(lines)
    create_cmd_file("\n".join(lines))
    
    # 50 Threads, No Logs (Speed)
    if run_executable(num_threads=50, log_mode=0, timeout=20):
        c0 = read_counter(0)
        if c0 == 0:
            print("🎉 PASSED (Counter 0 returned to 0)")
        else:
            print(f"❌ FAILED. Expected 0. Got {c0} (Locking failure)")
    print()

    # Test 6
    print("--- Test 6: Queue Flood (1000 Jobs) ---")
    clean_files()
    lines = ["worker increment 1"] * 1000
    lines.append("dispatcher_wait")
    create_cmd_file("\n".join(lines))
    
    if run_executable(num_threads=20, log_mode=0, timeout=20):
        c1 = read_counter(1)
        if c1 == 1000:
            print("🎉 PASSED (Processed 1000 jobs)")
        else:
            print(f"❌ FAILED. Expected 1000. Got {c1}")
    print()

    # Test 7
    print("--- Test 7: Max Line Length (Buffer Overflow Check) ---")
    clean_files()
   
    cmd_part = "increment 2; " * 78
    full_line = "worker " + cmd_part
    create_cmd_file(full_line)
    
    if run_executable(num_threads=4, log_mode=0):
        c2 = read_counter(2)
        if c2 == 78:
            print("🎉 PASSED (Long line parsed correctly)")
        else:
            print(f"❌ FAILED. Expected 78. Got {c2} (Buffer likely truncated)")
    print()

    # Test 8
    print("--- Test 8: Random Chaos ---")
    clean_files()
    expected = {i: 0 for i in range(10)}
    lines = []
    for _ in range(500):
        target = random.randint(0, 9)
        lines.append(f"worker increment {target}")
        expected[target] += 1
    create_cmd_file("\n".join(lines))
    
    if run_executable(num_threads=100, log_mode=0, timeout=20):
        all_passed = True
        for i in range(10):
            val = read_counter(i)
            if val != expected[i]:
                print(f"❌ Counter {i} Mismatch: Expected {expected[i]}, Got {val}")
                all_passed = False
        if all_passed:
            print("🎉 PASSED (All random counters match)")
    print()

if __name__ == "__main__":
    if not os.path.exists(EXECUTABLE):
        print(f"Error: Executable {EXECUTABLE} not found. Did you run 'make'?")
    else:
        run_basic_tests()
        run_extreme_tests()