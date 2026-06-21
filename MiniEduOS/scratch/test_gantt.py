import subprocess
import time
import os

def send_key(proc, key, count=1):
    for _ in range(count):
        cmd = f"sendkey {key}\n"
        proc.stdin.write(cmd.encode('utf-8'))
        proc.stdin.flush()
        time.sleep(0.08)

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
        time.sleep(0.08)

def capture():
    os.makedirs("/Users/abutaher/.gemini/antigravity-ide/brain/70229cbf-9600-4e85-b6fe-088d08558507", exist_ok=True)
    
    cmd = ["qemu-system-x86_64", "-display", "none", "-cdrom", "os.iso", "-monitor", "stdio", "-no-reboot"]
    print("Launching QEMU to test Gantt chart...")
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for OS to boot and render
    time.sleep(5.0)
    
    # Press Enter to clean prompt
    send_key(proc, "ret")
    time.sleep(0.5)

    # 1. Run fcfs then gantt
    print("Sending command: fcfs")
    send_string(proc, "fcfs\n")
    time.sleep(1.0)
    
    print("Sending command: gantt")
    send_string(proc, "gantt\n")
    time.sleep(1.0)
    
    path1 = "/Users/abutaher/.gemini/antigravity-ide/brain/70229cbf-9600-4e85-b6fe-088d08558507/gantt_fcfs.ppm"
    print(f"Dumping FCFS Gantt chart to {path1}...")
    proc.stdin.write(f"screendump {path1}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(1.0)

    # 2. Run gantt sjf
    print("Sending command: gantt sjf")
    send_string(proc, "gantt sjf\n")
    time.sleep(1.0)
    
    path2 = "/Users/abutaher/.gemini/antigravity-ide/brain/70229cbf-9600-4e85-b6fe-088d08558507/gantt_sjf.ppm"
    print(f"Dumping SJF Gantt chart to {path2}...")
    proc.stdin.write(f"screendump {path2}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(1.0)

    # 3. Run gantt rr 3
    print("Sending command: gantt rr 3")
    send_string(proc, "gantt rr 3\n")
    time.sleep(1.0)
    
    path3 = "/Users/abutaher/.gemini/antigravity-ide/brain/70229cbf-9600-4e85-b6fe-088d08558507/gantt_rr.ppm"
    print(f"Dumping RR Gantt chart to {path3}...")
    proc.stdin.write(f"screendump {path3}\n".encode('utf-8'))
    proc.stdin.flush()
    time.sleep(1.0)
    
    # Quit QEMU
    print("Quitting QEMU...")
    proc.stdin.write(b"quit\n")
    proc.stdin.flush()
    
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        
    print("Gantt test finished.")

if __name__ == '__main__':
    capture()
