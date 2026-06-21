import subprocess
import time
import os

def send_key(proc, key, count=1):
    for _ in range(count):
        cmd = f"sendkey {key}\n"
        proc.stdin.write(cmd.encode('utf-8'))
        proc.stdin.flush()
        time.sleep(0.08)

def capture():
    os.makedirs("/Users/abutaher/.gemini/antigravity-ide/brain/70229cbf-9600-4e85-b6fe-088d08558507", exist_ok=True)
    
    cmd = ["qemu-system-x86_64", "-display", "none", "-cdrom", "os.iso", "-monitor", "stdio", "-no-reboot"]
    print("Launching QEMU to capture graphics mode boot...")
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for OS to boot and render
    time.sleep(5.0)
    
    # Capture initial screen
    path1 = "/Users/abutaher/.gemini/antigravity-ide/brain/70229cbf-9600-4e85-b6fe-088d08558507/screenshot_initial.ppm"
    print(f"Dumping initial screen to {path1}...")
    proc.stdin.write(f"screendump {path1}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # Move mouse from (400, 300) to Start button (30, 570)
    # 25 Left moves, 18 Down moves
    print("Moving mouse to the Start button...")
    send_key(proc, "alt-left", 25)
    send_key(proc, "alt-down", 18)
    
    # Click Start button (Alt + Space)
    print("Clicking the Start button...")
    send_key(proc, "alt-spc", 1)
    time.sleep(1.0)
    
    # Capture screen with Start menu open
    path2 = "/Users/abutaher/.gemini/antigravity-ide/brain/70229cbf-9600-4e85-b6fe-088d08558507/screenshot_start_menu.ppm"
    print(f"Dumping screen with Start menu to {path2}...")
    proc.stdin.write(f"screendump {path2}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)
    
    # Quit QEMU
    print("Quitting QEMU...")
    proc.stdin.write(b"quit\n")
    proc.stdin.flush()
    
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        
    print("Verification capture finished.")

if __name__ == '__main__':
    capture()
