#!/usr/bin/env python3
"""Generate PWA icons from Zehnder.png in multiple sizes"""

from PIL import Image
import os

orig_path = r'd:\Source\git\Borstel67\ComfoAirMQTTESP\Android\app\sampledata\Zehnder.png'
output_dir = r'd:\Source\git\Borstel67\ComfoAirMQTTESP\Android\app\sampledata'

# Open original image
img = Image.open(orig_path)
print(f"✓ Original image: {img.size} px, Mode: {img.mode}")

# Generate standard icons
sizes = [
    (192, 192, 'Zehnder-192.png'),
    (512, 512, 'Zehnder-512.png'),
]

for width, height, filename in sizes:
    resized = img.resize((width, height), Image.Resampling.LANCZOS)
    output_path = os.path.join(output_dir, filename)
    resized.save(output_path, 'PNG')
    file_size = os.path.getsize(output_path) / 1024
    print(f"✓ {filename} ({width}x{height}) - {file_size:.1f} KB")

# Generate maskable icons (for adaptive icons on Android)
for width, height, base_name in [(192, 192, 'Zehnder-maskable-192.png'), (512, 512, 'Zehnder-maskable-512.png')]:
    resized = img.resize((width, height), Image.Resampling.LANCZOS)
    
    # If transparent, add white background
    if resized.mode == 'RGBA':
        background = Image.new('RGB', resized.size, (255, 255, 255))
        background.paste(resized, mask=resized.split()[3])
        resized = background
    
    output_path = os.path.join(output_dir, base_name)
    resized.save(output_path, 'PNG')
    file_size = os.path.getsize(output_path) / 1024
    print(f"✓ {base_name} ({width}x{height}) - {file_size:.1f} KB")

print("\n🎉 All icons generated successfully!")
