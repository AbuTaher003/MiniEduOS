import subprocess
import time
import os

def send_key(proc, key, count=1):
    for _ in range(count):
        cmd = f"sendkey {key}\n"
        proc.stdin.write(cmd.encode('utf-8'))
        proc.stdin.flush()
        time.sleep(0.15)

def right_click(proc):
    proc.stdin.write(b"mouse_button 4\n")
    proc.stdin.flush()
    time.sleep(0.1)
    proc.stdin.write(b"mouse_button 0\n")
    proc.stdin.flush()
    time.sleep(0.5)

def run_test():
    artifact_dir = "/Users/abutaher/.gemini/antigravity-ide/brain/70229cbf-9600-4e85-b6fe-088d08558507"
    os.makedirs(artifact_dir, exist_ok=True)
    
    cmd = ["qemu-system-x86_64", "-display", "none", "-cdrom", "os.iso", "-monitor", "stdio", "-no-reboot"]
    print("Launching QEMU for Context Menu test...")
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for boot
    time.sleep(6.0)
    
    # Send Enter to focus VM
    send_key(proc, "ret")
    time.sleep(0.5)
    
    # 1. Right click on empty desktop at center (400, 300)
    print("Right-clicking empty desktop...")
    right_click(proc)
    
    # Dump menu open state
    ppm_path = os.path.join(artifact_dir, "desktop_menu_open.ppm")
    print(f"Dumping open context menu to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)
    
    # 2. Select 'New File' (Arrow Down 2 times) and press Enter
    print("Selecting 'New File' via keyboard...")
    send_key(proc, "down", 2)
    send_key(proc, "ret")
    time.sleep(1.0)
    
    # Dump desktop with first file created
    ppm_path = os.path.join(artifact_dir, "desktop_file_created.ppm")
    print(f"Dumping file created screen to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)
    
    # 3. Right click again, select 'New File' again to test duplicate suffix resolution
    print("Right-clicking to create another file...")
    right_click(proc)
    print("Selecting 'New File' again...")
    send_key(proc, "down", 2)
    send_key(proc, "ret")
    time.sleep(1.0)
    
    # Dump desktop with second file created (New File (1).txt)
    ppm_path = os.path.join(artifact_dir, "desktop_file_created_2.ppm")
    print(f"Dumping duplicate file screen to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)
    
    # 4. Right click, select 'Terminal' (Arrow Down 3 times) to launch Terminal window
    print("Right-clicking to open Terminal...")
    right_click(proc)
    print("Selecting 'Terminal'...")
    send_key(proc, "down", 3)
    send_key(proc, "ret")
    time.sleep(1.0)
    
    # Dump desktop with Terminal open
    ppm_path = os.path.join(artifact_dir, "desktop_terminal_open.ppm")
    print(f"Dumping terminal open screen to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)
    
    # Quit QEMU
    print("Quitting QEMU...")
    proc.stdin.write(b"quit\n")
    proc.stdin.flush()
    
    stdout, stderr = proc.communicate()
    print("Test finished.")
    
    # Convert PPM images to PNG using PIL
    print("Converting PPM screenshots to PNG...")
    for filename in os.listdir(artifact_dir):
        if filename.endswith(".ppm") and filename.startswith("desktop_"):
            ppm_file = os.path.join(artifact_dir, filename)
            png_file = ppm_file.replace(".ppm", ".png")
            try:
                from PIL import Image
                Image.open(ppm_file).save(png_file)
                os.remove(ppm_file)
                print(f"Converted: {filename} -> {os.path.basename(png_file)}")
            except Exception as e:
                print(f"Failed to convert {filename}: {e}")

if __name__ == "__main__":
    run_test()
