import subprocess
import time
import os

def send_key(proc, key, count=1):
    for _ in range(count):
        cmd = f"sendkey {key}\n"
        proc.stdin.write(cmd.encode('utf-8'))
        proc.stdin.flush()
        time.sleep(0.15)

def send_string(proc, text):
    for char in text:
        if char == ' ':
            key = 'spc'
        elif char == '\n':
            key = 'ret'
        elif char == '-':
            key = 'minus'
        elif char == '.':
            key = 'dot'
        elif char >= '0' and char <= '9':
            key = char
        else:
            key = char.lower()
        proc.stdin.write(f"sendkey {key}\n".encode('utf-8'))
        proc.stdin.flush()
        time.sleep(0.15)

def run_test():
    artifact_dir = "/Users/abutaher/.gemini/antigravity-ide/brain/70229cbf-9600-4e85-b6fe-088d08558507"
    os.makedirs(artifact_dir, exist_ok=True)
    
    cmd = ["qemu-system-x86_64", "-display", "none", "-cdrom", "os.iso", "-monitor", "stdio", "-no-reboot"]
    print("Launching QEMU to test Graphical File Explorer...")
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for OS to boot and load desktop
    print("Waiting 6 seconds for boot...")
    time.sleep(6.0)
    
    # Press enter just to make sure focus is active
    send_key(proc, "ret")
    time.sleep(0.5)

    # 1. Capture initial desktop screen
    ppm_path = os.path.join(artifact_dir, "explorer_1_desktop.ppm")
    print(f"Dumping initial desktop to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # 2. Open Start Menu (Move to bottom-left: 25 Left, 18 Down)
    print("Moving mouse to Start button...")
    send_key(proc, "alt-left", 25)
    send_key(proc, "alt-down", 18)
    
    print("Clicking Start button...")
    send_key(proc, "alt-spc")
    time.sleep(1.0)
    
    ppm_path = os.path.join(artifact_dir, "explorer_2_start_menu.ppm")
    print(f"Dumping Start menu to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # 3. Move mouse to File Explorer (it's the 2nd item from the top of the start menu)
    # Cursor starts at bottom (row 24). To move up to row 17 (420px):
    # 170 / 15 = 11.3 steps. We move up 10 steps to hit Row 17 (420px).
    print("Moving mouse up to 'File Explorer'...")
    send_key(proc, "alt-up", 10)
    
    print("Clicking 'File Explorer'...")
    send_key(proc, "alt-spc")
    time.sleep(1.0)
    
    ppm_path = os.path.join(artifact_dir, "explorer_3_open.ppm")
    print(f"Dumping open File Explorer to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # 4. We are at root '/' inside File Explorer.
    # 'home' directory is highlighted by default, so enter it directly.
    print("Entering 'home' directory...")
    send_key(proc, "ret")
    time.sleep(1.0)
    
    ppm_path = os.path.join(artifact_dir, "explorer_4_home.ppm")
    print(f"Dumping '/home' directory to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # 5. Inside '/home', 'documents' is highlighted by default, so enter it directly.
    print("Entering 'documents'...")
    send_key(proc, "ret")
    time.sleep(1.0)
    
    ppm_path = os.path.join(artifact_dir, "explorer_5_documents.ppm")
    print(f"Dumping '/home/documents' directory to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # 6. Inside '/home/documents', let's create a new file via Ctrl + Shift + N.
    print("Simulating Ctrl+Shift+N to create a new file...")
    proc.stdin.write(b"sendkey ctrl-shift-n\n")
    proc.stdin.flush()
    time.sleep(1.0)
    
    ppm_path = os.path.join(artifact_dir, "explorer_6_create_modal.ppm")
    print(f"Dumping create modal state to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # Let's type a filename: "notes" and press Enter to confirm creation
    print("Typing file name 'notes' and confirming...")
    send_string(proc, "notes\n")
    time.sleep(1.0)

    ppm_path = os.path.join(artifact_dir, "explorer_7_file_created.ppm")
    print(f"Dumping directory with created file to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # 7. Open the context menu on the new file (by right clicking it).
    # Move mouse to (215, 84) from (25, 420) -> change of x: +190, y: -336
    # x: 190 / 15 = 12.6 -> 13 steps of alt-right
    # y: 336 / 15 = 22.4 -> 22 steps of alt-up
    print("Moving mouse to the first file row...")
    send_key(proc, "alt-right", 13)
    send_key(proc, "alt-up", 22)
    
    # Now right-click!
    print("Simulating right-click via QEMU monitor...")
    proc.stdin.write(b"mouse_button 4\n")
    proc.stdin.flush()
    time.sleep(0.1)
    proc.stdin.write(b"mouse_button 0\n")
    proc.stdin.flush()
    time.sleep(1.0)

    ppm_path = os.path.join(artifact_dir, "explorer_8_context_menu.ppm")
    print(f"Dumping context menu to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # Let's close the context menu by pressing Esc
    print("Closing context menu via Escape...")
    send_key(proc, "esc")
    time.sleep(0.5)

    # 8. Let's test window resizing control:
    # Current mouse is at (220, 90). Move to (700, 72):
    # x: 700 - 220 = +480 -> 32 steps of alt-right
    # y: 72 - 90 = -18 -> 1 step of alt-up
    print("Moving mouse to resize handle [R]...")
    send_key(proc, "alt-right", 32)
    send_key(proc, "alt-up", 1)
    
    # Let's click [R] to activate resize mode (Alt+Space)
    print("Activating Resize mode...")
    send_key(proc, "alt-spc")
    time.sleep(1.0)

    # In resize mode, let's press Left arrow 10 times to shrink the window width by 10 characters.
    print("Resizing window (Left arrow 10 times)...")
    send_key(proc, "left", 10)
    time.sleep(0.5)
    
    # Confirm resize with Enter
    print("Confirming resize...")
    send_key(proc, "ret")
    time.sleep(1.0)

    ppm_path = os.path.join(artifact_dir, "explorer_9_resized.ppm")
    print(f"Dumping resized window to {ppm_path}...")
    proc.stdin.write(f"screendump {ppm_path}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(0.5)

    # Quit QEMU
    print("Quitting QEMU...")
    proc.stdin.write(b"quit\n")
    proc.stdin.flush()
    
    # Capture QEMU monitor output
    stdout, stderr = proc.communicate()
    print("\n--- QEMU Monitor stdout ---")
    print(stdout.decode('utf-8', errors='ignore'))
    print("--- QEMU Monitor stderr ---")
    print(stderr.decode('utf-8', errors='ignore'))
    print("---------------------------\n")

    # Convert PPM images to PNG using sips
    print("Converting PPM screenshots to PNG...")
    for filename in os.listdir(artifact_dir):
        if filename.endswith(".ppm"):
            ppm_file = os.path.join(artifact_dir, filename)
            png_file = ppm_file.replace(".ppm", ".png")
            subprocess.run(["sips", "-s", "format", "png", ppm_file, "--out", png_file],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.remove(ppm_file)
            print(f"Converted: {filename} -> {os.path.basename(png_file)}")

if __name__ == "__main__":
    run_test()
