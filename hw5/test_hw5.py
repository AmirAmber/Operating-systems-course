import os
import subprocess
import sys
import hashlib

def run_command(cmd, shell=False):
    """Run a shell command and return stdout, stderr, and return code."""
    try:
        result = subprocess.run(
            cmd,
            shell=shell,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        return result.stdout, result.stderr, result.returncode
    except Exception as e:
        return "", str(e), -1

def check_file_exists(filepath):
    if not os.path.exists(filepath):
        print(f"[FAIL] Required file '{filepath}' not found.")
        return False
    return True

def main():
    print("=== HW5 Automated Test Script ===")
    
    # Configuration
    EXECUTABLE = "./hw5"
    IMAGE_FILE = "fs.img"
    MAKE_CMD = "make"
    
    # 1. Compile
    print("\n[Step 1] Compiling...")
    out, err, rc = run_command(MAKE_CMD)
    if rc != 0:
        print(f"[FAIL] Compilation failed:\n{err}")
        return
    if not check_file_exists(EXECUTABLE):
        print(f"[FAIL] Executable '{EXECUTABLE}' not created.")
        return
    print("[PASS] Compilation successful.")

    # 2. Check for fs.img
    if not check_file_exists(IMAGE_FILE):
        print(f"[WARN] '{IMAGE_FILE}' not found in current directory. copying from ../xv6-public/fs.img if possible or strictly failing.")
        # Try to find a common location or ask user
        if os.path.exists("../xv6-public/fs.img"):
             print("Found in ../xv6-public/fs.img, proceeding with that path.")
             IMAGE_FILE = "../xv6-public/fs.img"
        else:
             print(f"[FAIL] Please copy 'fs.img' to this directory or update the script.")
             return

    # 3. Test 'ls'
    print("\n[Step 2] Testing 'ls' command...")
    cmd = [EXECUTABLE, IMAGE_FILE, "ls"]
    out, err, rc = run_command(cmd)
    
    if rc != 0:
        print(f"[FAIL] 'ls' command returned error code {rc}:\n{err}")
        return
    
    print("Output from ls:")
    print(out)
    
    lines = out.strip().split('\n')
    parsed_files = {}
    for line in lines:
        parts = line.split()
        if len(parts) >= 4:
            name = parts[0]
            type_val = parts[1]
            inum = parts[2]
            size = parts[3]
            parsed_files[name] = {'type': type_val, 'inum': inum, 'size': int(size)}
    
    if "README" in parsed_files:
        print("[PASS] Found 'README' in ls output.")
    else:
        print("[FAIL] 'README' not found in ls output. Something is wrong.")
        
    if "." in parsed_files and parsed_files["."]['type'] == '1':
        print("[PASS] Found '.' directory entry.")
    else:
        print("[FAIL] '.' directory entry missing or incorrect type.")

    # 4. Test 'cp' (Small file)
    print("\n[Step 3] Testing 'cp' with 'README'...")
    target_file = "README"
    output_file = "README_extracted"
    
    if target_file not in parsed_files:
        print(f"[SKIP] Skipping cp test because {target_file} was not found in image.")
    else:
        cmd = [EXECUTABLE, IMAGE_FILE, "cp", target_file, output_file]
        out, err, rc = run_command(cmd)
        
        if rc != 0:
             print(f"[FAIL] 'cp' command failed:\n{err}")
        elif not os.path.exists(output_file):
             print(f"[FAIL] Extracted file '{output_file}' was not created.")
        else:
             extracted_size = os.path.getsize(output_file)
             expected_size = parsed_files[target_file]['size']
             if extracted_size == expected_size:
                 print(f"[PASS] Extracted {target_file}. Size matches ({extracted_size} bytes).")
                 # Optional: Diff with local README if it exists
                 if os.path.exists("README"): # Assuming source README is in folder
                      d_out, d_err, d_rc = run_command(["diff", "README", output_file])
                      if d_rc == 0:
                          print("[PASS] Content matches local README.")
                      else:
                          print("[WARN] Content differs from local README (might be expected if local is different).")
             else:
                 print(f"[FAIL] Size mismatch. Expected {expected_size}, got {extracted_size}.")

    # 5. Test 'cp' (Non-existent file)
    print("\n[Step 4] Testing 'cp' with non-existent file...")
    fake_file = "NON_EXISTENT_FILE_123"
    cmd = [EXECUTABLE, IMAGE_FILE, "cp", fake_file, "should_fail"]
    out, err, rc = run_command(cmd)
    
    # Expectation: program doesn't crash, prints error to stderr/stdout
    expected_msg = f"File {fake_file} does not exist in the root directory"
    if expected_msg in out or expected_msg in err:
        print("[PASS] Correctly reported missing file.")
    else:
        print(f"[FAIL] Did not find expected error message '{expected_msg}'.\nStdout: {out}\nStderr: {err}")

    # 6. Test 'cp' (Large file for indirect blocks)
    # Heuristic: Find biggest file
    print("\n[Step 5] Testing 'cp' with largest file (Indirect block test)...")
    largest_file = None
    max_size = 0
    
    for name, info in parsed_files.items():
        if info['type'] == '2' and info['size'] > max_size:
            max_size = info['size']
            largest_file = name
            
    if largest_file:
        if max_size > 12 * 512:
            print(f"Testing with '{largest_file}' (Size: {max_size} bytes, needs indirect blocks).")
        else:
            print(f"Testing with '{largest_file}' (Size: {max_size} bytes). Warning: File might be too small to test indirect blocks fully.")
            
        output_large = "LARGE_extracted"
        cmd = [EXECUTABLE, IMAGE_FILE, "cp", largest_file, output_large]
        run_command(cmd)
        
        if os.path.exists(output_large) and os.path.getsize(output_large) == max_size:
            print(f"[PASS] Extracted '{largest_file}' successfully with correct size.")
        else:
            print(f"[FAIL] Failed to extract '{largest_file}' or size mismatch.")
    else:
        print("[WARN] No files found to test.")

    # Cleanup
    print("\n[Cleanup]")
    if os.path.exists("README_extracted"): os.remove("README_extracted")
    if os.path.exists("LARGE_extracted"): os.remove("LARGE_extracted")
    print("Done.")

if __name__ == "__main__":
    main()
