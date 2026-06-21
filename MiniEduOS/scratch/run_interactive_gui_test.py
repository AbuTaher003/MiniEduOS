import subprocess
import time

def send_key_cmd(proc, key, count=1):
    for _ in range(count):
        cmd = f"sendkey {key}\n"
        proc.stdin.write(cmd.encode('utf-8'))
        proc.stdin.flush()
        time.sleep(0.1)

def run_test():
    cmd = ["qemu-system-x86_64", "-display", "none", "-cdrom", "os.iso", "-monitor", "stdio", "-no-reboot"]
    print("Launching QEMU...")
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
    # Wait for boot & loading animation
    time.sleep(12.0)
    
    # Initial clean input
    proc.stdin.write(b"\n")
    proc.stdin.flush()
    time.sleep(0.5)
    
    # Move mouse from (40, 12) to (3, 24):
    # Left 37 times, Down 12 times
    print("Moving mouse cursor to the Start button...")
    send_key_cmd(proc, "alt-left", 37)
    send_key_cmd(proc, "alt-down", 12)
    
    # Click Start button (Alt + Space)
    print("Clicking the Start button...")
    send_key_cmd(proc, "alt-spc")
    time.sleep(1.0)
    
    # Capture screen with Start menu open
    print("Saving Start menu VGA dump...")
    proc.stdin.write(b"pmemsave 0xb8000 4000 vga_start_menu.bin\n")
    proc.stdin.flush()
    time.sleep(1.0)
    
    # Move mouse up to Settings option: row 19 (5 steps up from 24)
    print("Moving mouse to Settings option...")
    send_key_cmd(proc, "alt-up", 5)
    
    # Click Settings (Alt + Space)
    print("Clicking Settings...")
    send_key_cmd(proc, "alt-spc")
    time.sleep(1.0)
    
    # Capture screen with Settings window open
    print("Saving Settings open VGA dump...")
    proc.stdin.write(b"pmemsave 0xb8000 4000 vga_settings_open.bin\n")
    proc.stdin.flush()
    time.sleep(1.0)
    
    # Exit QEMU
    print("Exiting QEMU...")
    proc.stdin.write(b"quit\n")
    proc.stdin.flush()
    
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    print("Test finished.")

if __name__ == '__main__':
    run_test()
