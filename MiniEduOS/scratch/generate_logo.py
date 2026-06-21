import os
from PIL import Image

def main():
    png_path = "assets/logo.png"
    header_path = "kernel/logo_data.h"
    
    if not os.path.exists(png_path):
        print(f"Error: {png_path} not found.")
        # Create empty header so kernel compiles cleanly even if assets are missing
        with open(header_path, "w") as f:
            f.write("#ifndef LOGO_DATA_H\n#define LOGO_DATA_H\n")
            f.write("#define LOGO_PNG_WIDTH 0\n")
            f.write("#define LOGO_PNG_HEIGHT 0\n")
            f.write("#define LOGO_PNG_SIZE 0\n")
            f.write("extern const unsigned char logo_png_file_bytes[1];\n")
            f.write("extern const unsigned char logo_png_raw_rgba[1];\n")
            f.write("#endif\n")
        return

    img = Image.open(png_path)
    img_rgba = img.convert("RGBA")
    width, height = img.size
    
    # Read PNG file bytes
    with open(png_path, "rb") as f:
        png_bytes = f.read()
    
    print(f"Processing logo: {width}x{height}, size={len(png_bytes)} bytes")
    
    # Write logo_data.h
    with open(header_path, "w") as f:
        f.write("#ifndef LOGO_DATA_H\n#define LOGO_DATA_H\n\n")
        f.write(f"#define LOGO_PNG_WIDTH {width}\n")
        f.write(f"#define LOGO_PNG_HEIGHT {height}\n")
        f.write(f"#define LOGO_PNG_SIZE {len(png_bytes)}\n\n")
        
        # Write file bytes array
        f.write("static const unsigned char logo_png_file_bytes[] = {\n")
        for i, b in enumerate(png_bytes):
            f.write(f"0x{b:02x},")
            if i % 16 == 15:
                f.write("\n")
        f.write("\n};\n\n")
        
        # Write raw RGBA pixels
        f.write("static const unsigned char logo_png_raw_rgba[] = {\n")
        pixels = list(img_rgba.getdata())
        for i, pix in enumerate(pixels):
            r, g, b, a = pix
            f.write(f"0x{r:02x},0x{g:02x},0x{b:02x},0x{a:02x},")
            if i % 4 == 3:
                f.write("\n")
        f.write("\n};\n\n")
        
        f.write("#endif\n")
    print(f"Header generated successfully: {header_path}")

if __name__ == "__main__":
    main()
